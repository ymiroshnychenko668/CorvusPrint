#include "MqttEventSink.hpp"
#include <mosquitto.h>
#include <sstream>
#include <cstring>

// Simple JSON helpers (avoid external dependency)
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

MqttEventSink::MqttEventSink(const MqttConfig& config)
    : m_config(config)
{
    // Initialize mosquitto library (safe to call multiple times)
    mosquitto_lib_init();
    m_initialized = true;

    // Create mosquitto client instance
    m_mosq = mosquitto_new(m_config.client_id.c_str(), true, this);
    if (m_mosq) {
        // Enable MQTT v5 protocol
        mosquitto_int_option(m_mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);

        // Set callbacks
        mosquitto_connect_callback_set(m_mosq, on_connect_callback);
        mosquitto_disconnect_callback_set(m_mosq, on_disconnect_callback);

        // Set credentials if provided
        if (!m_config.username.empty()) {
            mosquitto_username_pw_set(m_mosq,
                m_config.username.c_str(),
                m_config.password.empty() ? nullptr : m_config.password.c_str());
        }
    }
}

MqttEventSink::~MqttEventSink()
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

bool MqttEventSink::connect()
{
    if (!m_mosq) return false;
    if (m_connected) return true;

    int rc = mosquitto_connect(m_mosq,
        m_config.broker_host.c_str(),
        m_config.broker_port,
        m_config.keepalive);

    if (rc != MOSQ_ERR_SUCCESS) {
        return false;
    }

    // Start the network loop in a background thread
    rc = mosquitto_loop_start(m_mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_disconnect(m_mosq);
        return false;
    }

    return true;
}

void MqttEventSink::disconnect()
{
    if (m_mosq && m_connected) {
        mosquitto_loop_stop(m_mosq, true);
        mosquitto_disconnect(m_mosq);
        m_connected = false;
    }
}

bool MqttEventSink::is_connected() const
{
    return m_connected;
}

void MqttEventSink::on_connect_callback(struct mosquitto* /*mosq*/, void* userdata, int rc)
{
    auto* self = static_cast<MqttEventSink*>(userdata);
    if (rc == 0) {
        self->m_connected = true;
    }
}

void MqttEventSink::on_disconnect_callback(struct mosquitto* /*mosq*/, void* userdata, int /*rc*/)
{
    auto* self = static_cast<MqttEventSink*>(userdata);
    self->m_connected = false;
}

bool MqttEventSink::publish(const std::string& topic, const std::string& payload, bool retained)
{
    if (!m_mosq || !m_connected) return false;

    std::string full_topic = m_config.topic_prefix + topic;

    int rc = mosquitto_publish(m_mosq, nullptr,
        full_topic.c_str(),
        static_cast<int>(payload.size()),
        payload.data(),
        m_config.qos,
        retained);

    return rc == MOSQ_ERR_SUCCESS;
}

std::string MqttEventSink::serialize_status(const PrintBase::SlicingStatus& status) const
{
    std::ostringstream json;
    json << "{";
    json << "\"percent\":" << status.percent << ",";
    json << "\"message\":\"" << json_escape(status.text) << "\",";
    json << "\"flags\":" << status.flags << ",";
    json << "\"warning_step\":" << status.warning_step << ",";
    json << "\"is_helio\":" << (status.is_helio ? "true" : "false");
    json << "}";
    return json.str();
}

std::string MqttEventSink::serialize_completed(const SlicingCompletedInfo& info) const
{
    std::ostringstream json;
    json << "{";
    json << "\"status\":";
    switch (info.status) {
        case SlicingCompletedInfo::Finished:  json << "\"finished\""; break;
        case SlicingCompletedInfo::Cancelled: json << "\"cancelled\""; break;
        case SlicingCompletedInfo::Error:     json << "\"error\""; break;
    }
    json << ",";
    json << "\"error_message\":\"" << json_escape(info.error_message) << "\",";
    json << "\"critical_error\":" << (info.critical_error ? "true" : "false") << ",";
    json << "\"invalidate_plater\":" << (info.invalidate_plater ? "true" : "false") << ",";
    json << "\"error_object_ids\":[";
    for (size_t i = 0; i < info.error_object_ids.size(); ++i) {
        if (i > 0) json << ",";
        json << info.error_object_ids[i];
    }
    json << "]";
    json << "}";
    return json.str();
}

void MqttEventSink::on_slicing_update(const PrintBase::SlicingStatus& status)
{
    publish("status", serialize_status(status));
}

void MqttEventSink::on_slicing_completed(int timestamp)
{
    std::ostringstream json;
    json << "{\"timestamp\":" << timestamp << "}";
    publish("slicing_completed", json.str());
}

void MqttEventSink::on_process_finished(const SlicingCompletedInfo& info)
{
    // Publish as retained so new subscribers get the last state
    publish("finished", serialize_completed(info), true);
}

void MqttEventSink::on_export_began()
{
    publish("export/began", "{\"phase\":\"began\"}");
}

void MqttEventSink::on_export_finished(const std::string& path)
{
    std::ostringstream json;
    json << "{\"phase\":\"finished\",\"path\":\"" << json_escape(path) << "\"}";
    publish("export/finished", json.str());
}

} // namespace Slic3r
