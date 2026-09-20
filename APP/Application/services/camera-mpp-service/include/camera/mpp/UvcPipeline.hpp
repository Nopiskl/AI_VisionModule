#pragma once

#include "camera/mpp/EventMailbox.hpp"
#include "camera/mpp/ServiceConfig.hpp"
#include "camera/mpp/Status.hpp"
#include "camera/mpp/UvcGadget.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <linux/usb/video.h>
#include <mpi_isp.h>
#include <mpi_sys.h>
#include <mpi_venc.h>
#include <mpi_vi.h>

namespace camera {
namespace mpp {

enum class UvcState {
    Stopped,
    WaitingHost,
    Connected,
    Committed,
    Streaming,
    Fault,
};

class UvcPipeline {
public:
    UvcPipeline(UvcConfig config, EventMailbox& mailbox);
    ~UvcPipeline();

    UvcPipeline(const UvcPipeline&) = delete;
    UvcPipeline& operator=(const UvcPipeline&) = delete;

    Status start();
    Status stop() noexcept;

    bool active() const { return active_.load(); }
    bool hostConnected() const { return hostConnected_.load(); }
    bool streaming() const { return state_.load() == UvcState::Streaming; }
    UvcState state() const { return state_.load(); }
    std::string stateName() const;
    std::string formatName() const;
    int width() const { return currentWidth_.load(); }
    int height() const { return currentHeight_.load(); }
    int frameRate() const { return currentFrameRate_.load(); }
    std::uint64_t droppedFrames() const { return droppedFrames_.load(); }
    const std::string& devicePath() const { return config_.videoDevice; }

    // Internal value types are public only so the translation unit can define
    // the fixed sample-compatible profile table without dynamic allocation.
    struct Profile {
        std::uint8_t formatIndex;
        std::uint8_t frameIndex;
        std::uint32_t pixelFormat;
        int width;
        int height;
        std::uint32_t interval100ns;
    };

    struct GadgetBuffer {
        void* address{nullptr};
        std::size_t length{0};
    };

    struct FrameBlock {
        std::vector<unsigned char> bytes;
        std::size_t used{0};
        bool preserveUntilSent{false};
    };

private:
    static ERRORTYPE vencCallback(void* cookie, MPP_CHN_S* channel,
                                  MPP_EVENT_TYPE event, void* eventData);

    static const Profile* defaultProfile();
    static const Profile* findProfile(std::uint8_t formatIndex,
                                      std::uint8_t frameIndex);
    static void fillStreamingControl(const Profile& profile,
                                     uvc_streaming_control* control);
    static ERRORTYPE viCallback(void* cookie, MPP_CHN_S* channel,
                                MPP_EVENT_TYPE event, void* eventData);

    Status subscribeEvents();
    void unsubscribeEvents() noexcept;
    void eventLoop() noexcept;
    Status processEvent();
    Status processSetup(const uvc::Event& event, uvc::RequestData* response);
    Status processData(const uvc::RequestData& request);
    Status applyCommit(const Profile& profile,
                       const uvc_streaming_control& requested);
    Status setVideoFormat(const Profile& profile);

    Status startStreaming();
    Status stopStreaming(bool issueStreamOff) noexcept;
    Status requestGadgetBuffers();
    Status releaseGadgetBuffers() noexcept;
    Status prepareCapture(const Profile& profile);
    Status releaseCapture() noexcept;
    Status prepareEncoder(const Profile& profile);
    Status initializeFramePool(const Profile& profile);
    void clearFramePool() noexcept;
    Status drainOneOutputBuffer();

    void captureLoop() noexcept;
    bool acquireFrameBlock(FrameBlock* output) noexcept;
    void enqueueFrameBlock(FrameBlock frame) noexcept;
    void recycleFrameBlock(FrameBlock frame) noexcept;
    bool copyEncodedPack(const VENC_PACK_S& pack, FrameBlock* frame) noexcept;
    bool convertNv21ToYuyv(const VIDEO_FRAME_INFO_S& source,
                           FrameBlock* frame) noexcept;
    void setCaptureFault(std::string code, std::string detail) noexcept;
    void handleCaptureFault() noexcept;
    void postFault(std::string code, std::string detail) noexcept;
    void postStateEvent(PipelineEventType type, const char* code,
                        const std::string& detail) noexcept;

    UvcConfig config_;
    EventMailbox& mailbox_;
    int deviceFd_{-1};
    std::thread eventThread_;
    std::thread captureThread_;
    std::atomic<bool> active_{false};
    std::atomic<bool> hostConnected_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> captureStopRequested_{false};
    std::atomic<bool> captureFaultPending_{false};
    std::atomic<UvcState> state_{UvcState::Stopped};
    std::atomic<int> currentWidth_{0};
    std::atomic<int> currentHeight_{0};
    std::atomic<int> currentFrameRate_{0};
    std::atomic<std::uint32_t> currentPixelFormat_{0};
    std::atomic<std::uint64_t> droppedFrames_{0};
    std::mutex frameMutex_;
    std::deque<FrameBlock> freeFrames_;
    std::deque<FrameBlock> readyFrames_;
    std::mutex faultMutex_;
    std::string captureFaultCode_;
    std::string captureFaultDetail_;
    std::vector<GadgetBuffer> gadgetBuffers_;
    uvc_streaming_control probe_{};
    uvc_streaming_control commit_{};
    int pendingSetControl_{0};
    const Profile* committedProfile_{nullptr};
    bool eventsSubscribed_{false};
    bool gadgetStreaming_{false};
    bool vippCreated_{false};
    bool ispRunning_{false};
    bool viCreated_{false};
    bool vippEnabled_{false};
    bool viEnabled_{false};
    bool vencCreated_{false};
    bool vencStarted_{false};
    bool viVencBound_{false};
};

const char* toString(UvcState state);

}  // namespace mpp
}  // namespace camera
