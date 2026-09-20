#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace camera {
namespace mpp {

enum class PipelineEventType {
    ViTimeout,
    VoRenderingStarted,
    PlaybackEof,
    SnapshotCompleted,
    RecordDone,
    StorageFault,
    RtspFault,
    UvcConnected,
    UvcDisconnected,
    UvcCommitted,
    UvcStreamingStarted,
    UvcStreamingStopped,
    UvcFault,
    PipelineError,
};

struct PipelineEvent {
    PipelineEventType type{PipelineEventType::PipelineError};
    std::string code;
    std::string detail;
    std::uint64_t generation{0};

    PipelineEvent() = default;
    PipelineEvent(PipelineEventType eventType, std::string eventCode,
                  std::string eventDetail,
                  std::uint64_t eventGeneration = 0)
        : type(eventType),
          code(std::move(eventCode)),
          detail(std::move(eventDetail)),
          generation(eventGeneration) {}
};

class EventMailbox {
public:
    explicit EventMailbox(std::size_t capacity = 64);
    ~EventMailbox();

    EventMailbox(const EventMailbox&) = delete;
    EventMailbox& operator=(const EventMailbox&) = delete;

    int descriptor() const { return readFd_; }
    void post(PipelineEvent event) noexcept;
    void postCritical(PipelineEvent event) noexcept;
    std::vector<PipelineEvent> drain();
    std::size_t droppedCount() const;

private:
    const std::size_t capacity_;
    int readFd_{-1};
    int writeFd_{-1};
    mutable std::mutex mutex_;
    std::deque<PipelineEvent> events_;
    std::size_t dropped_{0};

    void enqueue(PipelineEvent event, bool replaceOldest) noexcept;
};

}  // namespace mpp
}  // namespace camera
