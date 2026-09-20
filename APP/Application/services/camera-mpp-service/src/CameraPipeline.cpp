#include "camera/mpp/CameraPipeline.hpp"

#include <cstring>
#include <utility>
#include <unistd.h>

namespace camera {
namespace mpp {
namespace {

Status mppFailure(const char* operation, ERRORTYPE result) {
    return Status::failure(operation, std::to_string(result));
}

CameraConfig mediaCameraConfig(const CameraConfig& preview) {
    CameraConfig media = preview;
    media.vippDevice = preview.mediaVippDevice;
    media.captureWidth = preview.mediaCaptureWidth;
    media.captureHeight = preview.mediaCaptureHeight;
    return media;
}

VI_ATTR_S captureAttributes(const CameraConfig& camera) {
    VI_ATTR_S attributes;
    std::memset(&attributes, 0, sizeof(attributes));
    attributes.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    attributes.memtype = V4L2_MEMORY_MMAP;
    // Match sample_virvi2vo's map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(NV21).
    // NV21M is a different vendor format and skips this SDK's NV21 stride setup.
    attributes.format.pixelformat = V4L2_PIX_FMT_NV21;
    attributes.format.field = V4L2_FIELD_NONE;
    attributes.format.colorspace = V4L2_COLORSPACE_JPEG;
    attributes.format.width = camera.captureWidth;
    attributes.format.height = camera.captureHeight;
    attributes.nbufs = camera.viBufferCount;
    attributes.nplanes = 2;
    attributes.drop_frame_num = 0;
    attributes.mbEncppEnable = TRUE;
    attributes.use_current_win = 0;
    attributes.fps = camera.frameRate;
    return attributes;
}

}  // namespace

CameraPipeline::CameraPipeline(const ServiceConfig& config, EventMailbox& mailbox)
    : config_(config),
      mailbox_(mailbox),
      snapshot_(mediaCameraConfig(config.camera), config.snapshot),
      record_(mediaCameraConfig(config.camera), config.record, config.mediaRoot,
              config.requireMediaRootMount != 0, mailbox),
      rtsp_(mediaCameraConfig(config.camera), config.rtsp, mailbox) {}

CameraPipeline::~CameraPipeline() {
    stop();
}

Status CameraPipeline::start() {
    if (active_) {
        return Status::success();
    }
    if (vippCreated_ || mediaVippCreated_ || previewChannelCreated_ || display_) {
        return Status::failure("camera_dirty_state", "camera resources remain allocated");
    }

    ERRORTYPE result = AW_MPI_VI_CreateVipp(config_.camera.vippDevice);
    if (result != SUCCESS) {
        return mppFailure("camera_vipp_create_failed", result);
    }
    vippCreated_ = true;

    MPPCallbackInfo callbackInfo;
    callbackInfo.cookie = this;
    callbackInfo.callback = &CameraPipeline::callback;
    result = AW_MPI_VI_RegisterCallback(config_.camera.vippDevice, &callbackInfo);
    if (result != SUCCESS) {
        const Status status = mppFailure("camera_vi_callback_failed", result);
        stop();
        return status;
    }

    VI_ATTR_S attributes = captureAttributes(config_.camera);
    result = AW_MPI_VI_SetVippAttr(config_.camera.vippDevice, &attributes);
    if (result != SUCCESS) {
        const Status status = mppFailure("camera_vipp_config_failed", result);
        stop();
        return status;
    }

    result = AW_MPI_ISP_Run(config_.camera.ispDevice);
    if (result != SUCCESS) {
        const Status status = mppFailure("camera_isp_start_failed", result);
        stop();
        return status;
    }
    ispRunning_ = true;

    result = AW_MPI_VI_CreateVirChn(config_.camera.vippDevice,
                                    config_.camera.previewChannel, nullptr);
    if (result != SUCCESS) {
        const Status status = mppFailure("camera_preview_vi_create_failed", result);
        stop();
        return status;
    }
    previewChannelCreated_ = true;

    result = AW_MPI_VI_EnableVipp(config_.camera.vippDevice);
    if (result != SUCCESS) {
        const Status status = mppFailure("camera_vipp_enable_failed", result);
        stop();
        return status;
    }
    vippEnabled_ = true;

    const DisplayConfig previewDisplay = DisplayOutput::fitWithin(
        config_.display, config_.camera.captureWidth,
        config_.camera.captureHeight);
    display_.reset(new DisplayOutput(previewDisplay));
    const Status createDisplay =
        display_->create(MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420, &callbackInfo);
    if (!createDisplay.ok) {
        stop();
        return createDisplay;
    }

    MPP_CHN_S vi = {MOD_ID_VIU, config_.camera.vippDevice,
                    config_.camera.previewChannel};
    MPP_CHN_S vo = display_->mppChannel();
    result = AW_MPI_SYS_Bind(&vi, &vo);
    if (result != SUCCESS) {
        const Status status = mppFailure("camera_bind_preview_failed", result);
        stop();
        return status;
    }
    previewBound_ = true;

    result = AW_MPI_VI_EnableVirChn(config_.camera.vippDevice,
                                    config_.camera.previewChannel);
    if (result != SUCCESS) {
        const Status status = mppFailure("camera_preview_vi_enable_failed", result);
        stop();
        return status;
    }
    previewEnabled_ = true;

    const Status displayStatus = display_->start();
    if (!displayStatus.ok) {
        stop();
        return displayStatus;
    }
    active_ = true;
    return Status::success();
}

Status CameraPipeline::ensureMediaCapture() {
    // Snapshot runs in its own worker and may overlap RTSP startup.
    std::lock_guard<std::mutex> lock(mediaCaptureMutex_);
    if (mediaVippEnabled_) return Status::success();
    if (mediaVippCreated_)
        return Status::failure("camera_media_dirty_state", "media VIPP cleanup is incomplete");

    const std::string node = "/dev/video" + std::to_string(config_.camera.mediaVippDevice);
    if (::access(node.c_str(), F_OK) != 0)
        return Status::failure("camera_media_unavailable",
                               node + " is unavailable; enable the configured VIPP node");

    ERRORTYPE result = AW_MPI_VI_CreateVipp(config_.camera.mediaVippDevice);
    if (result != SUCCESS) return mppFailure("camera_media_vipp_create_failed", result);
    mediaVippCreated_ = true;
    const auto rollback = [this](Status status) {
        const ERRORTYPE destroy = AW_MPI_VI_DestroyVipp(config_.camera.mediaVippDevice);
        if (destroy == SUCCESS) {
            mediaVippCreated_ = false;
        } else {
            status.detail += "; media VIPP rollback failed: " + std::to_string(destroy);
        }
        return status;
    };

    MPPCallbackInfo callbackInfo {};
    callbackInfo.cookie = this;
    callbackInfo.callback = &CameraPipeline::callback;
    result = AW_MPI_VI_RegisterCallback(config_.camera.mediaVippDevice, &callbackInfo);
    if (result != SUCCESS) return rollback(mppFailure("camera_media_callback_failed", result));

    VI_ATTR_S attributes = captureAttributes(mediaCameraConfig(config_.camera));
    attributes.use_current_win = 1; // share the already-running ISP0 sensor window
    result = AW_MPI_VI_SetVippAttr(config_.camera.mediaVippDevice, &attributes);
    if (result != SUCCESS) return rollback(mppFailure("camera_media_vipp_config_failed", result));
    result = AW_MPI_VI_EnableVipp(config_.camera.mediaVippDevice);
    if (result != SUCCESS) return rollback(mppFailure("camera_media_vipp_enable_failed", result));
    mediaVippEnabled_ = true;
    return Status::success();
}

Status CameraPipeline::stop() noexcept {
    Status firstFailure = Status::success();
    const auto retain = [&firstFailure](Status status) {
        if (firstFailure.ok && !status.ok) {
            firstFailure = std::move(status);
        }
    };

    active_ = false;
    retain(rtsp_.stop());
    if (record_.active()) {
        retain(record_.stop());
    } else {
        retain(record_.abort());
    }
    // Stop the producer before VO drains. Stopping VO first allows a late
    // VI frame into its Idle queue; UnBind then clears the tunnel handle and
    // DestroyChn tries to return that frame through a null handle.
    if (previewEnabled_) {
        const ERRORTYPE result = AW_MPI_VI_DisableVirChn(
            config_.camera.vippDevice, config_.camera.previewChannel);
        if (result != SUCCESS) {
            retain(mppFailure("camera_preview_vi_disable_failed", result));
        }
        previewEnabled_ = false;
    }
    if (display_) {
        retain(display_->stop());
    }
    if (previewBound_ && display_) {
        MPP_CHN_S vi = {MOD_ID_VIU, config_.camera.vippDevice,
                        config_.camera.previewChannel};
        MPP_CHN_S vo = display_->mppChannel();
        const ERRORTYPE result = AW_MPI_SYS_UnBind(&vi, &vo);
        if (result != SUCCESS) {
            retain(mppFailure("camera_unbind_preview_failed", result));
        }
        previewBound_ = false;
    }
    if (display_) {
        retain(display_->destroy());
        display_.reset();
    }
    if (previewChannelCreated_) {
        const ERRORTYPE result = AW_MPI_VI_DestroyVirChn(
            config_.camera.vippDevice, config_.camera.previewChannel);
        if (result != SUCCESS) {
            retain(mppFailure("camera_preview_vi_destroy_failed", result));
        }
        previewChannelCreated_ = false;
    }
    if (mediaVippEnabled_) {
        const ERRORTYPE result = AW_MPI_VI_DisableVipp(config_.camera.mediaVippDevice);
        if (result != SUCCESS)
            retain(mppFailure("camera_media_vipp_disable_failed", result));
        mediaVippEnabled_ = false;
    }
    if (vippEnabled_) {
        const ERRORTYPE result = AW_MPI_VI_DisableVipp(config_.camera.vippDevice);
        if (result != SUCCESS) {
            retain(mppFailure("camera_vipp_disable_failed", result));
        }
        vippEnabled_ = false;
    }
    if (ispRunning_) {
        const ERRORTYPE result = AW_MPI_ISP_Stop(config_.camera.ispDevice);
        if (result != SUCCESS) {
            retain(mppFailure("camera_isp_stop_failed", result));
        }
        ispRunning_ = false;
    }
    if (mediaVippCreated_) {
        const ERRORTYPE result = AW_MPI_VI_DestroyVipp(config_.camera.mediaVippDevice);
        if (result != SUCCESS)
            retain(mppFailure("camera_media_vipp_destroy_failed", result));
        mediaVippCreated_ = false;
    }
    if (vippCreated_) {
        const ERRORTYPE result = AW_MPI_VI_DestroyVipp(config_.camera.vippDevice);
        if (result != SUCCESS) {
            retain(mppFailure("camera_vipp_destroy_failed", result));
        }
        vippCreated_ = false;
    }
    return firstFailure;
}

Status CameraPipeline::takeSnapshot(const std::string& path) {
    if (!active_) {
        return Status::failure("camera_not_active", "Camera Preview is not running");
    }
    if (record_.active()) {
        return Status::failure(
            "snapshot_during_record_unsupported",
            "snapshot is disabled while recording; stop recording first");
    }

    const Status mediaStatus = ensureMediaCapture();
    if (!mediaStatus.ok) return mediaStatus;

    // The Preview channel is in tunnel mode while bound to VO.  Use the idle
    // secondary channel for non-tunnel GetFrame/ReleaseFrame instead of
    // assuming that a bound Preview channel can also be pulled directly.
    ERRORTYPE result = AW_MPI_VI_CreateVirChn(
        config_.camera.mediaVippDevice, config_.camera.recordChannel, nullptr);
    if (result != SUCCESS) {
        return mppFailure("snapshot_vi_create_failed", result);
    }

    bool channelEnabled = false;
    Status status = Status::success();
    result = AW_MPI_VI_EnableVirChn(config_.camera.mediaVippDevice,
                                    config_.camera.recordChannel);
    if (result != SUCCESS) {
        status = mppFailure("snapshot_vi_enable_failed", result);
    } else {
        channelEnabled = true;
        status = snapshot_.capture(config_.camera.mediaVippDevice,
                                   config_.camera.recordChannel, path);
    }

    if (channelEnabled) {
        result = AW_MPI_VI_DisableVirChn(config_.camera.mediaVippDevice,
                                         config_.camera.recordChannel);
        if (status.ok && result != SUCCESS) {
            status = mppFailure("snapshot_vi_disable_failed", result);
        }
    }
    result = AW_MPI_VI_DestroyVirChn(config_.camera.mediaVippDevice,
                                     config_.camera.recordChannel);
    if (status.ok && result != SUCCESS) {
        status = mppFailure("snapshot_vi_destroy_failed", result);
    }
    return status;
}

Status CameraPipeline::startRecord(const std::string& path) {
    if (!active_) {
        return Status::failure("camera_not_active", "Camera Preview is not running");
    }
    const Status mediaStatus = ensureMediaCapture();
    if (!mediaStatus.ok) return mediaStatus;
    return record_.start(config_.camera.mediaVippDevice, path);
}

Status CameraPipeline::stopRecord() {
    if (!record_.active()) {
        return Status::failure("record_not_active", "record output is not running");
    }
    return record_.stop();
}

Status CameraPipeline::startRtsp() {
    if (!active_) {
        return Status::failure("camera_not_active", "Camera Preview is not running");
    }
    const Status mediaStatus = ensureMediaCapture();
    if (!mediaStatus.ok) return mediaStatus;
    return rtsp_.start(config_.camera.mediaVippDevice);
}

Status CameraPipeline::stopRtsp() {
    if (!rtsp_.active()) {
        return Status::failure("rtsp_not_active", "RTSP output is not running");
    }
    return rtsp_.stop();
}

ERRORTYPE CameraPipeline::callback(void* cookie, MPP_CHN_S* channel,
                                   MPP_EVENT_TYPE event, void*) {
    auto* self = static_cast<CameraPipeline*>(cookie);
    if (self == nullptr || channel == nullptr) {
        return SUCCESS;
    }
    if (channel->mModId == MOD_ID_VIU && event == MPP_EVENT_VI_TIMEOUT) {
        self->mailbox_.post({PipelineEventType::ViTimeout, "vi_timeout",
                             "camera VI timed out"});
    } else if (channel->mModId == MOD_ID_VOU &&
               event == MPP_EVENT_RENDERING_START) {
        self->mailbox_.post({PipelineEventType::VoRenderingStarted,
                             "preview_rendering", "Camera Preview rendering"});
    }
    return SUCCESS;
}

}  // namespace mpp
}  // namespace camera
