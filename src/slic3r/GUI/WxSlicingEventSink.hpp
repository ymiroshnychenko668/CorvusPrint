#ifndef slic3r_GUI_WxSlicingEventSink_hpp_
#define slic3r_GUI_WxSlicingEventSink_hpp_

#include "libslic3r/SlicingEvents.hpp"
#include "BackgroundSlicingProcess.hpp"
#include <wx/event.h>

namespace Slic3r { namespace GUI {

// wxWidgets adapter for ISlicingEventSink
// Converts slicing events to wxWidgets events and posts them to the UI thread
//
// Usage:
//   auto wx_sink = std::make_shared<WxSlicingEventSink>(
//       plater,
//       EVT_SLICING_COMPLETED,
//       EVT_PROCESS_FINISHED,
//       EVT_EXPORT_BEGAN,
//       EVT_EXPORT_FINISHED
//   );
//   dispatcher->add_sink(wx_sink);
//
class WxSlicingEventSink : public ISlicingEventSink {
public:
    WxSlicingEventSink(wxEvtHandler* handler,
                       wxEventType slicing_update_event_type,
                       int slicing_completed_id,
                       int finished_id,
                       int export_began_id,
                       int export_finished_id)
        : m_handler(handler)
        , m_slicing_update_event_type(slicing_update_event_type)
        , m_slicing_completed_id(slicing_completed_id)
        , m_finished_id(finished_id)
        , m_export_began_id(export_began_id)
        , m_export_finished_id(export_finished_id)
    {}

    ~WxSlicingEventSink() override = default;

    void on_slicing_update(const PrintBase::SlicingStatus& status) override {
        if (m_handler) {
            wxQueueEvent(m_handler,
                new SlicingStatusEvent(m_slicing_update_event_type, 0, status));
        }
    }

    void on_slicing_completed(int timestamp) override {
        if (m_handler && m_slicing_completed_id != 0) {
            auto evt = new wxCommandEvent(m_slicing_completed_id);
            evt->SetInt(timestamp);
            wxQueueEvent(m_handler, evt);
        }
    }

    void on_process_finished(const SlicingCompletedInfo& info) override {
        if (m_handler && m_finished_id != 0) {
            // Convert SlicingCompletedInfo to SlicingProcessCompletedEvent
            SlicingProcessCompletedEvent::StatusType wx_status;
            switch (info.status) {
                case SlicingCompletedInfo::Finished:
                    wx_status = SlicingProcessCompletedEvent::Finished;
                    break;
                case SlicingCompletedInfo::Cancelled:
                    wx_status = SlicingProcessCompletedEvent::Cancelled;
                    break;
                case SlicingCompletedInfo::Error:
                default:
                    wx_status = SlicingProcessCompletedEvent::Error;
                    break;
            }

            // Note: Exception handling needs to be adapted if needed
            // For now, we pass nullptr for exception_ptr
            wxQueueEvent(m_handler,
                new SlicingProcessCompletedEvent(m_finished_id, 0, wx_status, nullptr));
        }
    }

    void on_export_began() override {
        if (m_handler && m_export_began_id != 0) {
            wxQueueEvent(m_handler, new wxCommandEvent(m_export_began_id));
        }
    }

    void on_export_finished(const std::string& path) override {
        if (m_handler && m_export_finished_id != 0) {
            auto evt = new wxCommandEvent(m_export_finished_id);
            evt->SetString(wxString::FromUTF8(path.c_str()));
            wxQueueEvent(m_handler, evt);
        }
    }

    // Change the target handler
    void set_handler(wxEvtHandler* handler) { m_handler = handler; }

private:
    wxEvtHandler* m_handler;
    wxEventType   m_slicing_update_event_type;
    int           m_slicing_completed_id;
    int           m_finished_id;
    int           m_export_began_id;
    int           m_export_finished_id;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_WxSlicingEventSink_hpp_
