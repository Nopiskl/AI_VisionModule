#pragma once

#include "camera/mpp/EventMailbox.hpp"
#include "camera/mpp/ServiceConfig.hpp"
#include "camera/mpp/Status.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <mpi_sys.h>
#include <mpi_venc.h>
#include <mpi_vi.h>

class MediaStream;
class TinyServer;

namespace camera {
namespace mpp {

class RtspOutput {
public:
    RtspOutput(CameraConfig camera, RtspConfig rtsp, EventMailbox& mailbox);
    ~RtspOutput();

    RtspOutput(const RtspOutput&) = delete;
    RtspOutput& operator=(const RtspOutput&) = delete;

    Status start(VI_DEV viDevice);
    Status stop() noexcept;

    bool active() const { return active_; }
    const std::string& url() const { return url_; }
    std::uint64_t generation() const { return generation_; }

private:
    static ERRORTYPE vencCallback(void* cookie, MPP_CHN_S* channel,
                                  MPP_EVENT_TYPE event, void* eventData);
    Status prepareVenc();
    Status prepareCodecHeader();
    Status prepareServer();
    void streamLoop() noexcept;
    static void newClientCallback(void* context);
    void postFaultOnce(const std::string& code,
                       const std::string& detail) noexcept;

    CameraConfig camera_;
    RtspConfig rtsp_;
    EventMailbox& mailbox_;
    VI_DEV viDevice_{-1};
    TinyServer* server_{nullptr};
    MediaStream* stream_{nullptr};
    std::vector<unsigned char> codecHeader_;
    std::string url_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> faultPosted_{false};
    std::atomic<bool> keyFrameRequested_{false};
    std::thread streamThread_;
    bool viCreated_{false};
    bool viEnabled_{false};
    bool vencCreated_{false};
    bool vencStarted_{false};
    bool viVencBound_{false};
    bool serverStarted_{false};
    bool active_{false};
    std::uint64_t generation_{0};
};

}  // namespace mpp
}  // namespace camera
