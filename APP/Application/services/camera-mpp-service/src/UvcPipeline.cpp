#include "camera/mpp/UvcPipeline.hpp"
#include "camera/mpp/VencIspLink.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <endian.h>
#include <exception>
#include <fcntl.h>
#include <iterator>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace camera {
namespace mpp {
namespace {

constexpr UvcPipeline::Profile kProfiles[] = {
    {1, 1, V4L2_PIX_FMT_MJPEG, 1920, 1080, 333333},
    {1, 2, V4L2_PIX_FMT_MJPEG, 1280, 720, 333333},
    {1, 3, V4L2_PIX_FMT_MJPEG, 640, 480, 333333},
    {2, 1, V4L2_PIX_FMT_YUYV, 320, 240, 333333},
    {3, 1, V4L2_PIX_FMT_H264, 1920, 1080, 333333},
    {3, 2, V4L2_PIX_FMT_H264, 1280, 720, 333333},
};

Status mppFailure(const char* operation, ERRORTYPE result) {
    return Status::failure(operation, std::to_string(result));
}

Status systemFailure(const char* operation, const std::string& subject = {}) {
    const std::string error = std::strerror(errno);
    return Status::failure(operation,
                           subject.empty() ? error : subject + ": " + error);
}

void retainFirst(Status status, Status* firstFailure) {
    if (firstFailure->ok && !status.ok) {
        *firstFailure = std::move(status);
    }
}

std::string profileDescription(const UvcPipeline::Profile& profile) {
    const char* format = profile.pixelFormat == V4L2_PIX_FMT_MJPEG ? "mjpeg" :
                         profile.pixelFormat == V4L2_PIX_FMT_H264 ? "h264" :
                         profile.pixelFormat == V4L2_PIX_FMT_YUYV ? "yuyv" :
                                                                    "unknown";
    return std::string(format) + " " + std::to_string(profile.width) + "x" +
           std::to_string(profile.height) + "@30";
}

bool appendPart(const unsigned char* source, unsigned int length,
                UvcPipeline::FrameBlock* frame, std::size_t* offset) {
    if (length == 0) {
        return true;
    }
    if (source == nullptr || static_cast<std::size_t>(length) >
                                 frame->bytes.size() - *offset) {
        return false;
    }
    std::memcpy(frame->bytes.data() + *offset, source, length);
    *offset += length;
    return true;
}

}  // namespace

const char* toString(UvcState state) {
    switch (state) {
        case UvcState::Stopped:
            return "stopped";
        case UvcState::WaitingHost:
            return "uvc_waiting_host";
        case UvcState::Connected:
            return "uvc_connected";
        case UvcState::Committed:
            return "uvc_committed";
        case UvcState::Streaming:
            return "uvc_streaming";
        case UvcState::Fault:
            return "uvc_error";
    }
    return "uvc_error";
}

UvcPipeline::UvcPipeline(UvcConfig config, EventMailbox& mailbox)
    : config_(std::move(config)), mailbox_(mailbox) {
    fillStreamingControl(*defaultProfile(), &probe_);
    fillStreamingControl(*defaultProfile(), &commit_);
}

UvcPipeline::~UvcPipeline() {
    stop();
}

const UvcPipeline::Profile* UvcPipeline::defaultProfile() {
    return &kProfiles[0];
}

const UvcPipeline::Profile* UvcPipeline::findProfile(
    std::uint8_t formatIndex, std::uint8_t frameIndex) {
    const auto found = std::find_if(
        std::begin(kProfiles), std::end(kProfiles),
        [formatIndex, frameIndex](const Profile& profile) {
            return profile.formatIndex == formatIndex &&
                   profile.frameIndex == frameIndex;
        });
    return found == std::end(kProfiles) ? nullptr : &*found;
}

void UvcPipeline::fillStreamingControl(const Profile& profile,
                                       uvc_streaming_control* control) {
    std::memset(control, 0, sizeof(*control));
    control->bmHint = 1;
    control->bFormatIndex = profile.formatIndex;
    control->bFrameIndex = profile.frameIndex;
    control->dwFrameInterval = profile.interval100ns;
    control->dwMaxVideoFrameSize =
        static_cast<std::uint32_t>(profile.width * profile.height * 2);
    control->dwMaxPayloadTransferSize =
        static_cast<std::uint32_t>(profile.width * profile.height);
    control->bmFramingInfo = 3;
    control->bPreferedVersion = 1;
    control->bMinVersion = 1;
    control->bMaxVersion = 1;
}

Status UvcPipeline::start() {
    if (config_.enabled == 0) {
        return Status::failure("uvc_disabled", "UVC is disabled by configuration");
    }
    if (active_.load()) {
        return Status::success();
    }
    if (deviceFd_ >= 0 || eventThread_.joinable() || vippCreated_ ||
        !gadgetBuffers_.empty()) {
        return Status::failure("uvc_dirty_state", "UVC resources remain allocated");
    }

    deviceFd_ = open(config_.videoDevice.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (deviceFd_ < 0) {
        return systemFailure("uvc_device_open_failed", config_.videoDevice);
    }

    v4l2_capability capability {};
    if (ioctl(deviceFd_, VIDIOC_QUERYCAP, &capability) != 0) {
        const Status status =
            systemFailure("uvc_query_capability_failed", config_.videoDevice);
        close(deviceFd_);
        deviceFd_ = -1;
        return status;
    }
    const std::uint32_t capabilities =
        (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0
            ? capability.device_caps
            : capability.capabilities;
    if ((capabilities & V4L2_CAP_VIDEO_OUTPUT) == 0 ||
        (capabilities & V4L2_CAP_STREAMING) == 0) {
        close(deviceFd_);
        deviceFd_ = -1;
        return Status::failure(
            "uvc_device_capability_missing",
            config_.videoDevice + " is not a streaming VIDEO_OUTPUT gadget node");
    }

    Status status = subscribeEvents();
    if (!status.ok) {
        unsubscribeEvents();
        close(deviceFd_);
        deviceFd_ = -1;
        return status;
    }

    fillStreamingControl(*defaultProfile(), &probe_);
    fillStreamingControl(*defaultProfile(), &commit_);
    pendingSetControl_ = 0;
    committedProfile_ = nullptr;
    currentWidth_.store(0);
    currentHeight_.store(0);
    currentFrameRate_.store(0);
    currentPixelFormat_.store(0);
    droppedFrames_.store(0);
    hostConnected_.store(false);
    stopRequested_.store(false);
    captureFaultPending_.store(false);
    state_.store(UvcState::WaitingHost);
    active_.store(true);

    try {
        eventThread_ = std::thread(&UvcPipeline::eventLoop, this);
    } catch (const std::exception& error) {
        active_.store(false);
        state_.store(UvcState::Stopped);
        unsubscribeEvents();
        close(deviceFd_);
        deviceFd_ = -1;
        return Status::failure("uvc_event_thread_failed", error.what());
    }
    return Status::success();
}

Status UvcPipeline::stop() noexcept {
    stopRequested_.store(true);
    if (eventThread_.joinable()) {
        eventThread_.join();
    }

    Status firstFailure = stopStreaming(true);
    unsubscribeEvents();
    if (deviceFd_ >= 0) {
        if (close(deviceFd_) != 0) {
            retainFirst(systemFailure("uvc_device_close_failed", config_.videoDevice),
                        &firstFailure);
        }
        deviceFd_ = -1;
    }
    hostConnected_.store(false);
    committedProfile_ = nullptr;
    pendingSetControl_ = 0;
    currentWidth_.store(0);
    currentHeight_.store(0);
    currentFrameRate_.store(0);
    currentPixelFormat_.store(0);
    active_.store(false);
    state_.store(UvcState::Stopped);
    return firstFailure;
}

std::string UvcPipeline::stateName() const {
    return toString(state_.load());
}

std::string UvcPipeline::formatName() const {
    const std::uint32_t format = currentPixelFormat_.load();
    return format == V4L2_PIX_FMT_MJPEG ? "mjpeg" :
           format == V4L2_PIX_FMT_H264 ? "h264" :
           format == V4L2_PIX_FMT_YUYV ? "yuyv" : "none";
}

Status UvcPipeline::subscribeEvents() {
    constexpr std::uint32_t eventTypes[] = {
        uvc::kEventConnect, uvc::kEventDisconnect, uvc::kEventStreamOn,
        uvc::kEventStreamOff, uvc::kEventSetup, uvc::kEventData,
    };
    for (const std::uint32_t type : eventTypes) {
        v4l2_event_subscription subscription {};
        subscription.type = type;
        if (ioctl(deviceFd_, VIDIOC_SUBSCRIBE_EVENT, &subscription) != 0) {
            return systemFailure("uvc_event_subscribe_failed",
                                 std::to_string(type));
        }
    }
    eventsSubscribed_ = true;
    return Status::success();
}

void UvcPipeline::unsubscribeEvents() noexcept {
    if (!eventsSubscribed_ || deviceFd_ < 0) {
        eventsSubscribed_ = false;
        return;
    }
    constexpr std::uint32_t eventTypes[] = {
        uvc::kEventConnect, uvc::kEventDisconnect, uvc::kEventStreamOn,
        uvc::kEventStreamOff, uvc::kEventSetup, uvc::kEventData,
    };
    for (const std::uint32_t type : eventTypes) {
        v4l2_event_subscription subscription {};
        subscription.type = type;
        (void)ioctl(deviceFd_, VIDIOC_UNSUBSCRIBE_EVENT, &subscription);
    }
    eventsSubscribed_ = false;
}

void UvcPipeline::eventLoop() noexcept {
    while (!stopRequested_.load()) {
        pollfd descriptor {};
        descriptor.fd = deviceFd_;
        descriptor.events = POLLPRI;
        if (gadgetStreaming_) {
            descriptor.events |= POLLOUT;
        }
        const int result = poll(&descriptor, 1, 100);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            postFault("uvc_poll_failed", std::strerror(errno));
            break;
        }
        if (stopRequested_.load()) {
            break;
        }

        if ((descriptor.revents & POLLPRI) != 0) {
            const Status status = processEvent();
            if (!status.ok) {
                (void)stopStreaming(true);
                postFault(status.code, status.detail);
            }
        }
        // V4L2 may report POLLERR before output buffers are allocated; unlike
        // POLLHUP/POLLNVAL it is not by itself a lost gadget node. Control
        // events and concrete ioctls above provide actionable failures.
        if ((descriptor.revents & (POLLHUP | POLLNVAL)) != 0) {
            (void)stopStreaming(false);
            postFault("uvc_device_unavailable",
                      config_.videoDevice + " poll revents=" +
                          std::to_string(descriptor.revents));
            break;
        }
        if ((descriptor.revents & POLLOUT) != 0 && gadgetStreaming_) {
            const Status status = drainOneOutputBuffer();
            if (!status.ok) {
                (void)stopStreaming(true);
                postFault(status.code, status.detail);
            }
        }
        if (captureFaultPending_.load()) {
            handleCaptureFault();
        }
    }
}

Status UvcPipeline::processEvent() {
    v4l2_event rawEvent {};
    if (ioctl(deviceFd_, VIDIOC_DQEVENT, &rawEvent) != 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Status::success();
        }
        return systemFailure("uvc_event_dequeue_failed");
    }
    const auto* event = reinterpret_cast<const uvc::Event*>(rawEvent.u.data);

    switch (rawEvent.type) {
        case uvc::kEventConnect:
            hostConnected_.store(true);
            state_.store(committedProfile_ == nullptr ? UvcState::Connected
                                                      : UvcState::Committed);
            postStateEvent(PipelineEventType::UvcConnected, "uvc_connected",
                           config_.videoDevice);
            return Status::success();

        case uvc::kEventDisconnect: {
            // The fd is still valid while handling DISCONNECT. Issue STREAMOFF
            // so REQBUFS(count=0) can release a driver that does not implicitly
            // stop its output queue on USB disconnect; EINVAL/ENODEV are benign.
            const Status status = stopStreaming(true);
            hostConnected_.store(false);
            committedProfile_ = nullptr;
            currentWidth_.store(0);
            currentHeight_.store(0);
            currentFrameRate_.store(0);
            currentPixelFormat_.store(0);
            state_.store(UvcState::WaitingHost);
            postStateEvent(PipelineEventType::UvcDisconnected,
                           "uvc_disconnected", config_.videoDevice);
            return status;
        }

        case uvc::kEventStreamOn: {
            const Status status = startStreaming();
            if (status.ok) {
                postStateEvent(PipelineEventType::UvcStreamingStarted,
                               "uvc_streaming_started",
                               committedProfile_ == nullptr
                                   ? std::string{}
                                   : profileDescription(*committedProfile_));
            }
            return status;
        }

        case uvc::kEventStreamOff: {
            const Status status = stopStreaming(true);
            state_.store(hostConnected_.load() ? UvcState::Committed
                                               : UvcState::WaitingHost);
            postStateEvent(PipelineEventType::UvcStreamingStopped,
                           "uvc_streaming_stopped", config_.videoDevice);
            return status;
        }

        case uvc::kEventSetup: {
            uvc::RequestData response {};
            response.length = -EL2HLT;
            Status status = processSetup(*event, &response);
            if (!status.ok) {
                return status;
            }
            if (ioctl(deviceFd_, uvc::kSendResponse, &response) != 0) {
                return systemFailure("uvc_control_response_failed");
            }
            return Status::success();
        }

        case uvc::kEventData:
            return processData(event->data);

        default:
            return Status::success();
    }
}

Status UvcPipeline::processSetup(const uvc::Event& event,
                                 uvc::RequestData* response) {
    const usb_ctrlrequest& request = event.request;
    if ((request.bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS ||
        (request.bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE ||
        (le16toh(request.wIndex) & 0xffU) != uvc::kStreamingInterface) {
        return Status::success();
    }

    const std::uint8_t selector =
        static_cast<std::uint8_t>(le16toh(request.wValue) >> 8U);
    if (selector != UVC_VS_PROBE_CONTROL &&
        selector != UVC_VS_COMMIT_CONTROL) {
        return Status::success();
    }

    const std::uint16_t hostLength = le16toh(request.wLength);
    const auto limitLength = [response, hostLength](std::size_t size) {
        response->length = static_cast<std::int32_t>(
            std::min<std::size_t>({size, hostLength, sizeof(response->data)}));
    };

    switch (request.bRequest) {
        case UVC_SET_CUR:
            pendingSetControl_ = selector;
            limitLength(sizeof(uvc_streaming_control));
            break;

        case UVC_GET_CUR: {
            const uvc_streaming_control& control =
                selector == UVC_VS_PROBE_CONTROL ? probe_ : commit_;
            limitLength(sizeof(control));
            std::memcpy(response->data, &control,
                        static_cast<std::size_t>(response->length));
            break;
        }

        case UVC_GET_MIN:
        case UVC_GET_MAX:
        case UVC_GET_DEF: {
            uvc_streaming_control control {};
            fillStreamingControl(*defaultProfile(), &control);
            limitLength(sizeof(control));
            std::memcpy(response->data, &control,
                        static_cast<std::size_t>(response->length));
            break;
        }

        case UVC_GET_RES:
            limitLength(sizeof(uvc_streaming_control));
            std::memset(response->data, 0,
                        static_cast<std::size_t>(response->length));
            break;

        case UVC_GET_LEN:
            response->data[0] =
                static_cast<std::uint8_t>(sizeof(uvc_streaming_control));
            response->data[1] = 0;
            limitLength(2);
            break;

        case UVC_GET_INFO:
            response->data[0] = 0x03;
            limitLength(1);
            break;

        default:
            break;
    }
    return Status::success();
}

Status UvcPipeline::processData(const uvc::RequestData& request) {
    if (pendingSetControl_ != UVC_VS_PROBE_CONTROL &&
        pendingSetControl_ != UVC_VS_COMMIT_CONTROL) {
        return Status::failure("uvc_control_data_unexpected",
                               "DATA arrived without SET_CUR");
    }
    if (request.length < 4) {
        pendingSetControl_ = 0;
        return Status::failure("uvc_control_data_short",
                               std::to_string(request.length));
    }

    uvc_streaming_control requested {};
    const std::size_t copyLength = std::min<std::size_t>(
        static_cast<std::size_t>(request.length), sizeof(requested));
    std::memcpy(&requested, request.data, copyLength);

    const Profile* profile =
        findProfile(requested.bFormatIndex, requested.bFrameIndex);
    if (profile == nullptr) {
        profile = defaultProfile();
    }
    uvc_streaming_control normalized {};
    fillStreamingControl(*profile, &normalized);
    if (config_.bulkMode != 0 && requested.dwMaxPayloadTransferSize != 0) {
        normalized.dwMaxPayloadTransferSize =
            requested.dwMaxPayloadTransferSize;
    }

    const int target = pendingSetControl_;
    pendingSetControl_ = 0;
    if (target == UVC_VS_PROBE_CONTROL) {
        probe_ = normalized;
        return Status::success();
    }
    commit_ = normalized;
    return applyCommit(*profile, requested);
}

Status UvcPipeline::applyCommit(
    const Profile& profile, const uvc_streaming_control&) {
    if (gadgetStreaming_ || captureThread_.joinable() || vippCreated_) {
        const Status stopStatus = stopStreaming(true);
        if (!stopStatus.ok) {
            return stopStatus;
        }
    }

    Status status = setVideoFormat(profile);
    if (!status.ok) {
        return status;
    }
    committedProfile_ = &profile;
    currentWidth_.store(profile.width);
    currentHeight_.store(profile.height);
    currentFrameRate_.store(30);
    currentPixelFormat_.store(profile.pixelFormat);
    state_.store(UvcState::Committed);
    postStateEvent(PipelineEventType::UvcCommitted, "uvc_committed",
                   profileDescription(profile));

    if (config_.bulkMode != 0) {
        status = startStreaming();
        if (status.ok) {
            postStateEvent(PipelineEventType::UvcStreamingStarted,
                           "uvc_streaming_started",
                           profileDescription(profile));
        }
    }
    return status;
}

Status UvcPipeline::setVideoFormat(const Profile& profile) {
    v4l2_format format {};
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = static_cast<std::uint32_t>(profile.width);
    format.fmt.pix.height = static_cast<std::uint32_t>(profile.height);
    format.fmt.pix.pixelformat = profile.pixelFormat;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    format.fmt.pix.sizeimage = static_cast<std::uint32_t>(
        profile.pixelFormat == V4L2_PIX_FMT_YUYV
            ? profile.width * profile.height * 2
            : config_.maxFrameBytes);
    if (ioctl(deviceFd_, VIDIOC_S_FMT, &format) != 0) {
        return systemFailure("uvc_set_format_failed", profileDescription(profile));
    }
    if (format.fmt.pix.width != static_cast<std::uint32_t>(profile.width) ||
        format.fmt.pix.height != static_cast<std::uint32_t>(profile.height) ||
        format.fmt.pix.pixelformat != profile.pixelFormat) {
        return Status::failure(
            "uvc_format_adjusted",
            "gadget rejected exact committed profile " +
                profileDescription(profile));
    }
    return Status::success();
}

Status UvcPipeline::startStreaming() {
    if (gadgetStreaming_) {
        return Status::success();
    }
    if (committedProfile_ == nullptr) {
        return Status::failure("uvc_stream_without_commit",
                               "STREAMON arrived before a valid COMMIT");
    }

    Status status = requestGadgetBuffers();
    if (!status.ok) {
        return status;
    }
    status = prepareCapture(*committedProfile_);
    if (!status.ok) {
        (void)releaseCapture();
        (void)releaseGadgetBuffers();
        return status;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(deviceFd_, VIDIOC_STREAMON, &type) != 0) {
        status = systemFailure("uvc_stream_on_failed");
        (void)releaseCapture();
        (void)releaseGadgetBuffers();
        return status;
    }
    gadgetStreaming_ = true;
    state_.store(UvcState::Streaming);
    return Status::success();
}

Status UvcPipeline::stopStreaming(bool issueStreamOff) noexcept {
    Status firstFailure = Status::success();
    if (gadgetStreaming_ && issueStreamOff && deviceFd_ >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        if (ioctl(deviceFd_, VIDIOC_STREAMOFF, &type) != 0 &&
            errno != EINVAL && errno != ENODEV) {
            retainFirst(systemFailure("uvc_stream_off_failed"), &firstFailure);
        }
    }
    gadgetStreaming_ = false;
    retainFirst(releaseCapture(), &firstFailure);
    retainFirst(releaseGadgetBuffers(), &firstFailure);
    if (active_.load() && state_.load() != UvcState::Fault) {
        state_.store(hostConnected_.load() && committedProfile_ != nullptr
                         ? UvcState::Committed
                         : hostConnected_.load() ? UvcState::Connected
                                                 : UvcState::WaitingHost);
    }
    return firstFailure;
}

Status UvcPipeline::requestGadgetBuffers() {
    if (!gadgetBuffers_.empty()) {
        return Status::failure("uvc_buffers_busy",
                               "gadget buffers are already mapped");
    }
    v4l2_requestbuffers request {};
    request.count = static_cast<std::uint32_t>(config_.gadgetBufferCount);
    request.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    request.memory = V4L2_MEMORY_MMAP;
    if (ioctl(deviceFd_, VIDIOC_REQBUFS, &request) != 0) {
        return systemFailure("uvc_request_buffers_failed");
    }
    if (request.count == 0) {
        v4l2_requestbuffers release {};
        release.count = 0;
        release.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        release.memory = V4L2_MEMORY_MMAP;
        (void)ioctl(deviceFd_, VIDIOC_REQBUFS, &release);
        return Status::failure("uvc_no_buffers", "gadget returned zero buffers");
    }

    try {
        gadgetBuffers_.resize(request.count);
    } catch (const std::exception& error) {
        (void)releaseGadgetBuffers();
        return Status::failure("uvc_buffer_table_failed", error.what());
    }

    for (std::uint32_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (ioctl(deviceFd_, VIDIOC_QUERYBUF, &buffer) != 0) {
            const Status status = systemFailure("uvc_query_buffer_failed",
                                                std::to_string(index));
            (void)releaseGadgetBuffers();
            return status;
        }
        void* address = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
                             MAP_SHARED, deviceFd_, buffer.m.offset);
        if (address == MAP_FAILED) {
            const Status status = systemFailure("uvc_map_buffer_failed",
                                                std::to_string(index));
            (void)releaseGadgetBuffers();
            return status;
        }
        gadgetBuffers_[index].address = address;
        gadgetBuffers_[index].length = buffer.length;
        buffer.bytesused = 0;
        if (ioctl(deviceFd_, VIDIOC_QBUF, &buffer) != 0) {
            const Status status = systemFailure("uvc_queue_buffer_failed",
                                                std::to_string(index));
            (void)releaseGadgetBuffers();
            return status;
        }
    }
    return Status::success();
}

Status UvcPipeline::releaseGadgetBuffers() noexcept {
    Status firstFailure = Status::success();
    const bool hadBuffers = !gadgetBuffers_.empty();
    for (GadgetBuffer& buffer : gadgetBuffers_) {
        if (buffer.address != nullptr && buffer.address != MAP_FAILED &&
            buffer.length != 0 && munmap(buffer.address, buffer.length) != 0) {
            retainFirst(systemFailure("uvc_unmap_buffer_failed"), &firstFailure);
        }
        buffer.address = nullptr;
        buffer.length = 0;
    }
    gadgetBuffers_.clear();

    if (deviceFd_ >= 0 && hadBuffers) {
        v4l2_requestbuffers request {};
        request.count = 0;
        request.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        request.memory = V4L2_MEMORY_MMAP;
        if (ioctl(deviceFd_, VIDIOC_REQBUFS, &request) != 0 &&
            errno != EINVAL && errno != ENODEV) {
            retainFirst(systemFailure("uvc_release_buffers_failed"),
                        &firstFailure);
        }
    }
    return firstFailure;
}

Status UvcPipeline::prepareCapture(const Profile& profile) {
    ERRORTYPE result = AW_MPI_VI_CreateVipp(config_.vippDevice);
    if (result != SUCCESS) {
        return mppFailure("uvc_vipp_create_failed", result);
    }
    vippCreated_ = true;

    MPPCallbackInfo callbackInfo {};
    callbackInfo.cookie = this;
    callbackInfo.callback = &UvcPipeline::viCallback;
    result = AW_MPI_VI_RegisterCallback(config_.vippDevice, &callbackInfo);
    if (result != SUCCESS) {
        return mppFailure("uvc_vi_callback_failed", result);
    }

    VI_ATTR_S attributes {};
    attributes.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    attributes.memtype = V4L2_MEMORY_MMAP;
    attributes.format.pixelformat = V4L2_PIX_FMT_NV21M;
    attributes.format.field = V4L2_FIELD_NONE;
    attributes.format.colorspace = V4L2_COLORSPACE_JPEG;
    attributes.format.width = profile.width;
    attributes.format.height = profile.height;
    attributes.fps = 30;
    attributes.nbufs = config_.viBufferCount;
    attributes.nplanes = 2;
    attributes.drop_frame_num = 0;
    attributes.mbEncppEnable = TRUE;
    attributes.use_current_win = 0;
    result = AW_MPI_VI_SetVippAttr(config_.vippDevice, &attributes);
    if (result != SUCCESS) {
        return mppFailure("uvc_vipp_config_failed", result);
    }

    result = AW_MPI_ISP_Run(config_.ispDevice);
    if (result != SUCCESS) {
        return mppFailure("uvc_isp_start_failed", result);
    }
    ispRunning_ = true;

    result = AW_MPI_VI_CreateVirChn(config_.vippDevice, config_.viChannel,
                                    nullptr);
    if (result != SUCCESS) {
        return mppFailure("uvc_vi_create_failed", result);
    }
    viCreated_ = true;

    result = AW_MPI_VI_EnableVipp(config_.vippDevice);
    if (result != SUCCESS) {
        return mppFailure("uvc_vipp_enable_failed", result);
    }
    vippEnabled_ = true;

    if (profile.pixelFormat != V4L2_PIX_FMT_YUYV) {
        Status status = prepareEncoder(profile);
        if (!status.ok) {
            return status;
        }
        MPP_CHN_S vi = {MOD_ID_VIU, config_.vippDevice, config_.viChannel};
        MPP_CHN_S venc = {MOD_ID_VENC, 0, config_.vencChannel};
        result = AW_MPI_SYS_Bind(&vi, &venc);
        if (result != SUCCESS) {
            return mppFailure("uvc_bind_vi_venc_failed", result);
        }
        viVencBound_ = true;
    }

    result = AW_MPI_VI_EnableVirChn(config_.vippDevice, config_.viChannel);
    if (result != SUCCESS) {
        return mppFailure("uvc_vi_enable_failed", result);
    }
    viEnabled_ = true;

    if (vencCreated_) {
        result = AW_MPI_VENC_StartRecvPic(config_.vencChannel);
        if (result != SUCCESS) {
            return mppFailure("uvc_venc_start_failed", result);
        }
        vencStarted_ = true;
    }

    Status status = initializeFramePool(profile);
    if (!status.ok) {
        return status;
    }

    if (profile.pixelFormat == V4L2_PIX_FMT_H264) {
        VencHeaderData header {};
        result = AW_MPI_VENC_GetH264SpsPpsInfo(config_.vencChannel, &header);
        if (result != SUCCESS) {
            return mppFailure("uvc_h264_header_failed", result);
        }
        FrameBlock frame;
        if (!acquireFrameBlock(&frame) || header.pBuffer == nullptr ||
            header.nLength == 0 || header.nLength > frame.bytes.size()) {
            recycleFrameBlock(std::move(frame));
            return Status::failure("uvc_h264_header_invalid",
                                   std::to_string(header.nLength));
        }
        std::memcpy(frame.bytes.data(), header.pBuffer, header.nLength);
        frame.used = header.nLength;
        frame.preserveUntilSent = true;
        enqueueFrameBlock(std::move(frame));
        result = AW_MPI_VENC_RequestIDR(config_.vencChannel, TRUE);
        if (result != SUCCESS) {
            return mppFailure("uvc_h264_idr_failed", result);
        }
    }

    captureStopRequested_.store(false);
    captureFaultPending_.store(false);
    try {
        captureThread_ = std::thread(&UvcPipeline::captureLoop, this);
    } catch (const std::exception& error) {
        return Status::failure("uvc_capture_thread_failed", error.what());
    }
    return Status::success();
}

Status UvcPipeline::prepareEncoder(const Profile& profile) {
    VENC_CHN_ATTR_S attributes {};
    attributes.VeAttr.Type = profile.pixelFormat == V4L2_PIX_FMT_MJPEG
                                 ? PT_MJPEG
                                 : PT_H264;
    attributes.VeAttr.MaxKeyInterval = 30;
    attributes.VeAttr.SrcPicWidth = profile.width;
    attributes.VeAttr.SrcPicHeight = profile.height;
    attributes.VeAttr.Field = VIDEO_FIELD_FRAME;
    attributes.VeAttr.PixelFormat = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    attributes.VeAttr.mColorSpace = V4L2_COLORSPACE_JPEG;
    attributes.VeAttr.Rotate = ROTATE_NONE;
    attributes.GopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    attributes.GopAttr.mGopSize = 30;
    attributes.EncppAttr.mbEncppEnable = TRUE;

    if (profile.pixelFormat == V4L2_PIX_FMT_MJPEG) {
        attributes.VeAttr.AttrMjpeg.mMaxPicWidth = profile.width;
        attributes.VeAttr.AttrMjpeg.mMaxPicHeight = profile.height;
        attributes.VeAttr.AttrMjpeg.mBufSize = config_.vencVbvBufferBytes;
        attributes.VeAttr.AttrMjpeg.mThreshSize =
            config_.vencVbvThresholdBytes;
        attributes.VeAttr.AttrMjpeg.mbByFrame = TRUE;
        attributes.VeAttr.AttrMjpeg.mPicWidth = profile.width;
        attributes.VeAttr.AttrMjpeg.mPicHeight = profile.height;
        attributes.RcAttr.mRcMode = VENC_RC_MODE_MJPEGCBR;
        attributes.RcAttr.mAttrMjpegeCbr.mSrcFrmRate = 30;
        attributes.RcAttr.mAttrMjpegeCbr.mDstFrmRate = 30;
        attributes.RcAttr.mAttrMjpegeCbr.mBitRate = config_.mjpegBitRate;
    } else {
        attributes.VeAttr.AttrH264e.MaxPicWidth = profile.width;
        attributes.VeAttr.AttrH264e.MaxPicHeight = profile.height;
        attributes.VeAttr.AttrH264e.BufSize = config_.vencVbvBufferBytes;
        attributes.VeAttr.AttrH264e.mThreshSize =
            config_.vencVbvThresholdBytes;
        attributes.VeAttr.AttrH264e.bByFrame = TRUE;
        attributes.VeAttr.AttrH264e.Profile = 1;
        attributes.VeAttr.AttrH264e.mLevel = H264_LEVEL_51;
        attributes.VeAttr.AttrH264e.PicWidth = profile.width;
        attributes.VeAttr.AttrH264e.PicHeight = profile.height;
        attributes.VeAttr.AttrH264e.mbPIntraEnable = TRUE;
        attributes.RcAttr.mRcMode = VENC_RC_MODE_H264CBR;
        attributes.RcAttr.mAttrH264Cbr.mGop = 30;
        attributes.RcAttr.mAttrH264Cbr.mSrcFrmRate = 30;
        attributes.RcAttr.mAttrH264Cbr.mDstFrmRate = 30;
        attributes.RcAttr.mAttrH264Cbr.mBitRate = config_.h264BitRate;
    }

    ERRORTYPE result =
        AW_MPI_VENC_CreateChn(config_.vencChannel, &attributes);
    if (result != SUCCESS) {
        return mppFailure("uvc_venc_create_failed", result);
    }
    vencCreated_ = true;
    MPPCallbackInfo callbackInfo {};
    callbackInfo.cookie = this;
    callbackInfo.callback = &UvcPipeline::vencCallback;
    result = AW_MPI_VENC_RegisterCallback(config_.vencChannel, &callbackInfo);
    if (result != SUCCESS) {
        return mppFailure("uvc_venc_callback_failed", result);
    }

    VENC_FRAME_RATE_S frameRate {};
    frameRate.SrcFrmRate = 30;
    frameRate.DstFrmRate = 30;
    result = AW_MPI_VENC_SetFrameRate(config_.vencChannel, &frameRate);
    return result == SUCCESS ? Status::success()
                             : mppFailure("uvc_venc_fps_failed", result);
}

Status UvcPipeline::releaseCapture() noexcept {
    Status firstFailure = Status::success();
    captureStopRequested_.store(true);
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    if (vencStarted_) {
        const ERRORTYPE result = AW_MPI_VENC_StopRecvPic(config_.vencChannel);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_venc_stop_failed", result),
                        &firstFailure);
        }
        vencStarted_ = false;
    }
    if (viEnabled_) {
        const ERRORTYPE result =
            AW_MPI_VI_DisableVirChn(config_.vippDevice, config_.viChannel);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_vi_disable_failed", result),
                        &firstFailure);
        }
        viEnabled_ = false;
    }
    if (viVencBound_) {
        MPP_CHN_S vi = {MOD_ID_VIU, config_.vippDevice, config_.viChannel};
        MPP_CHN_S venc = {MOD_ID_VENC, 0, config_.vencChannel};
        const ERRORTYPE result = AW_MPI_SYS_UnBind(&vi, &venc);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_unbind_vi_venc_failed", result),
                        &firstFailure);
        }
        viVencBound_ = false;
    }
    if (vencCreated_) {
        ERRORTYPE result = AW_MPI_VENC_ResetChn(config_.vencChannel);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_venc_reset_failed", result),
                        &firstFailure);
        }
        result = AW_MPI_VENC_DestroyChn(config_.vencChannel);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_venc_destroy_failed", result),
                        &firstFailure);
        }
        vencCreated_ = false;
    }
    if (viCreated_) {
        const ERRORTYPE result =
            AW_MPI_VI_DestroyVirChn(config_.vippDevice, config_.viChannel);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_vi_destroy_failed", result),
                        &firstFailure);
        }
        viCreated_ = false;
    }
    if (vippEnabled_) {
        const ERRORTYPE result = AW_MPI_VI_DisableVipp(config_.vippDevice);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_vipp_disable_failed", result),
                        &firstFailure);
        }
        vippEnabled_ = false;
    }
    if (ispRunning_) {
        const ERRORTYPE result = AW_MPI_ISP_Stop(config_.ispDevice);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_isp_stop_failed", result),
                        &firstFailure);
        }
        ispRunning_ = false;
    }
    if (vippCreated_) {
        const ERRORTYPE result = AW_MPI_VI_DestroyVipp(config_.vippDevice);
        if (result != SUCCESS) {
            retainFirst(mppFailure("uvc_vipp_destroy_failed", result),
                        &firstFailure);
        }
        vippCreated_ = false;
    }
    clearFramePool();
    captureFaultPending_.store(false);
    return firstFailure;
}

