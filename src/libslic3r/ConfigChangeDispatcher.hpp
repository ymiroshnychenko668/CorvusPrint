#ifndef slic3r_ConfigChangeDispatcher_hpp_
#define slic3r_ConfigChangeDispatcher_hpp_

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <boost/any.hpp>

namespace Slic3r {

// Interface for config change listeners
class IConfigChangeListener {
public:
    virtual ~IConfigChangeListener() = default;

    // Called when a config value changes
    // opt_key: the config option key (e.g., "layer_height")
    // value: the new value as boost::any
    virtual void on_config_change(const std::string& opt_key, const boost::any& value) = 0;
};

// Dispatcher for config change events
// Singleton pattern - use ConfigChangeDispatcher::instance()
//
// Usage:
//   // Add a listener
//   auto listener = std::make_shared<MyListener>();
//   ConfigChangeDispatcher::instance().add_listener(listener);
//
//   // Notify listeners (call this when config changes)
//   ConfigChangeDispatcher::instance().notify(opt_key, value);
//
class ConfigChangeDispatcher {
public:
    static ConfigChangeDispatcher& instance() {
        static ConfigChangeDispatcher inst;
        return inst;
    }

    // Add a listener (weak reference - won't prevent deletion)
    void add_listener(std::weak_ptr<IConfigChangeListener> listener) {
        m_listeners.push_back(listener);
    }

    // Add a callback function
    void add_callback(std::function<void(const std::string&, const boost::any&)> callback) {
        m_callbacks.push_back(callback);
    }

    // Notify all listeners of a config change
    void notify(const std::string& opt_key, const boost::any& value) {
        // Clean up expired weak pointers and notify active listeners
        auto it = m_listeners.begin();
        while (it != m_listeners.end()) {
            if (auto listener = it->lock()) {
                listener->on_config_change(opt_key, value);
                ++it;
            } else {
                it = m_listeners.erase(it);
            }
        }

        // Call registered callbacks
        for (auto& callback : m_callbacks) {
            if (callback) {
                callback(opt_key, value);
            }
        }
    }

    // Clear all listeners
    void clear() {
        m_listeners.clear();
        m_callbacks.clear();
    }

    // Enable/disable dispatching
    void set_enabled(bool enabled) { m_enabled = enabled; }
    bool is_enabled() const { return m_enabled; }

private:
    ConfigChangeDispatcher() = default;
    ~ConfigChangeDispatcher() = default;

    ConfigChangeDispatcher(const ConfigChangeDispatcher&) = delete;
    ConfigChangeDispatcher& operator=(const ConfigChangeDispatcher&) = delete;

    std::vector<std::weak_ptr<IConfigChangeListener>> m_listeners;
    std::vector<std::function<void(const std::string&, const boost::any&)>> m_callbacks;
    bool m_enabled{true};
};

} // namespace Slic3r

#endif // slic3r_ConfigChangeDispatcher_hpp_
