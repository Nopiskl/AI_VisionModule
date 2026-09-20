#pragma once

#include "camera/mpp/DisplayOutput.hpp"
#include "camera/mpp/EventMailbox.hpp"
#include "camera/mpp/RecordOutput.hpp"
#include "camera/mpp/RtspOutput.hpp"
#include "camera/mpp/ServiceConfig.hpp"
#include "camera/mpp/SnapshotOutput.hpp"
#include "camera/mpp/Status.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <mpi_isp.h>
#include <mpi_sys.h>
#include <mpi_vi.h>

namespace camera {
namespace mpp {

class CameraPipeline {
public:
    CameraPipeline(const ServiceConfig& config, EventMailbox& mailbox);
    ~CameraPipeline();

    CameraPipeline(const CameraPipeline&) = delete;
    CameraPipeline& operator=(const CameraPipeline&) = delete;

    Status start();
    Status stop() noexcept;
    Status takeSnapshot(const std::string& path);
    Status startRecord(const std::string& path);
    Status stopRecord();
    Status startRtsp();
    Status stopRtsp();

    bool active() const { return active_; }
    bool recording() const { return record_.active(); }
    const std::string& recordPath() const { return record_.finalPath(); }
    std::uint64_t recordGeneration() const { return record_.generation(); }
    bool rtspActive() const { return rtsp_.active(); }
    const std::string& rtspUrl() const { return rtsp_.url(); }
    std::uint64_t rtspGeneration() const { return rtsp_.generation(); }

private:
    Status ensureMediaCapture();
    static ERRORTYPE callback(void* cookie, MPP_CHN_S* channel,
                              MPP_EVENT_TYPE event, void* eventData);

    ServiceConfig config_;
    EventMailbox& mailbox_;
    SnapshotOutput snapshot_;
    RecordOutput record_;
    RtspOutput rtsp_;
    std::unique_ptr<DisplayOutput> display_;
    bool vippCreated_{false};
    std::mutex mediaCaptureMutex_;
    bool mediaVippCreated_{false};
    bool mediaVippEnabled_{false};
    bool ispRunning_{false};
    bool previewChannelCreated_{false};
    bool vippEnabled_{false};
    bool previewBound_{false};
    bool previewEnabled_{false};
    bool active_{false};
};

}  // namespace mpp
}  // namespace camera
