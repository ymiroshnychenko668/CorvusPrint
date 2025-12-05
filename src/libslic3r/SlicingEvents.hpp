#ifndef slic3r_SlicingEvents_hpp_
#define slic3r_SlicingEvents_hpp_

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "PrintBase.hpp"

namespace Slic3r {

// Completion information structure (wx-independent)
struct SlicingCompletedInfo {
    enum Status {
        Finished,   // Completed successfully
        Cancelled,  // User cancelled
        Error       // Error occurred
    };

    Status              status{Finished};
    std::string         error_message;
    std::vector<size_t> error_object_ids;
    bool                critical_error{false};
    bool                invalidate_plater{false};

    bool finished() const { return status == Finished; }
    bool success() const { return status == Finished; }
    bool cancelled() const { return status == Cancelled; }
    bool error() const { return status == Error; }
};

// Export phase information
struct ExportInfo {
    enum Phase {
        Began,
        Finished
    };

    Phase       phase{Began};
    std::string path;
};

// Abstract interface for receiving slicing events
// This interface is wx-independent and can be implemented by:
// - WxSlicingEventSink (for wxWidgets GUI)
// - MqttEventSink (for MQTT publishing)
// - HttpEventSink (for HTTP callbacks)
class ISlicingEventSink {
public:
    virtual ~ISlicingEventSink() = default;

    // Progress updates (called frequently from background thread)
    // @param status - Current slicing status with progress percentage and message
    virtual void on_slicing_update(const PrintBase::SlicingStatus& status) = 0;

    // Slicing phase completed, G-code export is starting
    // @param timestamp - Timestamp of when slicing finished
    virtual void on_slicing_completed(int timestamp) = 0;

    // All processing finished (slicing + export)
    // @param info - Completion information with status and any error details
    virtual void on_process_finished(const SlicingCompletedInfo& info) = 0;

    // G-code export has started
    virtual void on_export_began() = 0;

    // G-code export has finished
    // @param path - Path to the exported G-code file
    virtual void on_export_finished(const std::string& path) = 0;
};

using SlicingEventSinkPtr = std::shared_ptr<ISlicingEventSink>;

} // namespace Slic3r

#endif // slic3r_SlicingEvents_hpp_