Status UvcPipeline::initializeFramePool(const Profile& profile) {
    clearFramePool();
    const std::size_t frameCapacity =
        profile.pixelFormat == V4L2_PIX_FMT_YUYV
            ? static_cast<std::size_t>(profile.width) * profile.height * 2U
            : static_cast<std::size_t>(config_.maxFrameBytes);
    try {
        std::lock_guard<std::mutex> lock(frameMutex_);
        for (int index = 0; index < config_.frameQueueDepth; ++index) {
            FrameBlock frame;
            frame.bytes.resize(frameCapacity);
            freeFrames_.push_back(std::move(frame));
        }
    } catch (const std::exception& error) {
        clearFramePool();
        return Status::failure("uvc_frame_pool_failed", error.what());
    }
    return Status::success();
}

void UvcPipeline::clearFramePool() noexcept {
    std::lock_guard<std::mutex> lock(frameMutex_);
    freeFrames_.clear();
    readyFrames_.clear();
}

Status UvcPipeline::drainOneOutputBuffer() {
    FrameBlock frame;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (readyFrames_.empty()) {
            return Status::success();
        }
        frame = std::move(readyFrames_.front());
        readyFrames_.pop_front();
    }

    v4l2_buffer buffer {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (ioctl(deviceFd_, VIDIOC_DQBUF, &buffer) != 0) {
        const int errorNumber = errno;
        enqueueFrameBlock(std::move(frame));
        if (errorNumber == EAGAIN || errorNumber == EWOULDBLOCK) {
            return Status::success();
        }
        errno = errorNumber;
        return systemFailure("uvc_dequeue_buffer_failed");
    }
    if (buffer.index >= gadgetBuffers_.size() ||
        gadgetBuffers_[buffer.index].address == nullptr) {
        recycleFrameBlock(std::move(frame));
        return Status::failure("uvc_buffer_index_invalid",
                               std::to_string(buffer.index));
    }
    GadgetBuffer& output = gadgetBuffers_[buffer.index];
    if (frame.used == 0 || frame.used > output.length) {
        const std::size_t used = frame.used;
        buffer.bytesused = 0;
        (void)ioctl(deviceFd_, VIDIOC_QBUF, &buffer);
        recycleFrameBlock(std::move(frame));
        return Status::failure(
            "uvc_frame_too_large",
            std::to_string(used) + " bytes, gadget buffer " +
                std::to_string(output.length));
    }

    std::memcpy(output.address, frame.bytes.data(), frame.used);
    buffer.bytesused = static_cast<std::uint32_t>(frame.used);
    recycleFrameBlock(std::move(frame));
    if (ioctl(deviceFd_, VIDIOC_QBUF, &buffer) != 0) {
        return systemFailure("uvc_requeue_buffer_failed");
    }
    return Status::success();
}

