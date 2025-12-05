#ifndef slic3r_GUI_Studio_hpp_
#define slic3r_GUI_Studio_hpp_

#include <memory>
#include <atomic>

#if SLIC3R_HAS_MOSQUITTO
#include "slic3r/Network/MqttConfigPublisher.hpp"
#endif

namespace Slic3r {

class PresetBundle;

namespace GUI {

// Studio singleton - central application services
// Created alongside GUI_App, provides access to:
// - PresetBundle reference
// - MQTT config publisher
// - Future: other application-wide services
//
// Usage:
//   Studio::instance().preset_bundle()
//   Studio::instance().mqtt_publisher()->publish_change(...)
//   Studio::instance().publish_full_config()
//
class Studio {
public:
    // Get the singleton instance
    static Studio& instance();

    // Initialize the studio (call once at startup)
    static void create();

    // Shutdown the studio (call at exit)
    static void destroy();

    // Check if initialized
    static bool is_initialized() { return s_instance != nullptr; }

    // PresetBundle access
    void set_preset_bundle(PresetBundle* bundle) { m_preset_bundle = bundle; }
    PresetBundle* preset_bundle() { return m_preset_bundle; }
    const PresetBundle* preset_bundle() const { return m_preset_bundle; }

#if SLIC3R_HAS_MOSQUITTO
    // MQTT config publisher
    std::shared_ptr<MqttConfigPublisher> mqtt_publisher() { return m_mqtt_publisher; }

    // Initialize MQTT connection
    bool init_mqtt(const std::string& broker_host = "localhost",
                   int broker_port = 1883,
                   const std::string& client_id = "corvusprint-config");

    // Publish full config from preset_bundle to MQTT
    // Call this after presets are fully loaded
    void publish_full_config();
#endif

private:
    Studio();
    ~Studio();

    // Non-copyable
    Studio(const Studio&) = delete;
    Studio& operator=(const Studio&) = delete;

    // Initialize services
    void init();

    // Shutdown services
    void shutdown();

    PresetBundle* m_preset_bundle{nullptr};

#if SLIC3R_HAS_MOSQUITTO
    std::shared_ptr<MqttConfigPublisher> m_mqtt_publisher;
#endif

    static Studio* s_instance;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Studio_hpp_
