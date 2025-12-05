#ifndef slic3r_Network_MqttEventSink_hpp_
#define slic3r_Network_MqttEventSink_hpp_

#include "libslic3r/SlicingEvents.hpp"
#include <string>
#include <memory>
#include <atomic>

// Forward declaration for mosquitto
struct mosquitto;

namespace Slic3r {

// Configuration for MQTT connection
struct MqttConfig {
    std::string broker_host{"localhost"};
    int         broker_port{1883};
    std::string client_id{"corvusprint-slicer"};
    std::string topic_prefix{"slicer/"};
    std::string username;
    std::string password;
    bool        use_tls{false};
    int         keepalive{60};
    int         qos{1};  // Quality of Service: 0, 1, or 2
};

// MQTT event sink for publishing slicing events to an MQTT broker
// Uses libmosquitto with MQTT 5.0 protocol
//
// Topics published:
//   {prefix}status           - SlicingStatus updates (frequent)
//   {prefix}slicing_completed - Slicing phase done
//   {prefix}finished         - All processing complete (retained)
//   {prefix}export/began     - Export started
//   {prefix}export/finished  - Export completed with path
//
// Usage:
//   MqttConfig config;
//   config.broker_host = "192.168.1.100";
//   auto mqtt_sink = std::make_shared<MqttEventSink>(config);
//   if (mqtt_sink->connect()) {
//       dispatcher->add_sink(mqtt_sink);
//   }
//
class MqttEventSink : public ISlicingEventSink {
public:
    explicit MqttEventSink(const MqttConfig& config);
    ~MqttEventSink() override;

    // Non-copyable
    MqttEventSink(const MqttEventSink&) = delete;
    MqttEventSink& operator=(const MqttEventSink&) = delete;

    // Connect to MQTT broker
    // Returns true on success
    bool connect();

    // Disconnect from MQTT broker
    void disconnect();

    // Check if connected
    bool is_connected() const;

    // ISlicingEventSink implementation
    void on_slicing_update(const PrintBase::SlicingStatus& status) override;
    void on_slicing_completed(int timestamp) override;
    void on_process_finished(const SlicingCompletedInfo& info) override;
    void on_export_began() override;
    void on_export_finished(const std::string& path) override;

private:
    // Serialize status to JSON
    std::string serialize_status(const PrintBase::SlicingStatus& status) const;
    std::string serialize_completed(const SlicingCompletedInfo& info) const;

    // Publish message to topic
    bool publish(const std::string& topic, const std::string& payload, bool retained = false);

    // MQTT callbacks
    static void on_connect_callback(struct mosquitto* mosq, void* userdata, int rc);
    static void on_disconnect_callback(struct mosquitto* mosq, void* userdata, int rc);

    MqttConfig          m_config;
    struct mosquitto*   m_mosq{nullptr};
    std::atomic<bool>   m_connected{false};
    bool                m_initialized{false};
};

} // namespace Slic3r

#endif // slic3r_Network_MqttEventSink_hpp_
