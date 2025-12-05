#include "Studio.hpp"
#include "libslic3r/PresetBundle.hpp"
#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace GUI {

Studio* Studio::s_instance = nullptr;

Studio& Studio::instance()
{
    assert(s_instance != nullptr && "Studio not initialized! Call Studio::create() first.");
    return *s_instance;
}

void Studio::create()
{
    if (s_instance != nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "Studio::create() called but instance already exists";
        return;
    }

    s_instance = new Studio();
    s_instance->init();
    BOOST_LOG_TRIVIAL(info) << "Studio singleton created";
}

void Studio::destroy()
{
    if (s_instance == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "Studio::destroy() called but no instance exists";
        return;
    }

    s_instance->shutdown();
    delete s_instance;
    s_instance = nullptr;
    BOOST_LOG_TRIVIAL(info) << "Studio singleton destroyed";
}

Studio::Studio()
{
}

Studio::~Studio()
{
}

void Studio::init()
{
    BOOST_LOG_TRIVIAL(info) << "Studio initializing services...";

#if SLIC3R_HAS_MOSQUITTO
    // MQTT will be initialized later via init_mqtt() when network is ready
    m_mqtt_publisher = MqttConfigPublisher::create();
#endif
}

void Studio::shutdown()
{
    BOOST_LOG_TRIVIAL(info) << "Studio shutting down services...";

    m_preset_bundle = nullptr;

#if SLIC3R_HAS_MOSQUITTO
    if (m_mqtt_publisher) {
        m_mqtt_publisher->disconnect();
        m_mqtt_publisher.reset();
    }
#endif
}

#if SLIC3R_HAS_MOSQUITTO
bool Studio::init_mqtt(const std::string& broker_host, int broker_port, const std::string& client_id)
{
    if (!m_mqtt_publisher) {
        BOOST_LOG_TRIVIAL(error) << "MQTT publisher not created";
        return false;
    }

    m_mqtt_publisher->configure(broker_host, broker_port, client_id);

    if (m_mqtt_publisher->connect()) {
        m_mqtt_publisher->register_with_dispatcher();
        BOOST_LOG_TRIVIAL(info) << "MQTT config publisher connected to " << broker_host << ":" << broker_port;
        return true;
    } else {
        BOOST_LOG_TRIVIAL(warning) << "MQTT config publisher failed to connect to " << broker_host << ":" << broker_port;
        return false;
    }
}

void Studio::publish_full_config()
{
    if (!m_mqtt_publisher) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot publish full config: MQTT publisher not initialized";
        return;
    }

    if (!m_mqtt_publisher->is_connected()) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot publish full config: MQTT not connected";
        return;
    }

    if (!m_preset_bundle) {
        BOOST_LOG_TRIVIAL(warning) << "Cannot publish full config: preset_bundle not set";
        return;
    }

    try {
        DynamicPrintConfig config = m_preset_bundle->full_config();
        m_mqtt_publisher->publish_full_config(config);
        BOOST_LOG_TRIVIAL(info) << "Published full config to MQTT (" << config.keys().size() << " keys)";
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to publish full config: " << e.what();
    }
}
#endif

} // namespace GUI
} // namespace Slic3r
