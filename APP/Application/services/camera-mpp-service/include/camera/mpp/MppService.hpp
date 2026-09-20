#pragma once

#include "camera/common/IpcMessage.hpp"
#include "camera/mpp/BackendLock.hpp"
#include "camera/mpp/CameraPipeline.hpp"
#include "camera/mpp/EventMailbox.hpp"
#include "camera/mpp/MppRuntime.hpp"
#include "camera/mpp/PlaybackPipeline.hpp"
#include "camera/mpp/ServiceConfig.hpp"
#include "camera/mpp/Status.hpp"
#include "camera/mpp/UvcPipeline.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace camera {
namespace mpp {

enum class ServiceMode {
    None,
    Camera,
    Playback,
    Uvc,
};

class MppService {
public:
    using EventSink = std::function<void(const ipc::Message&)>;

    explicit MppService(ServiceConfig config);
    ~MppService();

    MppService(const MppService&) = delete;
    MppService& operator=(const MppService&) = delete;

    Status initialize();
    Status shutdown() noexcept;
    ipc::Message handle(const ipc::Message& request);
    void drainPipelineEvents();
    void setEventSink(EventSink sink) { eventSink_ = std::move(sink); }

    EventMailbox& mailbox() { return mailbox_; }
    bool shutdownRequested() const { return shutdownRequested_; }
    ServiceMode mode() const { return mode_; }
    std::string stateName() const;

private:
    Status enterMode(ServiceMode mode);
    Status leaveMode() noexcept;
    Status startSnapshot(const std::string& path);
    void finishSnapshot() noexcept;
    void reapSnapshot() noexcept;
    Status validateMediaPath(const std::string& requested,
                             std::string* resolved) const;
    Status makeOutputPath(const ipc::Message& request, const std::string& prefix,
                          const std::string& extension, std::string* output) const;
    void emit(ipc::Message event) const;
    ipc::Message response(const ipc::Message& request, const Status& status) const;
    void addStatusFields(ipc::Message* message) const;
    std::uint64_t recordElapsedMs() const;

    ServiceConfig config_;
    EventMailbox mailbox_;
    BackendLock backendLock_;
    MppRuntime runtime_;
    CameraPipeline camera_;
    PlaybackPipeline playback_;
    UvcPipeline uvc_;
    EventSink eventSink_;
    ServiceMode mode_{ServiceMode::None};
    bool initialized_{false};
    bool shutdownRequested_{false};
    bool recordClockActive_{false};
    std::chrono::steady_clock::time_point recordStartedAt_{};
    std::atomic<bool> snapshotBusy_{false};
    std::thread snapshotThread_;
};

const char* toString(ServiceMode mode);

}  // namespace mpp
}  // namespace camera