void UvcPipeline::captureLoop() noexcept {
    try {
        int consecutiveFailures = 0;
        bool waitingForH264Idr =
            currentPixelFormat_.load() == V4L2_PIX_FMT_H264;
        while (!captureStopRequested_.load()) {
            FrameBlock frame;
            if (!acquireFrameBlock(&frame)) {
                std::this_thread::yield();
                continue;
            }

            if (vencCreated_) {
                VENC_PACK_S pack {};
                VENC_STREAM_S stream {};
                stream.mPackCount = 1;
                stream.mpPack = &pack;
                const ERRORTYPE result = AW_MPI_VENC_GetStream(
                    config_.vencChannel, &stream, config_.frameTimeoutMs);
                if (result != SUCCESS) {
                    recycleFrameBlock(std::move(frame));
                    if (captureStopRequested_.load() ||
                        result == ERR_VENC_BUF_EMPTY) {
                        continue;
                    }
                    if (++consecutiveFailures >= 30) {
                        setCaptureFault("uvc_venc_get_stream_failed",
                                        std::to_string(result));
                        break;
                    }
                    continue;
                }

                consecutiveFailures = 0;
                const bool isH264Idr =
                    waitingForH264Idr &&
                    (pack.mDataType.enH264EType == H264E_NALU_ISLICE ||
                     pack.mDataType.enH264EType == H264E_NALU_IPSLICE);
                if (waitingForH264Idr && !isH264Idr) {
                    const ERRORTYPE releaseResult = AW_MPI_VENC_ReleaseStream(
                        config_.vencChannel, &stream);
                    recycleFrameBlock(std::move(frame));
                    droppedFrames_.fetch_add(1);
                    if (releaseResult != SUCCESS) {
                        setCaptureFault("uvc_venc_release_stream_failed",
                                        std::to_string(releaseResult));
                        break;
                    }
                    continue;
                }
                const bool copied = copyEncodedPack(pack, &frame);
                const ERRORTYPE releaseResult = AW_MPI_VENC_ReleaseStream(
                    config_.vencChannel, &stream);
                if (releaseResult != SUCCESS) {
                    recycleFrameBlock(std::move(frame));
                    setCaptureFault("uvc_venc_release_stream_failed",
                                    std::to_string(releaseResult));
                    break;
                }
                if (!copied) {
                    recycleFrameBlock(std::move(frame));
                    setCaptureFault("uvc_encoded_frame_invalid",
                                    "encoded frame exceeds bounded copy buffer");
                    break;
                }
                waitingForH264Idr = false;
                enqueueFrameBlock(std::move(frame));
                continue;
            }

            VIDEO_FRAME_INFO_S source {};
            const ERRORTYPE result = AW_MPI_VI_GetFrame(
                config_.vippDevice, config_.viChannel, &source,
                config_.frameTimeoutMs);
            if (result != SUCCESS) {
                recycleFrameBlock(std::move(frame));
                if (captureStopRequested_.load() || result == ERR_VI_BUF_EMPTY) {
                    continue;
                }
                if (++consecutiveFailures >= 30) {
                    setCaptureFault("uvc_vi_get_frame_failed",
                                    std::to_string(result));
                    break;
                }
                continue;
            }

            consecutiveFailures = 0;
            const bool converted = convertNv21ToYuyv(source, &frame);
            const ERRORTYPE releaseResult = AW_MPI_VI_ReleaseFrame(
                config_.vippDevice, config_.viChannel, &source);
            if (releaseResult != SUCCESS) {
                recycleFrameBlock(std::move(frame));
                setCaptureFault("uvc_vi_release_frame_failed",
                                std::to_string(releaseResult));
                break;
            }
            if (!converted) {
                recycleFrameBlock(std::move(frame));
                setCaptureFault("uvc_yuyv_conversion_failed",
                                "VI frame is not compatible NV21");
                break;
            }
            enqueueFrameBlock(std::move(frame));
        }
    } catch (const std::exception& error) {
        setCaptureFault("uvc_capture_exception", error.what());
    } catch (...) {
        setCaptureFault("uvc_capture_exception", "unknown exception");
    }
}

