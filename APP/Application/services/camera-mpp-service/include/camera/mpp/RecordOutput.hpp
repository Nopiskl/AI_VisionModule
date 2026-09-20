#pragma once

#include "camera/mpp/EventMailbox.hpp"
#include "camera/mpp/ServiceConfig.hpp"
#include "camera/mpp/Status.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <mpi_mux.h>
#include <mpi_sys.h>
#include <mpi_venc.h>
#include <mpi_vi.h>

namespace camera {
namespace mpp {

class RecordOutput {
public:
    RecordOutput(CameraConfig camera, RecordConfig record,
                 std::string mediaRoot, bool requireMediaRootMount,
                 EventMailbox& mailbox);
    ~RecordOutput();

    RecordOutput(const RecordOutput&) = delete;
    RecordOutput& operator=(const RecordOutput&) = delete;

    Status start(VI_DEV viDevice, const std::string& finalPath);
    Status stop();
    Status abort() noexcept;

    bool active() const { return started_; }
    const std::string& finalPath() const { return finalPath_; }
    std::uint64_t generation() const { return generation_; }

private:
    static ERRORTYPE callback(void* cookie, MPP_CHN_S* channel,
                              MPP_EVENT_TYPE event, void* eventData);
    Status prepareVenc();
    Status prepareMux();
    Status cleanup(bool commitFile) noexcept;
    Status checkStorage() const;
    Status startStorageMonitor();
    void stopStorageMonitor() noexcept;
    void monitorStorage() noexcept;

    CameraConfig camera_;
    RecordConfig record_;
    std::string mediaRoot_;
    bool requireMediaRootMount_{true};
    EventMailbox& mailbox_;
    VI_DEV viDevice_{-1};
    int fileDescriptor_{-1};
    std::string finalPath_;
    std::string temporaryPath_;
    bool viCreated_{false};
    bool viEnabled_{false};
    bool vencCreated_{false};
    bool vencStarted_{false};
    bool muxChannelCreated_{false};
    bool muxStarted_{false};
    bool viVencBound_{false};
    bool vencMuxBound_{false};
    bool started_{false};
    std::uint64_t generation_{0};
    std::atomic<bool> monitorStop_{false};
    std::mutex monitorMutex_;
    std::condition_variable monitorCondition_;
    std::thread monitorThread_;
};

}  // namespace mpp
}  // namespace camera
