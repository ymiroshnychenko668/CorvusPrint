#ifndef slic3r_Network_HttpSlicerApi_hpp_
#define slic3r_Network_HttpSlicerApi_hpp_

#include "libslic3r/SlicingEvents.hpp"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

// Forward declarations
namespace Slic3r {
    class BackgroundSlicingProcess;
    class Model;
    class DynamicPrintConfig;
}

namespace httplib {
    class Server;
}

namespace Slic3r {

// Configuration for HTTP API server
struct HttpApiConfig {
    std::string bind_address{"0.0.0.0"};
    int         port{8080};
    bool        enable_cors{true};
};

// HTTP REST API for controlling the slicer
// Provides endpoints for starting/stopping slicing and querying status
//
// Endpoints:
//   GET  /api/status            - Get current slicing status
//   POST /api/start             - Start slicing
//   POST /api/stop              - Stop slicing
//   POST /api/apply             - Apply configuration (JSON body)
//   GET  /api/result            - Get slicing result info
//   GET  /api/health            - Health check
//   GET  /api/printers          - Get available printers list
//   POST /api/printers/select   - Select printer preset (body: {"name": "..."})
//   GET  /api/filaments         - Get available filaments list
//   POST /api/filaments/select  - Select filament preset (body: {"name": "...", "extruder": 0})
//
// Usage:
//   HttpApiConfig config;
//   config.port = 8080;
//   auto api = std::make_shared<HttpSlicerApi>(config, &background_process);
//   api->start();
//
class HttpSlicerApi {
public:
    HttpSlicerApi(const HttpApiConfig& config, BackgroundSlicingProcess* process);
    ~HttpSlicerApi();

    // Non-copyable
    HttpSlicerApi(const HttpSlicerApi&) = delete;
    HttpSlicerApi& operator=(const HttpSlicerApi&) = delete;

    // Start the HTTP server (non-blocking, runs in separate thread)
    bool start();

    // Stop the HTTP server
    void stop();

    // Check if server is running
    bool is_running() const;

    // Get the port the server is listening on
    int get_port() const { return m_config.port; }

    // Update last known status (called by event sink)
    void update_status(const PrintBase::SlicingStatus& status);
    void update_completed(const SlicingCompletedInfo& info);

private:
    void setup_routes();
    void server_thread_func();

    // Request handlers
    void handle_status(/* request, response */);
    void handle_start(/* request, response */);
    void handle_stop(/* request, response */);
    void handle_health(/* request, response */);

    HttpApiConfig               m_config;
    BackgroundSlicingProcess*   m_process;
    std::unique_ptr<httplib::Server> m_server;
    std::thread                 m_server_thread;
    std::atomic<bool>           m_running{false};

    // Cached status for GET /status
    mutable std::mutex          m_status_mutex;
    PrintBase::SlicingStatus    m_last_status;
    SlicingCompletedInfo        m_last_completed;
    bool                        m_has_completed{false};
};

// HTTP event sink that updates the HttpSlicerApi with current status
// This allows polling via GET /api/status
class HttpEventSink : public ISlicingEventSink {
public:
    explicit HttpEventSink(std::shared_ptr<HttpSlicerApi> api)
        : m_api(api) {}

    void on_slicing_update(const PrintBase::SlicingStatus& status) override {
        if (auto api = m_api.lock()) {
            api->update_status(status);
        }
    }

    void on_slicing_completed(int /*timestamp*/) override {
        // Status already updated via on_slicing_update
    }

    void on_process_finished(const SlicingCompletedInfo& info) override {
        if (auto api = m_api.lock()) {
            api->update_completed(info);
        }
    }

    void on_export_began() override {
        // Could update status if needed
    }

    void on_export_finished(const std::string& /*path*/) override {
        // Could update status if needed
    }

private:
    std::weak_ptr<HttpSlicerApi> m_api;
};

} // namespace Slic3r

#endif // slic3r_Network_HttpSlicerApi_hpp_
