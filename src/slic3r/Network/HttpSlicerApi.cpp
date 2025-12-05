#include "HttpSlicerApi.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/Studio.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"

#if SLIC3R_HAS_MOSQUITTO
#include "MqttConfigPublisher.hpp"
#endif

#include <wx/app.h>

// cpp-httplib is a header-only library
// Download from: https://github.com/yhirose/cpp-httplib
// Place httplib.h in deps or include path
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <sstream>

namespace Slic3r {

namespace {
    // Simple JSON escape helper
    std::string json_escape(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c; break;
            }
        }
        return result;
    }

    std::string status_to_json(const PrintBase::SlicingStatus& status,
                               bool has_completed,
                               const SlicingCompletedInfo& completed) {
        std::ostringstream json;
        json << "{";
        json << "\"percent\":" << status.percent << ",";
        json << "\"message\":\"" << json_escape(status.text) << "\",";
        json << "\"flags\":" << status.flags << ",";
        json << "\"warning_step\":" << status.warning_step;

        if (has_completed) {
            json << ",\"completed\":{";
            json << "\"status\":";
            switch (completed.status) {
                case SlicingCompletedInfo::Finished:  json << "\"finished\""; break;
                case SlicingCompletedInfo::Cancelled: json << "\"cancelled\""; break;
                case SlicingCompletedInfo::Error:     json << "\"error\""; break;
            }
            if (!completed.error_message.empty()) {
                json << ",\"error_message\":\"" << json_escape(completed.error_message) << "\"";
            }
            json << "}";
        }

        json << "}";
        return json.str();
    }
}

HttpSlicerApi::HttpSlicerApi(const HttpApiConfig& config, BackgroundSlicingProcess* process)
    : m_config(config)
    , m_process(process)
    , m_server(std::make_unique<httplib::Server>())
    , m_last_status(0, "")  // Initialize with empty status
{
    setup_routes();
}

HttpSlicerApi::~HttpSlicerApi()
{
    stop();
}

