#include "MqttConfigPublisher.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <mosquitto.h>
#include <sstream>
#include <iomanip>
#include <cfloat>
#include <climits>

namespace {
    std::string json_escape(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c; break;
            }
        }
        return result;
    }
}

namespace Slic3r {

std::shared_ptr<MqttConfigPublisher> MqttConfigPublisher::create()
{
    // Using new because constructor is private
    return std::shared_ptr<MqttConfigPublisher>(new MqttConfigPublisher());
}

MqttConfigPublisher::MqttConfigPublisher()
{
    init_topic_map();
}

void MqttConfigPublisher::register_with_dispatcher()
{
    ConfigChangeDispatcher::instance().add_listener(weak_from_this());
}

void MqttConfigPublisher::on_config_change(const std::string& opt_key, const boost::any& value)
{
    publish_change(opt_key, value);
}

MqttConfigPublisher::~MqttConfigPublisher()
{
    disconnect();

    if (m_mosq) {
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
    }

    if (m_initialized) {
        mosquitto_lib_cleanup();
    }
}

void MqttConfigPublisher::configure(const std::string& broker_host, int broker_port,
                                     const std::string& client_id)
{
    m_broker_host = broker_host;
    m_broker_port = broker_port;
    m_client_id = client_id;
}

bool MqttConfigPublisher::connect()
{
    if (m_connected) return true;

    if (!m_initialized) {
        mosquitto_lib_init();
        m_initialized = true;
    }

    if (!m_mosq) {
        m_mosq = mosquitto_new(m_client_id.c_str(), true, this);
        if (!m_mosq) return false;

        mosquitto_int_option(m_mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);
        mosquitto_connect_callback_set(m_mosq, on_connect_callback);
        mosquitto_disconnect_callback_set(m_mosq, on_disconnect_callback);
    }

    int rc = mosquitto_connect(m_mosq, m_broker_host.c_str(), m_broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        return false;
    }

    rc = mosquitto_loop_start(m_mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_disconnect(m_mosq);
        return false;
    }

    return true;
}

void MqttConfigPublisher::disconnect()
{
    if (m_mosq && m_connected) {
        mosquitto_loop_stop(m_mosq, true);
        mosquitto_disconnect(m_mosq);
        m_connected = false;
    }
}

bool MqttConfigPublisher::is_connected() const
{
    return m_connected;
}

void MqttConfigPublisher::on_connect_callback(struct mosquitto*, void* userdata, int rc)
{
    auto* self = static_cast<MqttConfigPublisher*>(userdata);
    if (rc == 0) {
        self->m_connected = true;
    }
}

void MqttConfigPublisher::on_disconnect_callback(struct mosquitto*, void* userdata, int)
{
    auto* self = static_cast<MqttConfigPublisher*>(userdata);
    self->m_connected = false;
}

std::string MqttConfigPublisher::get_topic(const std::string& opt_key) const
{
    auto it = m_topic_map.find(opt_key);
    if (it != m_topic_map.end()) {
        return "config/" + it->second.page + "/" + it->second.group + "/" + opt_key;
    }
    // Fallback for unknown keys
    return "config/unknown/" + opt_key;
}

std::string MqttConfigPublisher::serialize_value(const std::string& opt_key, const boost::any& value) const
{
    std::ostringstream json;
    json << "{\"key\":\"" << json_escape(opt_key) << "\",";

    // Get option definition for metadata
    const ConfigOptionDef* opt_def = print_config_def.get(opt_key);

    try {
        if (value.type() == typeid(bool)) {
            json << "\"value\":" << (boost::any_cast<bool>(value) ? "true" : "false");
            json << ",\"type\":\"bool\"";
        }
        else if (value.type() == typeid(int)) {
            json << "\"value\":" << boost::any_cast<int>(value);
            json << ",\"type\":\"int\"";
        }
        else if (value.type() == typeid(double)) {
            json << "\"value\":" << std::setprecision(6) << boost::any_cast<double>(value);
            json << ",\"type\":\"float\"";
        }
        else if (value.type() == typeid(std::string)) {
            json << "\"value\":\"" << json_escape(boost::any_cast<std::string>(value)) << "\"";
            json << ",\"type\":\"string\"";
        }
        else if (value.type() == typeid(std::vector<std::string>)) {
            json << "\"value\":[";
            const auto& vec = boost::any_cast<std::vector<std::string>>(value);
            for (size_t i = 0; i < vec.size(); ++i) {
                if (i > 0) json << ",";
                json << "\"" << json_escape(vec[i]) << "\"";
            }
            json << "],\"type\":\"strings\"";
        }
        else if (value.type() == typeid(std::vector<double>)) {
            json << "\"value\":[";
            const auto& vec = boost::any_cast<std::vector<double>>(value);
            for (size_t i = 0; i < vec.size(); ++i) {
                if (i > 0) json << ",";
                json << std::setprecision(6) << vec[i];
            }
            json << "],\"type\":\"floats\"";
        }
        else if (value.type() == typeid(std::vector<int>)) {
            json << "\"value\":[";
            const auto& vec = boost::any_cast<std::vector<int>>(value);
            for (size_t i = 0; i < vec.size(); ++i) {
                if (i > 0) json << ",";
                json << vec[i];
            }
            json << "],\"type\":\"ints\"";
        }
        else {
            json << "\"value\":null,\"type\":\"unknown\"";
        }
    }
    catch (const boost::bad_any_cast&) {
        json << "\"value\":null,\"type\":\"error\"";
    }

    // Add option metadata if available
    if (opt_def) {
        json << ",\"meta\":{";
        json << "\"label\":\"" << json_escape(opt_def->label) << "\"";
        json << ",\"category\":\"" << json_escape(opt_def->category) << "\"";
        json << ",\"tooltip\":\"" << json_escape(opt_def->tooltip) << "\"";

        if (!opt_def->sidetext.empty()) {
            json << ",\"unit\":\"" << json_escape(opt_def->sidetext) << "\"";
        }

        if (opt_def->min != INT_MIN && opt_def->min != -FLT_MAX) {
            json << ",\"min\":" << opt_def->min;
        }
        if (opt_def->max != INT_MAX && opt_def->max != FLT_MAX) {
            json << ",\"max\":" << opt_def->max;
        }

        // Add enum options for enum types
        if (opt_def->enum_keys_map && !opt_def->enum_keys_map->empty()) {
            json << ",\"options\":[";
            bool first = true;
            for (const auto& kv : *opt_def->enum_keys_map) {
                if (!first) json << ",";
                first = false;
                json << "{\"key\":\"" << json_escape(kv.first) << "\",\"value\":" << kv.second << "}";
            }
            json << "]";
        }

        // Add enum values if available
        if (!opt_def->enum_values.empty()) {
            json << ",\"enum_values\":[";
            for (size_t i = 0; i < opt_def->enum_values.size(); ++i) {
                if (i > 0) json << ",";
                json << "\"" << json_escape(opt_def->enum_values[i]) << "\"";
            }
            json << "]";
        }

        // Add enum labels if available
        if (!opt_def->enum_labels.empty()) {
            json << ",\"enum_labels\":[";
            for (size_t i = 0; i < opt_def->enum_labels.size(); ++i) {
                if (i > 0) json << ",";
                json << "\"" << json_escape(opt_def->enum_labels[i]) << "\"";
            }
            json << "]";
        }

        json << "}";
    }

    json << "}";
    return json.str();
}

bool MqttConfigPublisher::publish(const std::string& topic, const std::string& payload, bool retained)
{
    if (!m_mosq || !m_connected) return false;

    std::string full_topic = m_topic_prefix + topic;

    int rc = mosquitto_publish(m_mosq, nullptr,
        full_topic.c_str(),
        static_cast<int>(payload.size()),
        payload.data(),
        1, // QoS 1
        retained);

    return rc == MOSQ_ERR_SUCCESS;
}

void MqttConfigPublisher::publish_change(const std::string& opt_key, const boost::any& value)
{
    if (!m_connected) return;

    std::string topic = get_topic(opt_key);
    std::string payload = serialize_value(opt_key, value);
    publish(topic, payload, true);
}

void MqttConfigPublisher::publish_full_config(const DynamicPrintConfig& config)
{
    if (!m_connected) return;

    for (const auto& pair : m_topic_map) {
        const std::string& opt_key = pair.first;
        const ConfigOption* opt = config.option(opt_key);
        if (!opt) continue;

        boost::any value;

        // Convert ConfigOption to boost::any based on type
        switch (opt->type()) {
            case coBool:
                value = static_cast<const ConfigOptionBool*>(opt)->value;
                break;
            case coInt:
                value = static_cast<const ConfigOptionInt*>(opt)->value;
                break;
            case coFloat:
                value = static_cast<const ConfigOptionFloat*>(opt)->value;
                break;
            case coPercent:
                value = static_cast<const ConfigOptionPercent*>(opt)->value;
                break;
            case coString:
                value = static_cast<const ConfigOptionString*>(opt)->value;
                break;
            case coFloatOrPercent: {
                const auto* fop = static_cast<const ConfigOptionFloatOrPercent*>(opt);
                std::ostringstream ss;
                ss << fop->value;
                if (fop->percent) ss << "%";
                value = ss.str();
                break;
            }
            case coEnum:
                value = opt->getInt();
                break;
            default:
                // Skip complex types for now
                continue;
        }

        std::string topic = get_topic(opt_key);
        std::string payload = serialize_value(opt_key, value);
        publish(topic, payload, true);
    }
}

// Helper to get printer topic path based on wx dialog structure
static std::string get_printer_topic(const std::string& opt_key) {
    // Per-extruder options (handled separately with extruder index)
    static const std::set<std::string> extruder_basic_opts = {
        "extruder_type", "nozzle_diameter", "nozzle_volume", "extruder_printable_height",
        "extruder_printable_area", "default_nozzle_volume_type"
    };
    static const std::set<std::string> extruder_layer_opts = {
        "min_layer_height", "max_layer_height"
    };
    static const std::set<std::string> extruder_retraction_opts = {
        "retraction_length", "z_hop", "retract_lift_above", "retract_lift_below",
        "z_hop_types", "retraction_speed", "deretraction_speed", "retract_restart_extra",
        "retraction_minimum_travel", "retract_when_changing_layer", "wipe", "wipe_distance",
        "retract_before_wipe", "retract_length_toolchange", "retract_restart_extra_toolchange",
        "long_retractions_when_cut", "retraction_distances_when_cut"
    };

    // Basic information - Printable space
    static const std::set<std::string> basic_printable_opts = {
        "printable_area", "bed_exclude_area", "printable_height", "best_object_pos"
    };
    // Basic information - Advanced
    static const std::set<std::string> basic_advanced_opts = {
        "gcode_flavor", "use_relative_e_distances", "use_firmware_retraction",
        "machine_load_filament_time", "machine_unload_filament_time",
        "machine_switch_extruder_time", "machine_hotend_change_time",
        "printer_structure", "scan_first_layer", "thumbnail_size"
    };
    // Basic information - Extruder clearance
    static const std::set<std::string> basic_clearance_opts = {
        "extruder_clearance_max_radius", "extruder_clearance_dist_to_rod",
        "extruder_clearance_height_to_rod", "extruder_clearance_height_to_lid"
    };
    // Basic information - Accessory
    static const std::set<std::string> basic_accessory_opts = {
        "nozzle_type", "auxiliary_fan", "fan_direction", "support_chamber_temp_control",
        "support_air_filtration", "cooling_filter_enabled", "auto_disable_filter_on_overheat"
    };

    // Machine gcode
    static const std::set<std::string> gcode_opts = {
        "machine_start_gcode", "machine_end_gcode", "printing_by_object_gcode",
        "before_layer_change_gcode", "layer_change_gcode", "time_lapse_gcode",
        "wrapping_detection_gcode", "change_filament_gcode", "machine_pause_gcode",
        "template_custom_gcode"
    };

    // Motion ability - Speed
    static const std::set<std::string> motion_speed_opts = {
        "machine_max_speed_x", "machine_max_speed_y", "machine_max_speed_z", "machine_max_speed_e"
    };
    // Motion ability - Acceleration
    static const std::set<std::string> motion_accel_opts = {
        "machine_max_acceleration_x", "machine_max_acceleration_y",
        "machine_max_acceleration_z", "machine_max_acceleration_e",
        "machine_max_acceleration_extruding", "machine_max_acceleration_retracting",
        "machine_max_acceleration_travel"
    };
    // Motion ability - Jerk
    static const std::set<std::string> motion_jerk_opts = {
        "machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z", "machine_max_jerk_e"
    };

    // Map to topic paths
    if (basic_printable_opts.count(opt_key)) return "basic_information/printable_space/" + opt_key;
    if (basic_advanced_opts.count(opt_key)) return "basic_information/advanced/" + opt_key;
    if (basic_clearance_opts.count(opt_key)) return "basic_information/extruder_clearance/" + opt_key;
    if (basic_accessory_opts.count(opt_key)) return "basic_information/accessory/" + opt_key;
    if (gcode_opts.count(opt_key)) return "machine_gcode/" + opt_key;
    if (motion_speed_opts.count(opt_key)) return "motion_ability/speed/" + opt_key;
    if (motion_accel_opts.count(opt_key)) return "motion_ability/acceleration/" + opt_key;
    if (motion_jerk_opts.count(opt_key)) return "motion_ability/jerk/" + opt_key;
    if (opt_key == "printer_notes") return "notes/" + opt_key;

    // Default: put in misc
    return "misc/" + opt_key;
}

// Check if option is per-extruder
static std::string get_extruder_topic_group(const std::string& opt_key) {
    static const std::set<std::string> extruder_basic_opts = {
        "extruder_type", "nozzle_diameter", "nozzle_volume", "extruder_printable_height",
        "extruder_printable_area", "default_nozzle_volume_type", "extruder_offset", "extruder_colour"
    };
    static const std::set<std::string> extruder_layer_opts = {
        "min_layer_height", "max_layer_height"
    };
    static const std::set<std::string> extruder_retraction_opts = {
        "retraction_length", "z_hop", "retract_lift_above", "retract_lift_below",
        "z_hop_types", "retraction_speed", "deretraction_speed", "retract_restart_extra",
        "retraction_minimum_travel", "retract_when_changing_layer", "wipe", "wipe_distance",
        "retract_before_wipe", "retract_length_toolchange", "retract_restart_extra_toolchange",
        "long_retractions_when_cut", "retraction_distances_when_cut"
    };

    if (extruder_basic_opts.count(opt_key)) return "basic_information";
    if (extruder_layer_opts.count(opt_key)) return "layer_height_limits";
    if (extruder_retraction_opts.count(opt_key)) return "retraction";
    return "";
}

void MqttConfigPublisher::publish_printer_config(const DynamicPrintConfig& printer_config, const std::string& preset_name)
{
    if (!m_connected) return;

    // Publish preset name first
    {
        std::ostringstream payload;
        payload << "{\"key\":\"preset_name\",\"value\":\"" << json_escape(preset_name) << "\",\"type\":\"string\"}";
        publish("config/printer/preset_name", payload.str(), true);
    }

    // Get number of extruders from nozzle_diameter
    size_t num_extruders = 1;
    if (auto* nozzle_opt = printer_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")) {
        num_extruders = nozzle_opt->values.size();
    }

    // Iterate over all config options in the printer config
    for (const std::string& opt_key : printer_config.keys()) {
        const ConfigOption* opt = printer_config.option(opt_key);
        if (!opt) continue;

        // Check if this is a per-extruder option (vector type)
        std::string extruder_group = get_extruder_topic_group(opt_key);
        bool is_extruder_opt = !extruder_group.empty();

        // Handle per-extruder options specially
        if (is_extruder_opt && opt->is_vector()) {
            // Publish each extruder's value separately
            for (size_t ext_idx = 0; ext_idx < num_extruders; ++ext_idx) {
                boost::any value;
                bool has_value = false;

                switch (opt->type()) {
                    case coFloats:
                    case coPercents: {
                        const auto* floats = static_cast<const ConfigOptionFloats*>(opt);
                        if (ext_idx < floats->values.size()) {
                            value = floats->values[ext_idx];
                            has_value = true;
                        }
                        break;
                    }
                    case coInts: {
                        const auto* ints = static_cast<const ConfigOptionInts*>(opt);
                        if (ext_idx < ints->values.size()) {
                            value = ints->values[ext_idx];
                            has_value = true;
                        }
                        break;
                    }
                    case coBools: {
                        const auto* bools = static_cast<const ConfigOptionBools*>(opt);
                        if (ext_idx < bools->values.size()) {
                            value = static_cast<bool>(bools->values[ext_idx]);
                            has_value = true;
                        }
                        break;
                    }
                    case coStrings: {
                        const auto* strings = static_cast<const ConfigOptionStrings*>(opt);
                        if (ext_idx < strings->values.size()) {
                            value = strings->values[ext_idx];
                            has_value = true;
                        }
                        break;
                    }
                    case coPoints: {
                        const auto* points = static_cast<const ConfigOptionPoints*>(opt);
                        if (ext_idx < points->values.size()) {
                            std::ostringstream ss;
                            ss << points->values[ext_idx].x() << "," << points->values[ext_idx].y();
                            value = ss.str();
                            has_value = true;
                        }
                        break;
                    }
                    default:
                        break;
                }

                if (has_value) {
                    std::string topic = "config/printer/extruder/" + std::to_string(ext_idx) + "/" + extruder_group + "/" + opt_key;
                    std::string payload = serialize_value(opt_key, value);
                    publish(topic, payload, true);
                }
            }
        } else {
            // Non-extruder options - publish normally
            boost::any value;
            bool has_value = true;

            switch (opt->type()) {
                case coBool:
                    value = static_cast<const ConfigOptionBool*>(opt)->value;
                    break;
                case coInt:
                    value = static_cast<const ConfigOptionInt*>(opt)->value;
                    break;
                case coFloat:
                    value = static_cast<const ConfigOptionFloat*>(opt)->value;
                    break;
                case coPercent:
                    value = static_cast<const ConfigOptionPercent*>(opt)->value;
                    break;
                case coString:
                    value = static_cast<const ConfigOptionString*>(opt)->value;
                    break;
                case coFloatOrPercent: {
                    const auto* fop = static_cast<const ConfigOptionFloatOrPercent*>(opt);
                    std::ostringstream ss;
                    ss << fop->value;
                    if (fop->percent) ss << "%";
                    value = ss.str();
                    break;
                }
                case coEnum:
                    value = opt->getInt();
                    break;
                case coFloats: {
                    const auto* floats = static_cast<const ConfigOptionFloats*>(opt);
                    value = floats->values;
                    break;
                }
                case coInts: {
                    const auto* ints = static_cast<const ConfigOptionInts*>(opt);
                    value = ints->values;
                    break;
                }
                case coStrings: {
                    const auto* strings = static_cast<const ConfigOptionStrings*>(opt);
                    value = strings->values;
                    break;
                }
                default:
                    has_value = false;
                    break;
            }

            if (has_value) {
                std::string topic = "config/printer/" + get_printer_topic(opt_key);
                std::string payload = serialize_value(opt_key, value);
                publish(topic, payload, true);
            }
        }
    }
}

void MqttConfigPublisher::publish_filament_config(const DynamicPrintConfig& filament_config, const std::string& preset_name, int extruder_idx)
{
    if (!m_connected) return;

    std::string extruder_prefix = "config/filament/" + std::to_string(extruder_idx) + "/";

    // Publish preset name first
    {
        std::ostringstream payload;
        payload << "{\"key\":\"preset_name\",\"value\":\"" << json_escape(preset_name) << "\",\"type\":\"string\",\"extruder\":" << extruder_idx << "}";
        publish(extruder_prefix + "preset_name", payload.str(), true);
    }

    // Iterate over all config options in the filament config
    for (const std::string& opt_key : filament_config.keys()) {
        const ConfigOption* opt = filament_config.option(opt_key);
        if (!opt) continue;

        boost::any value;

        // Convert ConfigOption to boost::any based on type
        switch (opt->type()) {
            case coBool:
                value = static_cast<const ConfigOptionBool*>(opt)->value;
                break;
            case coInt:
                value = static_cast<const ConfigOptionInt*>(opt)->value;
                break;
            case coFloat:
                value = static_cast<const ConfigOptionFloat*>(opt)->value;
                break;
            case coPercent:
                value = static_cast<const ConfigOptionPercent*>(opt)->value;
                break;
            case coString:
                value = static_cast<const ConfigOptionString*>(opt)->value;
                break;
            case coFloatOrPercent: {
                const auto* fop = static_cast<const ConfigOptionFloatOrPercent*>(opt);
                std::ostringstream ss;
                ss << fop->value;
                if (fop->percent) ss << "%";
                value = ss.str();
                break;
            }
            case coEnum:
                value = opt->getInt();
                break;
            case coFloats: {
                const auto* floats = static_cast<const ConfigOptionFloats*>(opt);
                value = floats->values;
                break;
            }
            case coInts: {
                const auto* ints = static_cast<const ConfigOptionInts*>(opt);
                value = ints->values;
                break;
            }
            case coStrings: {
                const auto* strings = static_cast<const ConfigOptionStrings*>(opt);
                value = strings->values;
                break;
            }
            default:
                // Skip complex types
                continue;
        }

        std::string topic = extruder_prefix + opt_key;
        std::string payload = serialize_value(opt_key, value);
        publish(topic, payload, true);
    }
}

void MqttConfigPublisher::init_topic_map()
{
    using namespace ConfigTopics;

    // Quality - Layer height
    m_topic_map["layer_height"] = {Quality::PAGE, Quality::LayerHeight::GROUP};
    m_topic_map["initial_layer_print_height"] = {Quality::PAGE, Quality::LayerHeight::GROUP};

    // Quality - Line width
    m_topic_map["line_width"] = {Quality::PAGE, Quality::LineWidth::GROUP};
    m_topic_map["initial_layer_line_width"] = {Quality::PAGE, Quality::LineWidth::GROUP};
    m_topic_map["outer_wall_line_width"] = {Quality::PAGE, Quality::LineWidth::GROUP};
    m_topic_map["inner_wall_line_width"] = {Quality::PAGE, Quality::LineWidth::GROUP};
    m_topic_map["top_surface_line_width"] = {Quality::PAGE, Quality::LineWidth::GROUP};
    m_topic_map["sparse_infill_line_width"] = {Quality::PAGE, Quality::LineWidth::GROUP};
    m_topic_map["internal_solid_infill_line_width"] = {Quality::PAGE, Quality::LineWidth::GROUP};
    m_topic_map["support_line_width"] = {Quality::PAGE, Quality::LineWidth::GROUP};

    // Quality - Seam
    m_topic_map["seam_position"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_placement_away_from_overhangs"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_gap"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_slope_conditional"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["scarf_angle_threshold"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_slope_entire_loop"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_slope_steps"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_slope_inner_walls"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["override_filament_scarf_seam_setting"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_slope_type"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_slope_start_height"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_slope_gap"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["seam_slope_min_length"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["wipe_speed"] = {Quality::PAGE, Quality::Seam::GROUP};
    m_topic_map["role_base_wipe_speed"] = {Quality::PAGE, Quality::Seam::GROUP};

    // Quality - Precision
    m_topic_map["slice_closing_radius"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["resolution"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["enable_arc_fitting"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["xy_hole_compensation"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["xy_contour_compensation"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["enable_circle_compensation"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["circle_compensation_manual_offset"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["elefant_foot_compensation"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["precise_outer_wall"] = {Quality::PAGE, Quality::Precision::GROUP};
    m_topic_map["precise_z_height"] = {Quality::PAGE, Quality::Precision::GROUP};

    // Quality - Ironing
    m_topic_map["ironing_type"] = {Quality::PAGE, Quality::Ironing::GROUP};
    m_topic_map["ironing_pattern"] = {Quality::PAGE, Quality::Ironing::GROUP};
    m_topic_map["ironing_speed"] = {Quality::PAGE, Quality::Ironing::GROUP};
    m_topic_map["ironing_flow"] = {Quality::PAGE, Quality::Ironing::GROUP};
    m_topic_map["ironing_spacing"] = {Quality::PAGE, Quality::Ironing::GROUP};
    m_topic_map["ironing_inset"] = {Quality::PAGE, Quality::Ironing::GROUP};
    m_topic_map["ironing_direction"] = {Quality::PAGE, Quality::Ironing::GROUP};

    // Quality - Wall generator
    m_topic_map["wall_generator"] = {Quality::PAGE, Quality::WallGenerator::GROUP};
    m_topic_map["wall_transition_angle"] = {Quality::PAGE, Quality::WallGenerator::GROUP};
    m_topic_map["wall_transition_filter_deviation"] = {Quality::PAGE, Quality::WallGenerator::GROUP};
    m_topic_map["wall_transition_length"] = {Quality::PAGE, Quality::WallGenerator::GROUP};
    m_topic_map["wall_distribution_count"] = {Quality::PAGE, Quality::WallGenerator::GROUP};
    m_topic_map["min_bead_width"] = {Quality::PAGE, Quality::WallGenerator::GROUP};
    m_topic_map["min_feature_size"] = {Quality::PAGE, Quality::WallGenerator::GROUP};

    // Quality - Advanced
    m_topic_map["wall_sequence"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["is_infill_first"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["bridge_flow"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["thick_bridges"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["print_flow_ratio"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["top_solid_infill_flow_ratio"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["initial_layer_flow_ratio"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["top_one_wall_type"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["top_area_threshold"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["only_one_wall_first_layer"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["detect_overhang_wall"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["smooth_speed_discontinuity_area"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["smooth_coefficient"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["reduce_crossing_wall"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["max_travel_detour_distance"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["avoid_crossing_wall_includes_support"] = {Quality::PAGE, Quality::Advanced::GROUP};
    m_topic_map["z_direction_outwall_speed_continuous"] = {Quality::PAGE, Quality::Advanced::GROUP};

    // Strength - Walls
    m_topic_map["wall_loops"] = {Strength::PAGE, Strength::Walls::GROUP};
    m_topic_map["embedding_wall_into_infill"] = {Strength::PAGE, Strength::Walls::GROUP};
    m_topic_map["detect_thin_wall"] = {Strength::PAGE, Strength::Walls::GROUP};

    // Strength - Top/bottom shells
    m_topic_map["interface_shells"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["top_surface_pattern"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["top_shell_layers"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["top_shell_thickness"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["top_color_penetration_layers"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["bottom_surface_pattern"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["bottom_shell_layers"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["bottom_shell_thickness"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["bottom_color_penetration_layers"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["infill_instead_top_bottom_surfaces"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};
    m_topic_map["internal_solid_infill_pattern"] = {Strength::PAGE, Strength::TopBottomShells::GROUP};

    // Strength - Sparse infill
    m_topic_map["sparse_infill_density"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["fill_multiline"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["sparse_infill_pattern"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["locked_skin_infill_pattern"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["skin_infill_density"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["locked_skeleton_infill_pattern"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["skeleton_infill_density"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["infill_lock_depth"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["skin_infill_depth"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["skin_infill_line_width"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["skeleton_infill_line_width"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["symmetric_infill_y_axis"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["infill_shift_step"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["infill_rotate_step"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["sparse_infill_anchor"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["sparse_infill_anchor_max"] = {Strength::PAGE, Strength::SparseInfill::GROUP};
    m_topic_map["filter_out_gap_fill"] = {Strength::PAGE, Strength::SparseInfill::GROUP};

    // Strength - Advanced
    m_topic_map["infill_wall_overlap"] = {Strength::PAGE, Strength::Advanced::GROUP};
    m_topic_map["infill_direction"] = {Strength::PAGE, Strength::Advanced::GROUP};
    m_topic_map["bridge_angle"] = {Strength::PAGE, Strength::Advanced::GROUP};
    m_topic_map["minimum_sparse_infill_area"] = {Strength::PAGE, Strength::Advanced::GROUP};
    m_topic_map["infill_combination"] = {Strength::PAGE, Strength::Advanced::GROUP};
    m_topic_map["detect_narrow_internal_solid_infill"] = {Strength::PAGE, Strength::Advanced::GROUP};
    m_topic_map["ensure_vertical_shell_thickness"] = {Strength::PAGE, Strength::Advanced::GROUP};
    m_topic_map["detect_floating_vertical_shell"] = {Strength::PAGE, Strength::Advanced::GROUP};

    // Speed - Initial layer
    m_topic_map["initial_layer_speed"] = {Speed::PAGE, Speed::InitialLayer::GROUP};
    m_topic_map["initial_layer_infill_speed"] = {Speed::PAGE, Speed::InitialLayer::GROUP};

    // Speed - Other layers
    m_topic_map["outer_wall_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["inner_wall_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["small_perimeter_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["small_perimeter_threshold"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["sparse_infill_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["internal_solid_infill_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["vertical_shell_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["top_surface_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["enable_overhang_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["overhang_1_4_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["overhang_2_4_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["overhang_3_4_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["overhang_4_4_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["overhang_totally_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["enable_height_slowdown"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["slowdown_start_height"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["slowdown_start_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["slowdown_start_acc"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["slowdown_end_height"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["slowdown_end_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["slowdown_end_acc"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["bridge_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["gap_infill_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["support_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};
    m_topic_map["support_interface_speed"] = {Speed::PAGE, Speed::OtherLayers::GROUP};

    // Speed - Travel
    m_topic_map["travel_speed"] = {Speed::PAGE, Speed::Travel::GROUP};

    // Speed - Acceleration
    m_topic_map["default_acceleration"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["travel_acceleration"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["initial_layer_travel_acceleration"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["initial_layer_acceleration"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["outer_wall_acceleration"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["inner_wall_acceleration"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["top_surface_acceleration"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["sparse_infill_acceleration"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["accel_to_decel_enable"] = {Speed::PAGE, Speed::Acceleration::GROUP};
    m_topic_map["accel_to_decel_factor"] = {Speed::PAGE, Speed::Acceleration::GROUP};

    // Speed - Jerk
    m_topic_map["default_jerk"] = {Speed::PAGE, Speed::Jerk::GROUP};
    m_topic_map["outer_wall_jerk"] = {Speed::PAGE, Speed::Jerk::GROUP};
    m_topic_map["inner_wall_jerk"] = {Speed::PAGE, Speed::Jerk::GROUP};
    m_topic_map["infill_jerk"] = {Speed::PAGE, Speed::Jerk::GROUP};
    m_topic_map["top_surface_jerk"] = {Speed::PAGE, Speed::Jerk::GROUP};
    m_topic_map["initial_layer_jerk"] = {Speed::PAGE, Speed::Jerk::GROUP};
    m_topic_map["travel_jerk"] = {Speed::PAGE, Speed::Jerk::GROUP};

    // Support - General
    m_topic_map["enable_support"] = {Support::PAGE, Support::General::GROUP};
    m_topic_map["support_type"] = {Support::PAGE, Support::General::GROUP};
    m_topic_map["support_style"] = {Support::PAGE, Support::General::GROUP};
    m_topic_map["support_threshold_angle"] = {Support::PAGE, Support::General::GROUP};
    m_topic_map["support_on_build_plate_only"] = {Support::PAGE, Support::General::GROUP};
    m_topic_map["support_critical_regions_only"] = {Support::PAGE, Support::General::GROUP};
    m_topic_map["support_remove_small_overhang"] = {Support::PAGE, Support::General::GROUP};

    // Support - Raft
    m_topic_map["raft_layers"] = {Support::PAGE, Support::Raft::GROUP};
    m_topic_map["raft_contact_distance"] = {Support::PAGE, Support::Raft::GROUP};

    // Support - Filament
    m_topic_map["support_filament"] = {Support::PAGE, Support::Filament::GROUP};
    m_topic_map["support_interface_filament"] = {Support::PAGE, Support::Filament::GROUP};
    m_topic_map["support_interface_not_for_body"] = {Support::PAGE, Support::Filament::GROUP};

    // Support - Advanced
    m_topic_map["raft_first_layer_density"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["raft_first_layer_expansion"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["tree_support_wall_count"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_top_z_distance"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_bottom_z_distance"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_base_pattern"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_base_pattern_spacing"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_angle"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_interface_top_layers"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_interface_bottom_layers"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_interface_pattern"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_interface_spacing"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_bottom_interface_spacing"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_expansion"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_object_xy_distance"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["top_z_overrides_xy_distance"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["support_object_first_layer_gap"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["bridge_no_support"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["max_bridge_length"] = {Support::PAGE, Support::Advanced::GROUP};
    m_topic_map["independent_support_layer_height"] = {Support::PAGE, Support::Advanced::GROUP};

    // Support - Tree support
    m_topic_map["tree_support_branch_distance"] = {Support::PAGE, Support::TreeSupport::GROUP};
    m_topic_map["tree_support_branch_diameter"] = {Support::PAGE, Support::TreeSupport::GROUP};
    m_topic_map["tree_support_branch_angle"] = {Support::PAGE, Support::TreeSupport::GROUP};
    m_topic_map["tree_support_branch_diameter_angle"] = {Support::PAGE, Support::TreeSupport::GROUP};

    // Others - Bed adhesion
    m_topic_map["skirt_loops"] = {Others::PAGE, Others::BedAdhesion::GROUP};
    m_topic_map["skirt_height"] = {Others::PAGE, Others::BedAdhesion::GROUP};
    m_topic_map["skirt_distance"] = {Others::PAGE, Others::BedAdhesion::GROUP};
    m_topic_map["brim_type"] = {Others::PAGE, Others::BedAdhesion::GROUP};
    m_topic_map["brim_width"] = {Others::PAGE, Others::BedAdhesion::GROUP};
    m_topic_map["brim_object_gap"] = {Others::PAGE, Others::BedAdhesion::GROUP};

    // Others - Prime tower
    m_topic_map["enable_prime_tower"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_skip_points"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_enable_framework"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_width"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_max_speed"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_brim_width"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_infill_gap"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_rib_wall"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_extra_rib_length"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_rib_width"] = {Others::PAGE, Others::PrimeTower::GROUP};
    m_topic_map["prime_tower_fillet_wall"] = {Others::PAGE, Others::PrimeTower::GROUP};

    // Others - Flush options
    m_topic_map["flush_into_infill"] = {Others::PAGE, Others::FlushOptions::GROUP};
    m_topic_map["flush_into_objects"] = {Others::PAGE, Others::FlushOptions::GROUP};
    m_topic_map["flush_into_support"] = {Others::PAGE, Others::FlushOptions::GROUP};

    // Others - Special mode
    m_topic_map["slicing_mode"] = {Others::PAGE, Others::SpecialMode::GROUP};
    m_topic_map["print_sequence"] = {Others::PAGE, Others::SpecialMode::GROUP};
    m_topic_map["spiral_mode"] = {Others::PAGE, Others::SpecialMode::GROUP};
    m_topic_map["spiral_mode_smooth"] = {Others::PAGE, Others::SpecialMode::GROUP};
    m_topic_map["spiral_mode_max_xy_smoothing"] = {Others::PAGE, Others::SpecialMode::GROUP};
    m_topic_map["timelapse_type"] = {Others::PAGE, Others::SpecialMode::GROUP};
    m_topic_map["fuzzy_skin"] = {Others::PAGE, Others::SpecialMode::GROUP};
    m_topic_map["fuzzy_skin_point_distance"] = {Others::PAGE, Others::SpecialMode::GROUP};
    m_topic_map["fuzzy_skin_thickness"] = {Others::PAGE, Others::SpecialMode::GROUP};

    // Others - Advanced
    m_topic_map["enable_wrapping_detection"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["interlocking_beam"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["mmu_segmented_region_interlocking_depth"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["interlocking_beam_width"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["interlocking_orientation"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["interlocking_beam_layer_count"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["interlocking_depth"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["interlocking_boundary_avoidance"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["sparse_infill_filament"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["solid_infill_filament"] = {Others::PAGE, Others::Advanced::GROUP};
    m_topic_map["wall_filament"] = {Others::PAGE, Others::Advanced::GROUP};

    // Others - G-code output
    m_topic_map["reduce_infill_retraction"] = {Others::PAGE, Others::GcodeOutput::GROUP};
    m_topic_map["gcode_add_line_number"] = {Others::PAGE, Others::GcodeOutput::GROUP};
    m_topic_map["exclude_object"] = {Others::PAGE, Others::GcodeOutput::GROUP};
    m_topic_map["filename_format"] = {Others::PAGE, Others::GcodeOutput::GROUP};
    m_topic_map["post_process"] = {Others::PAGE, Others::GcodeOutput::GROUP};
    m_topic_map["process_notes"] = {Others::PAGE, Others::GcodeOutput::GROUP};
}

} // namespace Slic3r