bool UvcPipeline::acquireFrameBlock(FrameBlock* output) noexcept {
    try {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if (!freeFrames_.empty()) {
            *output = std::move(freeFrames_.front());
            freeFrames_.pop_front();
        } else {
            const auto discardable = std::find_if(
                readyFrames_.begin(), readyFrames_.end(),
                [](const FrameBlock& frame) { return !frame.preserveUntilSent; });
            if (discardable == readyFrames_.end()) {
                return false;
            }
            *output = std::move(*discardable);
            readyFrames_.erase(discardable);
            droppedFrames_.fetch_add(1);
        }
        output->used = 0;
        output->preserveUntilSent = false;
        return true;
    } catch (...) {
        return false;
    }
}

void UvcPipeline::enqueueFrameBlock(FrameBlock frame) noexcept {
    try {
        std::lock_guard<std::mutex> lock(frameMutex_);
        readyFrames_.push_back(std::move(frame));
    } catch (...) {
        droppedFrames_.fetch_add(1);
    }
}

void UvcPipeline::recycleFrameBlock(FrameBlock frame) noexcept {
    frame.used = 0;
    frame.preserveUntilSent = false;
    try {
        std::lock_guard<std::mutex> lock(frameMutex_);
        freeFrames_.push_back(std::move(frame));
    } catch (...) {
    }
}

