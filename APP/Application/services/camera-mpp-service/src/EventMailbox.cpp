#include "camera/mpp/EventMailbox.hpp"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <utility>
#include <unistd.h>

namespace camera {
namespace mpp {
namespace {

void setDescriptorFlags(int descriptor) {
    const int status = fcntl(descriptor, F_GETFL, 0);
    if (status < 0 || fcntl(descriptor, F_SETFL, status | O_NONBLOCK) < 0) {
        throw std::runtime_error("failed to make mailbox descriptor non-blocking");
    }
    const int flags = fcntl(descriptor, F_GETFD, 0);
    if (flags < 0 || fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0) {
        throw std::runtime_error("failed to mark mailbox descriptor close-on-exec");
    }
}

}  // namespace

EventMailbox::EventMailbox(std::size_t capacity) : capacity_(capacity) {
    if (capacity_ == 0) {
        throw std::invalid_argument("mailbox capacity must be non-zero");
    }
    int descriptors[2] = {-1, -1};
    if (pipe(descriptors) != 0) {
        throw std::runtime_error("failed to create event mailbox pipe");
    }
    readFd_ = descriptors[0];
    writeFd_ = descriptors[1];
    try {
        setDescriptorFlags(readFd_);
        setDescriptorFlags(writeFd_);
    } catch (...) {
        close(readFd_);
        close(writeFd_);
        readFd_ = -1;
        writeFd_ = -1;
        throw;
    }
}

EventMailbox::~EventMailbox() {
    if (readFd_ >= 0) {
        close(readFd_);
    }
    if (writeFd_ >= 0) {
        close(writeFd_);
    }
}

void EventMailbox::post(PipelineEvent event) noexcept {
    enqueue(std::move(event), false);
}

void EventMailbox::postCritical(PipelineEvent event) noexcept {
    enqueue(std::move(event), true);
}

void EventMailbox::enqueue(PipelineEvent event, bool replaceOldest) noexcept {
    bool queued = false;
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (events_.size() < capacity_) {
            events_.push_back(std::move(event));
            queued = true;
        } else if (replaceOldest) {
            events_.pop_front();
            events_.push_back(std::move(event));
            ++dropped_;
            queued = true;
        } else {
            ++dropped_;
        }
    } catch (...) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            ++dropped_;
        } catch (...) {
        }
    }
    if (!queued) {
        return;
    }
    const unsigned char marker = 1;
    ssize_t ignored;
    do {
        ignored = write(writeFd_, &marker, sizeof(marker));
    } while (ignored < 0 && errno == EINTR);
    (void)ignored;
}

std::vector<PipelineEvent> EventMailbox::drain() {
    unsigned char buffer[64];
    for (;;) {
        const ssize_t count = read(readFd_, buffer, sizeof(buffer));
        if (count > 0 || (count < 0 && errno == EINTR)) {
            continue;
        }
        break;
    }

    std::vector<PipelineEvent> result;
    std::lock_guard<std::mutex> lock(mutex_);
    result.reserve(events_.size());
    while (!events_.empty()) {
        result.push_back(std::move(events_.front()));
        events_.pop_front();
    }
    return result;
}

std::size_t EventMailbox::droppedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

}  // namespace mpp
}  // namespace camera
