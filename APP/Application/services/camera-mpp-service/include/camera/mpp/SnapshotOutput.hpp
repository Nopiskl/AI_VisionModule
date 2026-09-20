#pragma once

#include "camera/mpp/ServiceConfig.hpp"
#include "camera/mpp/Status.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

#include <mpi_sys.h>
#include <mpi_venc.h>
#include <mpi_vi.h>

namespace camera {
namespace mpp {

class SnapshotOutput {
public:
    SnapshotOutput(CameraConfig camera, SnapshotConfig snapshot);

    Status capture(VI_DEV viDevice, VI_CHN viChannel,
                   const std::string& finalPath);

private:
    static ERRORTYPE callback(void* cookie, MPP_CHN_S* channel,
                              MPP_EVENT_TYPE event, void* eventData);
    Status writeJpegTemporary(const VENC_STREAM_S& stream,
                              const std::string& temporaryPath);

    std::atomic<ERRORTYPE> linkageError_{SUCCESS};
    CameraConfig camera_;
    SnapshotConfig snapshot_;
    std::mutex captureMutex_;
    std::mutex callbackMutex_;
    std::condition_variable callbackCondition_;
    unsigned int waitingFrameId_{0};
    bool waitingForFrameReturn_{false};
    bool frameReturned_{false};
};

}  // namespace mpp
}  // namespace camera