bool UvcPipeline::copyEncodedPack(const VENC_PACK_S& pack,
                                  FrameBlock* frame) noexcept {
    std::size_t offset = 0;
    if (!appendPart(pack.mpAddr0, pack.mLen0, frame, &offset) ||
        !appendPart(pack.mpAddr1, pack.mLen1, frame, &offset) ||
        !appendPart(pack.mpAddr2, pack.mLen2, frame, &offset) || offset == 0) {
        return false;
    }
    frame->used = offset;
    return true;
}

bool UvcPipeline::convertNv21ToYuyv(const VIDEO_FRAME_INFO_S& source,
                                    FrameBlock* frame) noexcept {
    const int width = currentWidth_.load();
    const int height = currentHeight_.load();
    if (width <= 0 || height <= 0 || (width % 2) != 0 ||
        source.VFrame.mWidth != static_cast<unsigned int>(width) ||
        source.VFrame.mHeight != static_cast<unsigned int>(height) ||
        source.VFrame.mPixelFormat != MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420 ||
        source.VFrame.mpVirAddr[0] == nullptr ||
        source.VFrame.mpVirAddr[1] == nullptr ||
        frame->bytes.size() < static_cast<std::size_t>(width) * height * 2U) {
        return false;
    }

    const auto* yPlane =
        static_cast<const unsigned char*>(source.VFrame.mpVirAddr[0]);
    const auto* vuPlane =
        static_cast<const unsigned char*>(source.VFrame.mpVirAddr[1]);
    const std::size_t yStride = source.VFrame.mStride[0] != 0
                                    ? source.VFrame.mStride[0]
                                    : static_cast<unsigned int>(width);
    const std::size_t vuStride = source.VFrame.mStride[1] != 0
                                     ? source.VFrame.mStride[1]
                                     : static_cast<unsigned int>(width);
    unsigned char* destination = frame->bytes.data();
    for (int row = 0; row < height; ++row) {
        const unsigned char* y = yPlane + static_cast<std::size_t>(row) * yStride;
        const unsigned char* vu =
            vuPlane + static_cast<std::size_t>(row / 2) * vuStride;
        unsigned char* output =
            destination + static_cast<std::size_t>(row) * width * 2U;
        for (int column = 0; column < width; column += 2) {
            output[column * 2 + 0] = y[column];
            output[column * 2 + 1] = vu[column + 1];
            output[column * 2 + 2] = y[column + 1];
            output[column * 2 + 3] = vu[column];
        }
    }
    frame->used = static_cast<std::size_t>(width) * height * 2U;
    return true;
}

