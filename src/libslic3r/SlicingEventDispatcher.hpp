#ifndef slic3r_SlicingEventDispatcher_hpp_
#define slic3r_SlicingEventDispatcher_hpp_

#include "SlicingEvents.hpp"
#include <vector>
#include <mutex>
#include <algorithm>

namespace Slic3r {

// Dispatches slicing events to multiple sinks simultaneously
// Thread-safe: can be called from background slicing thread
//
// Usage:
//   auto dispatcher = std::make_shared<SlicingEventDispatcher>();
//   dispatcher->add_sink(wx_sink);
//   dispatcher->add_sink(mqtt_sink);
//   background_process.set_event_sink(dispatcher);
//
class SlicingEventDispatcher : public ISlicingEventSink {
public:
    SlicingEventDispatcher() = default;
    ~SlicingEventDispatcher() override = default;

    // Add a sink to receive events
    // Thread-safe
    void add_sink(SlicingEventSinkPtr sink) {
        if (!sink) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sinks.push_back(sink);
    }

    // Remove a sink
    // Thread-safe
    void remove_sink(SlicingEventSinkPtr sink) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sinks.erase(
            std::remove(m_sinks.begin(), m_sinks.end(), sink),
            m_sinks.end()
        );
    }

    // Remove all sinks
    // Thread-safe
    void clear_sinks() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sinks.clear();
    }

    // Get number of registered sinks
    size_t sink_count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sinks.size();
    }

    // ISlicingEventSink implementation
    // All methods are thread-safe

    void on_slicing_update(const PrintBase::SlicingStatus& status) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& sink : m_sinks) {
            if (sink) sink->on_slicing_update(status);
        }
    }

    void on_slicing_completed(int timestamp) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& sink : m_sinks) {
            if (sink) sink->on_slicing_completed(timestamp);
        }
    }

    void on_process_finished(const SlicingCompletedInfo& info) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& sink : m_sinks) {
            if (sink) sink->on_process_finished(info);
        }
    }

    void on_export_began() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& sink : m_sinks) {
            if (sink) sink->on_export_began();
        }
    }

    void on_export_finished(const std::string& path) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& sink : m_sinks) {
            if (sink) sink->on_export_finished(path);
        }
    }

private:
    mutable std::mutex m_mutex;
    std::vector<SlicingEventSinkPtr> m_sinks;
};

} // namespace Slic3r

#endif // slic3r_SlicingEventDispatcher_hpp_