void HttpSlicerApi::setup_routes()
{
    if (!m_server) return;

    // CORS headers
    if (m_config.enable_cors) {
        m_server->set_default_headers({
            {"Access-Control-Allow-Origin", "*"},
            {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
            {"Access-Control-Allow-Headers", "Content-Type"}
        });
    }

    // Health check
    m_server->Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // Get current status
    m_server->Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        std::string json = status_to_json(m_last_status, m_has_completed, m_last_completed);
        res.set_content(json, "application/json");
    });

    // Start slicing
    m_server->Post("/api/start", [this](const httplib::Request&, httplib::Response& res) {
        if (!m_process) {
            res.status = 500;
            res.set_content("{\"error\":\"No process configured\"}", "application/json");
            return;
        }

        bool started = m_process->start();
        if (started) {
            // Reset completed status
            {
                std::lock_guard<std::mutex> lock(m_status_mutex);
                m_has_completed = false;
            }
            res.set_content("{\"status\":\"started\"}", "application/json");
        } else {
            res.status = 400;
            res.set_content("{\"error\":\"Could not start (already running or empty)\"}", "application/json");
        }
    });

    // Stop slicing
    m_server->Post("/api/stop", [this](const httplib::Request&, httplib::Response& res) {
        if (!m_process) {
            res.status = 500;
            res.set_content("{\"error\":\"No process configured\"}", "application/json");
            return;
        }

        bool stopped = m_process->stop();
        if (stopped) {
            res.set_content("{\"status\":\"stopped\"}", "application/json");
        } else {
            res.status = 400;
            res.set_content("{\"error\":\"Could not stop (not running)\"}", "application/json");
        }
    });

    // Reset slicing
    m_server->Post("/api/reset", [this](const httplib::Request&, httplib::Response& res) {
        if (!m_process) {
            res.status = 500;
            res.set_content("{\"error\":\"No process configured\"}", "application/json");
            return;
        }

        m_process->reset();
        {
            std::lock_guard<std::mutex> lock(m_status_mutex);
            m_has_completed = false;
            m_last_status = PrintBase::SlicingStatus(0, "");  // Reset to empty status
        }
        res.set_content("{\"status\":\"reset\"}", "application/json");
    });

    // Get process state
    m_server->Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        if (!m_process) {
            res.status = 500;
            res.set_content("{\"error\":\"No process configured\"}", "application/json");
            return;
        }

        std::ostringstream json;
        json << "{";
        json << "\"idle\":" << (m_process->idle() ? "true" : "false") << ",";
        json << "\"running\":" << (m_process->running() ? "true" : "false") << ",";
        json << "\"finished\":" << (m_process->finished() ? "true" : "false") << ",";
        json << "\"empty\":" << (m_process->empty() ? "true" : "false");
        json << "}";
        res.set_content(json.str(), "application/json");
    });

    // Get available printers
    m_server->Get("/api/printers", [](const httplib::Request&, httplib::Response& res) {
        if (!GUI::Studio::is_initialized()) {
            res.status = 500;
            res.set_content("{\"error\":\"Studio not initialized\"}", "application/json");
            return;
        }

        PresetBundle* bundle = GUI::Studio::instance().preset_bundle();
        if (!bundle) {
            res.status = 500;
            res.set_content("{\"error\":\"PresetBundle not available\"}", "application/json");
            return;
        }

        std::ostringstream json;
        json << "{\"printers\":[";

        bool first = true;
        for (const Preset& preset : bundle->printers.get_presets()) {
            if (!preset.is_visible) continue;

            if (!first) json << ",";
            first = false;

            json << "{";
            json << "\"name\":\"" << json_escape(preset.name) << "\",";
            json << "\"is_system\":" << (preset.is_system ? "true" : "false") << ",";
            json << "\"is_default\":" << (preset.is_default ? "true" : "false") << ",";
            json << "\"is_external\":" << (preset.is_external ? "true" : "false") << ",";
            json << "\"is_visible\":" << (preset.is_visible ? "true" : "false") << ",";
            json << "\"is_compatible\":" << (preset.is_compatible ? "true" : "false");

            // Add printer model info if available
            const std::string* model = preset.config.option<ConfigOptionString>("printer_model") ?
                &preset.config.option<ConfigOptionString>("printer_model")->value : nullptr;
            if (model && !model->empty()) {
                json << ",\"model\":\"" << json_escape(*model) << "\"";
            }

            const std::string* variant = preset.config.option<ConfigOptionString>("printer_variant") ?
                &preset.config.option<ConfigOptionString>("printer_variant")->value : nullptr;
            if (variant && !variant->empty()) {
                json << ",\"variant\":\"" << json_escape(*variant) << "\"";
            }

            json << "}";
        }

        json << "],";

        // Add currently selected printer
        const Preset& selected = bundle->printers.get_selected_preset();
        json << "\"selected\":\"" << json_escape(selected.name) << "\"";

        json << "}";
        res.set_content(json.str(), "application/json");
    });

    // Select a printer preset
    // POST /api/printers/select with JSON body: {"name": "printer_name"}
    // This implements the same logic as Tab::select_preset in wxWidgets
    m_server->Post("/api/printers/select", [](const httplib::Request& req, httplib::Response& res) {
        if (!GUI::Studio::is_initialized()) {
            res.status = 500;
            res.set_content("{\"error\":\"Studio not initialized\"}", "application/json");
            return;
        }

        PresetBundle* bundle = GUI::Studio::instance().preset_bundle();
        if (!bundle) {
            res.status = 500;
            res.set_content("{\"error\":\"PresetBundle not available\"}", "application/json");
            return;
        }

        // Parse JSON body to get printer name
        std::string printer_name;

        // Simple JSON parsing for {"name": "value"}
        const std::string& body = req.body;
        size_t name_pos = body.find("\"name\"");
        if (name_pos != std::string::npos) {
            size_t colon_pos = body.find(':', name_pos);
            if (colon_pos != std::string::npos) {
                size_t quote_start = body.find('"', colon_pos);
                if (quote_start != std::string::npos) {
                    size_t quote_end = body.find('"', quote_start + 1);
                    if (quote_end != std::string::npos) {
                        printer_name = body.substr(quote_start + 1, quote_end - quote_start - 1);
                    }
                }
            }
        }

        if (printer_name.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing or invalid 'name' field in request body\"}", "application/json");
            return;
        }

        // Find the preset
        Preset* preset = bundle->printers.find_preset(printer_name, false);
        if (!preset) {
            res.status = 404;
            res.set_content("{\"error\":\"Printer preset not found\",\"name\":\"" + json_escape(printer_name) + "\"}", "application/json");
            return;
        }

        // Get previous printer name for MQTT notification
        std::string prev_printer = bundle->printers.get_selected_preset().name;

        // Select the preset (like Tab::select_preset does)
        bool selected = bundle->printers.select_preset_by_name(printer_name, false);
        if (!selected) {
            res.status = 400;
            res.set_content("{\"error\":\"Could not select printer preset\",\"name\":\"" + json_escape(printer_name) + "\"}", "application/json");
            return;
        }

        // Update compatibility for filaments and prints (like Tab::select_preset does)
        // This ensures filament/print presets are marked compatible/incompatible with new printer
        bundle->update_compatible(PresetSelectCompatibleType::Always, PresetSelectCompatibleType::Always);

        // Schedule UI update on wx main thread (like Tab::on_presets_changed does)
        // This updates the sidebar combo boxes and refreshes the UI
        std::string name_copy = printer_name;
        wxTheApp->CallAfter([name_copy]() {
            auto* app = dynamic_cast<GUI::GUI_App*>(wxTheApp);
            if (app && app->plater()) {
                // Update preset combo boxes in sidebar
                app->plater()->sidebar().update_presets(Preset::TYPE_PRINTER);
                // Also update dependent presets (filaments, prints) since printer changed
                app->plater()->sidebar().update_presets(Preset::TYPE_FILAMENT);
                app->plater()->sidebar().update_presets(Preset::TYPE_PRINT);
                // Mark project as dirty
                app->plater()->update_project_dirty_from_presets();
            }
        });

#if SLIC3R_HAS_MOSQUITTO
        // Publish printer change to MQTT
        if (auto mqtt = GUI::Studio::instance().mqtt_publisher()) {
            if (mqtt->is_connected()) {
                // Publish preset change event
                std::ostringstream payload;
                payload << "{\"event\":\"printer_changed\",";
                payload << "\"previous\":\"" << json_escape(prev_printer) << "\",";
                payload << "\"current\":\"" << json_escape(printer_name) << "\"}";
                mqtt->publish("config/presets/printer", payload.str(), true);

                // Publish all printer settings to slicer/config/printer/... topics
                mqtt->publish_printer_config(preset->config, printer_name);
            }
        }
#endif

        std::ostringstream json;
        json << "{\"status\":\"selected\",\"name\":\"" << json_escape(printer_name) << "\"}";
        res.set_content(json.str(), "application/json");
    });

    // Get available filaments
    m_server->Get("/api/filaments", [](const httplib::Request&, httplib::Response& res) {
        if (!GUI::Studio::is_initialized()) {
            res.status = 500;
            res.set_content("{\"error\":\"Studio not initialized\"}", "application/json");
            return;
        }

        PresetBundle* bundle = GUI::Studio::instance().preset_bundle();
        if (!bundle) {
            res.status = 500;
            res.set_content("{\"error\":\"PresetBundle not available\"}", "application/json");
            return;
        }

        std::ostringstream json;
        json << "{\"filaments\":[";

        bool first = true;
        for (const Preset& preset : bundle->filaments.get_presets()) {
            if (!preset.is_visible) continue;

            if (!first) json << ",";
            first = false;

            json << "{";
            json << "\"name\":\"" << json_escape(preset.name) << "\",";
            json << "\"is_system\":" << (preset.is_system ? "true" : "false") << ",";
            json << "\"is_default\":" << (preset.is_default ? "true" : "false") << ",";
            json << "\"is_external\":" << (preset.is_external ? "true" : "false") << ",";
            json << "\"is_visible\":" << (preset.is_visible ? "true" : "false") << ",";
            json << "\"is_compatible\":" << (preset.is_compatible ? "true" : "false");

            // Add filament type if available
            const std::string* type = preset.config.option<ConfigOptionStrings>("filament_type") ?
                (preset.config.option<ConfigOptionStrings>("filament_type")->values.empty() ? nullptr :
                 &preset.config.option<ConfigOptionStrings>("filament_type")->values[0]) : nullptr;
            if (type && !type->empty()) {
                json << ",\"type\":\"" << json_escape(*type) << "\"";
            }

            // Add filament color if available
            const std::string* color = preset.config.option<ConfigOptionStrings>("filament_colour") ?
                (preset.config.option<ConfigOptionStrings>("filament_colour")->values.empty() ? nullptr :
                 &preset.config.option<ConfigOptionStrings>("filament_colour")->values[0]) : nullptr;
            if (color && !color->empty()) {
                json << ",\"color\":\"" << json_escape(*color) << "\"";
            }

            json << "}";
        }

        json << "],";

        // Add currently selected filaments (one per extruder)
        json << "\"selected\":[";
        for (size_t i = 0; i < bundle->filament_presets.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << json_escape(bundle->filament_presets[i]) << "\"";
        }
        json << "]";

        json << "}";
        res.set_content(json.str(), "application/json");
    });

    // Select a filament preset
    // POST /api/filaments/select with JSON body: {"name": "filament_name", "extruder": 0}
    // extruder is optional, defaults to 0
    m_server->Post("/api/filaments/select", [](const httplib::Request& req, httplib::Response& res) {
        if (!GUI::Studio::is_initialized()) {
            res.status = 500;
            res.set_content("{\"error\":\"Studio not initialized\"}", "application/json");
            return;
        }

        PresetBundle* bundle = GUI::Studio::instance().preset_bundle();
        if (!bundle) {
            res.status = 500;
            res.set_content("{\"error\":\"PresetBundle not available\"}", "application/json");
            return;
        }

        // Parse JSON body
        std::string filament_name;
        int extruder_idx = 0;

        const std::string& body = req.body;

        // Parse "name" field
        size_t name_pos = body.find("\"name\"");
        if (name_pos != std::string::npos) {
            size_t colon_pos = body.find(':', name_pos);
            if (colon_pos != std::string::npos) {
                size_t quote_start = body.find('"', colon_pos);
                if (quote_start != std::string::npos) {
                    size_t quote_end = body.find('"', quote_start + 1);
                    if (quote_end != std::string::npos) {
                        filament_name = body.substr(quote_start + 1, quote_end - quote_start - 1);
                    }
                }
            }
        }

        // Parse "extruder" field (optional)
        size_t extruder_pos = body.find("\"extruder\"");
        if (extruder_pos != std::string::npos) {
            size_t colon_pos = body.find(':', extruder_pos);
            if (colon_pos != std::string::npos) {
                // Find the number after the colon
                size_t num_start = colon_pos + 1;
                while (num_start < body.size() && (body[num_start] == ' ' || body[num_start] == '\t')) {
                    ++num_start;
                }
                size_t num_end = num_start;
                while (num_end < body.size() && body[num_end] >= '0' && body[num_end] <= '9') {
                    ++num_end;
                }
                if (num_end > num_start) {
                    extruder_idx = std::stoi(body.substr(num_start, num_end - num_start));
                }
            }
        }

        if (filament_name.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"Missing or invalid 'name' field in request body\"}", "application/json");
            return;
        }

        // Validate extruder index
        if (extruder_idx < 0 || extruder_idx >= static_cast<int>(bundle->filament_presets.size())) {
            res.status = 400;
            std::ostringstream err;
            err << "{\"error\":\"Invalid extruder index\",\"extruder\":" << extruder_idx;
            err << ",\"max_extruders\":" << bundle->filament_presets.size() << "}";
            res.set_content(err.str(), "application/json");
            return;
        }

        // Find the preset
        Preset* preset = bundle->filaments.find_preset(filament_name, false);
        if (!preset) {
            res.status = 404;
            res.set_content("{\"error\":\"Filament preset not found\",\"name\":\"" + json_escape(filament_name) + "\"}", "application/json");
            return;
        }

        // Get previous filament name for MQTT notification
        std::string prev_filament = (extruder_idx < static_cast<int>(bundle->filament_presets.size()))
            ? bundle->filament_presets[extruder_idx] : "";

        // Select the filament for the specified extruder
        // First, update the filament_presets array
        bundle->set_filament_preset(extruder_idx, filament_name);

        // Schedule UI update on wx main thread (like Tab::on_presets_changed does)
        // We need to call filaments.select_preset_by_name() on the main thread
        // because update_presets reads from filaments.get_selected_preset_name()
        std::string name_copy = filament_name;
        int ext_idx_copy = extruder_idx;
        wxTheApp->CallAfter([name_copy, ext_idx_copy]() {
            auto* app = dynamic_cast<GUI::GUI_App*>(wxTheApp);
            if (app && app->plater() && app->preset_bundle) {
                // Select the preset in the collection (so update_presets sees it)
                app->preset_bundle->filaments.select_preset_by_name(name_copy, false);
                // Update the filament_presets array again (in case something changed)
                app->preset_bundle->set_filament_preset(ext_idx_copy, name_copy);
                // Update filament preset combo boxes in sidebar
                app->plater()->sidebar().update_presets(Preset::TYPE_FILAMENT);
                // Mark project as dirty
                app->plater()->update_project_dirty_from_presets();
            }
        });

#if SLIC3R_HAS_MOSQUITTO
        // Publish filament change to MQTT
        if (auto mqtt = GUI::Studio::instance().mqtt_publisher()) {
            if (mqtt->is_connected()) {
                // Publish preset change event
                std::ostringstream payload;
                payload << "{\"event\":\"filament_changed\",";
                payload << "\"extruder\":" << extruder_idx << ",";
                payload << "\"previous\":\"" << json_escape(prev_filament) << "\",";
                payload << "\"current\":\"" << json_escape(filament_name) << "\"}";
                mqtt->publish("config/presets/filament", payload.str(), true);

                // Publish all filament settings to slicer/config/filament/{extruder}/... topics
                mqtt->publish_filament_config(preset->config, filament_name, extruder_idx);
            }
        }
#endif

        std::ostringstream json;
        json << "{\"status\":\"selected\",\"name\":\"" << json_escape(filament_name);
        json << "\",\"extruder\":" << extruder_idx << "}";
        res.set_content(json.str(), "application/json");
    });

    // OPTIONS handler for CORS preflight
    m_server->Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });
}

bool HttpSlicerApi::start()
{
    if (m_running) return true;

    m_running = true;
    m_server_thread = std::thread(&HttpSlicerApi::server_thread_func, this);

    return true;
}

void HttpSlicerApi::stop()
{
    if (!m_running) return;

    m_running = false;
    if (m_server) {
        m_server->stop();
    }

    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
}

bool HttpSlicerApi::is_running() const
{
    return m_running;
}

void HttpSlicerApi::server_thread_func()
{
    if (m_server) {
        m_server->listen(m_config.bind_address.c_str(), m_config.port);
    }
    m_running = false;
}

void HttpSlicerApi::update_status(const PrintBase::SlicingStatus& status)
{
    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_last_status = status;
}

void HttpSlicerApi::update_completed(const SlicingCompletedInfo& info)
{
    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_last_completed = info;
    m_has_completed = true;
}

} // namespace Slic3r