void UvcPipeline::setCaptureFault(std::string code,
                                  std::string detail) noexcept {
    try {
        std::lock_guard<std::mutex> lock(faultMutex_);
        captureFaultCode_ = std::move(code);
        captureFaultDetail_ = std::move(detail);
    } catch (...) {
    }
    captureStopRequested_.store(true);
    captureFaultPending_.store(true);
}

void UvcPipeline::handleCaptureFault() noexcept {
    if (!captureFaultPending_.exchange(false)) {
        return;
    }
    std::string code = "uvc_capture_failed";
    std::string detail;
    try {
        std::lock_guard<std::mutex> lock(faultMutex_);
        code = captureFaultCode_.empty() ? code : captureFaultCode_;
        detail = captureFaultDetail_;
    } catch (...) {
    }
    (void)stopStreaming(true);
    postFault(std::move(code), std::move(detail));
}

void UvcPipeline::postFault(std::string code, std::string detail) noexcept {
    state_.store(UvcState::Fault);
    mailbox_.postCritical({PipelineEventType::UvcFault, std::move(code),
                           std::move(detail)});
}

void UvcPipeline::postStateEvent(PipelineEventType type, const char* code,
                                 const std::string& detail) noexcept {
    mailbox_.post({type, code, detail});
}

ERRORTYPE UvcPipeline::viCallback(void* cookie, MPP_CHN_S* channel,
                                  MPP_EVENT_TYPE event, void*) {
    auto* self = static_cast<UvcPipeline*>(cookie);
    if (self != nullptr && channel != nullptr &&
        channel->mModId == MOD_ID_VIU && event == MPP_EVENT_VI_TIMEOUT) {
        self->mailbox_.post({PipelineEventType::ViTimeout, "uvc_vi_timeout",
                             "UVC VI timed out"});
    }
    return SUCCESS;
}

ERRORTYPE UvcPipeline::vencCallback(void* cookie, MPP_CHN_S* channel,
                                      MPP_EVENT_TYPE event, void* eventData) {
    auto* self = static_cast<UvcPipeline*>(cookie);
    if (self == nullptr || channel == nullptr || channel->mModId != MOD_ID_VENC ||
        event != MPP_EVENT_LINKAGE_ISP2VE_PARAM) {
        return SUCCESS;
    }
    const ERRORTYPE result = updateVencIspParameters(
        self->config_.vippDevice, channel->mChnId, eventData);
    if (result != SUCCESS) {
        self->setCaptureFault("uvc_isp_link_failed", std::to_string(result));
    }
    return result;
}

}  // namespace mpp
}  // namespace camera
