#include "camera/mpp/PlaybackPipeline.hpp"

#include <ClockCompPortIndex.h>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

namespace camera {
namespace mpp {
namespace {

constexpr int kClockVideoPort = CLOCK_PORT_INDEX_VIDEO;

Status mppFailure(const char* operation, ERRORTYPE result) {
    return Status::failure(operation, std::to_string(result));
}

const char* payloadName(PAYLOAD_TYPE_E type) {
    switch (type) {
        case PT_H264:
            return "h264";
        case PT_H265:
            return "h265";
        case PT_MJPEG:
            return "mjpeg";
        default:
            return "unknown";
    }
}

bool codecAllowed(const PlaybackConfig& config, PAYLOAD_TYPE_E type) {
    return (type == PT_H264 && SUNXI_MPP_VDEC_H264 && config.allowH264 != 0) ||
           (type == PT_H265 && SUNXI_MPP_VDEC_H265 && config.allowH265 != 0) ||
           (type == PT_MJPEG && SUNXI_MPP_VDEC_JPEG && config.allowMjpeg != 0);
}

}  // namespace

const char* toString(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle:
            return "idle";
        case PlaybackState::Ready:
            return "ready";
        case PlaybackState::Playing:
            return "playing";
        case PlaybackState::Paused:
            return "paused";
        case PlaybackState::Ended:
            return "ended";
    }
    return "unknown";
}

PlaybackPipeline::PlaybackPipeline(const ServiceConfig& config,
                                   EventMailbox& mailbox)
    : config_(config), mailbox_(mailbox) {}

PlaybackPipeline::~PlaybackPipeline() {
    close();
}

Status PlaybackPipeline::load(const std::string& path) {
    if (state_ != PlaybackState::Idle) {
        return Status::failure("playback_not_idle", "stop current media before load");
    }
    sourceDescriptor_ = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (sourceDescriptor_ < 0) {
        return Status::failure("media_open_failed", path);
    }
    sourcePath_ = path;

    DEMUX_CHN_ATTR_S demux;
    std::memset(&demux, 0, sizeof(demux));
    demux.mSourceType = SOURCETYPE_FD;
    demux.mSourceUrl = nullptr;
    demux.mFd = sourceDescriptor_;
    demux.mDemuxDisableTrack =
        DEMUX_DISABLE_AUDIO_TRACK | DEMUX_DISABLE_SUBTITLE_TRACK;
    ERRORTYPE result =
        AW_MPI_DEMUX_CreateChn(config_.playback.demuxChannel, &demux);
    if (result != SUCCESS) {
        const Status status = mppFailure("demux_create_failed", result);
        close();
        return status;
    }
    demuxCreated_ = true;

    MPPCallbackInfo callbackInfo;
    callbackInfo.cookie = this;
    callbackInfo.callback = &PlaybackPipeline::callback;
    result = AW_MPI_DEMUX_RegisterCallback(
        config_.playback.demuxChannel, &callbackInfo);
    if (result != SUCCESS) {
        const Status status = mppFailure("demux_callback_failed", result);
        close();
        return status;
    }

    std::memset(&mediaInfo_, 0, sizeof(mediaInfo_));
    result = AW_MPI_DEMUX_GetMediaInfo(
        config_.playback.demuxChannel, &mediaInfo_);
    if (result != SUCCESS) {
        const Status status = mppFailure("demux_media_info_failed", result);
        close();
        return status;
    }
    if (mediaInfo_.mVideoNum <= 0 || mediaInfo_.mVideoIndex < 0 ||
        mediaInfo_.mVideoIndex >= mediaInfo_.mVideoNum ||
        mediaInfo_.mVideoNum > DEMUX_MAX_VIDEO_STREAM_NUM) {
        close();
        return Status::failure("media_has_no_video", path);
    }
    const auto& stream = mediaInfo_.mVideoStreamInfo[mediaInfo_.mVideoIndex];
    if (!codecAllowed(config_.playback, stream.mCodecType)) {
        const std::string detail = std::string(payloadName(stream.mCodecType)) +
                                   " (id=" +
                                   std::to_string(static_cast<int>(stream.mCodecType)) +
                                   ")";
        close();
        return Status::failure("media_codec_unsupported", detail);
    }
    const bool landscape720p =
        stream.mWidth <= config_.playback.maxSourceWidth &&
        stream.mHeight <= config_.playback.maxSourceHeight;
    const bool portrait720p =
        stream.mWidth <= config_.playback.maxSourceHeight &&
        stream.mHeight <= config_.playback.maxSourceWidth;
    if (stream.mWidth <= 0 || stream.mHeight <= 0 ||
        (!landscape720p && !portrait720p)) {
        const std::string detail = std::to_string(stream.mWidth) + "x" +
                                   std::to_string(stream.mHeight);
        close();
        return Status::failure("media_size_unsupported", detail);
    }
    if (stream.mFrameRate > config_.playback.maxFrameRate * 1000) {
        const std::string detail = std::to_string(stream.mFrameRate) +
                                   " milli-fps";
        close();
        return Status::failure("media_frame_rate_unsupported", detail);
    }
    const int reportedBitRate = std::max(stream.mAvgBitsRate, stream.mMaxBitsRate);
    if (reportedBitRate > config_.playback.maxBitRate) {
        const std::string detail = std::to_string(reportedBitRate) + " bit/s";
        close();
        return Status::failure("media_bit_rate_unsupported", detail);
    }

    VDEC_CHN_ATTR_S decoder;
    std::memset(&decoder, 0, sizeof(decoder));
    decoder.mPicWidth = stream.mWidth;
    decoder.mPicHeight = stream.mHeight;
    decoder.mInitRotation = ROTATE_NONE;
    decoder.mOutputPixelFormat = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    decoder.mType = stream.mCodecType;
    decoder.mVdecVideoAttr.mSupportBFrame = config_.playback.supportBFrames;
    decoder.mVdecVideoAttr.mMode = VIDEO_MODE_FRAME;
    result = AW_MPI_VDEC_CreateChn(config_.playback.vdecChannel, &decoder);
    if (result != SUCCESS) {
        const Status status = mppFailure("vdec_create_failed", result);
        close();
        return status;
    }
    vdecCreated_ = true;
    if (config_.playback.veFrequencyMHz > 0) {
        result = AW_MPI_VDEC_SetVEFreq(config_.playback.vdecChannel,
                                      config_.playback.veFrequencyMHz);
        if (result != SUCCESS) {
            const Status status = mppFailure("vdec_frequency_failed", result);
            close();
            return status;
        }
    }
    result = AW_MPI_VDEC_ForceFramePackage(
        config_.playback.vdecChannel,
        config_.playback.forceFramePackage != 0 ? TRUE : FALSE);
    if (result != SUCCESS) {
        const Status status = mppFailure("vdec_frame_package_failed", result);
        close();
        return status;
    }
    result = AW_MPI_VDEC_RegisterCallback(
        config_.playback.vdecChannel, &callbackInfo);
    if (result != SUCCESS) {
        const Status status = mppFailure("vdec_callback_failed", result);
        close();
        return status;
    }

    DisplayConfig playbackCanvas = config_.display;
    playbackCanvas.x = config_.playback.displayX;
    playbackCanvas.y = config_.playback.displayY;
    playbackCanvas.width = config_.playback.displayWidth;
    playbackCanvas.height = config_.playback.displayHeight;
    fittedDisplay_ = DisplayOutput::fitWithin(
        playbackCanvas, stream.mWidth, stream.mHeight);
    display_.reset(new DisplayOutput(fittedDisplay_));
    Status status = display_->create(
        MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420, &callbackInfo);
    if (!status.ok) {
        close();
        return status;
    }

    CLOCK_CHN_ATTR_S clock;
    std::memset(&clock, 0, sizeof(clock));
    clock.nWaitMask = 1 << kClockVideoPort;
    result = AW_MPI_CLOCK_CreateChn(config_.playback.clockChannel, &clock);
    if (result != SUCCESS) {
        status = mppFailure("clock_create_failed", result);
        close();
        return status;
    }
    clockCreated_ = true;

    MPP_CHN_S demuxChannel = {MOD_ID_DEMUX, 0, config_.playback.demuxChannel};
    MPP_CHN_S vdecChannel = {MOD_ID_VDEC, 0, config_.playback.vdecChannel};
    MPP_CHN_S voChannel = display_->mppChannel();
    MPP_CHN_S clockChannel = {MOD_ID_CLOCK, 0, config_.playback.clockChannel};
    result = AW_MPI_SYS_Bind(&demuxChannel, &vdecChannel);
    if (result != SUCCESS) {
        status = mppFailure("bind_demux_vdec_failed", result);
        close();
        return status;
    }
    demuxVdecBound_ = true;
    result = AW_MPI_SYS_Bind(&vdecChannel, &voChannel);
    if (result != SUCCESS) {
        status = mppFailure("bind_vdec_vo_failed", result);
        close();
        return status;
    }
    vdecVoBound_ = true;
    result = AW_MPI_SYS_Bind(&clockChannel, &demuxChannel);
    if (result != SUCCESS) {
        status = mppFailure("bind_clock_demux_failed", result);
        close();
        return status;
    }
    clockDemuxBound_ = true;
    result = AW_MPI_SYS_Bind(&clockChannel, &voChannel);
    if (result != SUCCESS) {
        status = mppFailure("bind_clock_vo_failed", result);
        close();
        return status;
    }
    clockVoBound_ = true;
    state_ = PlaybackState::Ready;
    return Status::success();
}

Status PlaybackPipeline::play() {
    if (state_ != PlaybackState::Ready) {
        return Status::failure("playback_not_ready", toString(state_));
    }
    ERRORTYPE result = AW_MPI_CLOCK_Start(config_.playback.clockChannel);
    if (result != SUCCESS) {
        return mppFailure("clock_start_failed", result);
    }
    clockStarted_ = true;
    result = AW_MPI_VDEC_StartRecvStream(config_.playback.vdecChannel);
    if (result != SUCCESS) {
        const Status status = mppFailure("vdec_start_failed", result);
        stopStreams();
        return status;
    }
    vdecStarted_ = true;
    const Status displayStatus = display_->start();
    if (!displayStatus.ok) {
        stopStreams();
        return displayStatus;
    }
    result = AW_MPI_DEMUX_Start(config_.playback.demuxChannel);
    if (result != SUCCESS) {
        const Status status = mppFailure("demux_start_failed", result);
        stopStreams();
        return status;
    }
    demuxStarted_ = true;
    state_ = PlaybackState::Playing;
    return Status::success();
}

Status PlaybackPipeline::pause() {
    if (state_ != PlaybackState::Playing) {
        return Status::failure("playback_not_playing", toString(state_));
    }
    Status firstFailure = Status::success();
    const auto check = [&firstFailure](const char* operation, ERRORTYPE result) {
        if (firstFailure.ok && result != SUCCESS) {
            firstFailure = mppFailure(operation, result);
        }
    };
    check("demux_pause_failed", AW_MPI_DEMUX_Pause(config_.playback.demuxChannel));
    check("vdec_pause_failed", AW_MPI_VDEC_Pause(config_.playback.vdecChannel));
    check("clock_pause_failed", AW_MPI_CLOCK_Pause(config_.playback.clockChannel));
    const Status displayStatus = display_->pause();
    if (firstFailure.ok && !displayStatus.ok) {
        firstFailure = displayStatus;
    }
    if (firstFailure.ok) {
        state_ = PlaybackState::Paused;
    } else {
        // A partial pause leaves vendor components in incompatible states.
        // Tear the entire pipeline down so the next command starts cleanly.
        close();
    }
    return firstFailure;
}

Status PlaybackPipeline::resume() {
    if (state_ != PlaybackState::Paused) {
        return Status::failure("playback_not_paused", toString(state_));
    }
    Status firstFailure = Status::success();
    const auto check = [&firstFailure](const char* operation, ERRORTYPE result) {
        if (firstFailure.ok && result != SUCCESS) {
            firstFailure = mppFailure(operation, result);
        }
    };
    // This MPP exposes Start rather than a separate Resume for DEMUX/CLOCK.
    check("clock_resume_failed", AW_MPI_CLOCK_Start(config_.playback.clockChannel));
    check("vdec_resume_failed", AW_MPI_VDEC_Resume(config_.playback.vdecChannel));
    const Status displayStatus = display_->resume();
    if (firstFailure.ok && !displayStatus.ok) {
        firstFailure = displayStatus;
    }
    check("demux_resume_failed", AW_MPI_DEMUX_Start(config_.playback.demuxChannel));
    if (firstFailure.ok) {
        state_ = PlaybackState::Playing;
    } else {
        close();
    }
    return firstFailure;
}

Status PlaybackPipeline::stop() {
    if (state_ == PlaybackState::Idle || state_ == PlaybackState::Ready) {
        return Status::success();
    }
    const Status status = stopStreams();
    if (status.ok) {
        state_ = PlaybackState::Ready;
    }
    return status;
}

Status PlaybackPipeline::stopStreams() noexcept {
    Status firstFailure = Status::success();
    const auto retain = [&firstFailure](Status status) {
        if (firstFailure.ok && !status.ok) {
            firstFailure = std::move(status);
        }
    };
    // Stop producers before draining VO. This SDK accepts incoming frames even
    // while VO is Idle; stopping VO first can leave a late VDEC frame queued.
    // UnBind then clears its tunnel pointer, and VO Destroy dereferences that
    // pointer when returning the late frame. Keep the tunnel until VO is drained.
    if (demuxStarted_) {
        const ERRORTYPE result = AW_MPI_DEMUX_Stop(config_.playback.demuxChannel);
        if (result != SUCCESS) {
            retain(mppFailure("demux_stop_failed", result));
        }
        demuxStarted_ = false;
    }
    if (vdecStarted_) {
        const ERRORTYPE result =
            AW_MPI_VDEC_StopRecvStream(config_.playback.vdecChannel);
        if (result != SUCCESS) {
            retain(mppFailure("vdec_stop_failed", result));
        }
        vdecStarted_ = false;
    }
    if (display_ && display_->started()) {
        const Status stopped = display_->stop();
        retain(stopped);
        if (stopped.ok) {
            // At EOF this SDK moves VO to Idle without returning its two
            // displayed frames. StopChn on an already-Idle VO is a no-op, and
            // Seek deliberately retains those frames. With both producers
            // stopped and all tunnels intact, clear EOF and perform a real
            // Executing -> Idle transition to return them before UnBind.
            const ERRORTYPE cleared = AW_MPI_VO_SetStreamEof(
                config_.display.videoLayer, config_.display.videoChannel, FALSE);
            if (cleared != SUCCESS) {
                retain(mppFailure("vo_clear_eof_failed", cleared));
            } else {
                const Status restarted = display_->start();
                retain(restarted);
                if (restarted.ok) {
                    retain(display_->stop());
                }
            }
        }
    }
    if (clockStarted_) {
        const ERRORTYPE result = AW_MPI_CLOCK_Stop(config_.playback.clockChannel);
        if (result != SUCCESS) {
            retain(mppFailure("clock_stop_failed", result));
        }
        clockStarted_ = false;
    }
    return firstFailure;
}

Status PlaybackPipeline::close() noexcept {
    Status firstFailure = stopStreams();
    const auto retain = [&firstFailure](Status status) {
        if (firstFailure.ok && !status.ok) {
            firstFailure = std::move(status);
        }
    };

    MPP_CHN_S demuxChannel = {MOD_ID_DEMUX, 0, config_.playback.demuxChannel};
    MPP_CHN_S vdecChannel = {MOD_ID_VDEC, 0, config_.playback.vdecChannel};
    MPP_CHN_S clockChannel = {MOD_ID_CLOCK, 0, config_.playback.clockChannel};
    MPP_CHN_S voChannel{};
    if (display_) {
        voChannel = display_->mppChannel();
    }
    if (clockVoBound_) {
        const ERRORTYPE result = AW_MPI_SYS_UnBind(&clockChannel, &voChannel);
        if (result != SUCCESS) {
            retain(mppFailure("unbind_clock_vo_failed", result));
        }
        clockVoBound_ = false;
    }
    if (clockDemuxBound_) {
        const ERRORTYPE result = AW_MPI_SYS_UnBind(&clockChannel, &demuxChannel);
        if (result != SUCCESS) {
            retain(mppFailure("unbind_clock_demux_failed", result));
        }
        clockDemuxBound_ = false;
    }
    if (vdecVoBound_) {
        const ERRORTYPE result = AW_MPI_SYS_UnBind(&vdecChannel, &voChannel);
        if (result != SUCCESS) {
            retain(mppFailure("unbind_vdec_vo_failed", result));
        }
        vdecVoBound_ = false;
    }
    if (demuxVdecBound_) {
        const ERRORTYPE result = AW_MPI_SYS_UnBind(&demuxChannel, &vdecChannel);
        if (result != SUCCESS) {
            retain(mppFailure("unbind_demux_vdec_failed", result));
        }
        demuxVdecBound_ = false;
    }
    if (display_) {
        retain(display_->destroy());
        display_.reset();
    }
    if (vdecCreated_) {
        const ERRORTYPE result =
            AW_MPI_VDEC_DestroyChn(config_.playback.vdecChannel);
        if (result != SUCCESS) {
            retain(mppFailure("vdec_destroy_failed", result));
        }
        vdecCreated_ = false;
    }
    if (demuxCreated_) {
        const ERRORTYPE result =
            AW_MPI_DEMUX_DestroyChn(config_.playback.demuxChannel);
        if (result != SUCCESS) {
            retain(mppFailure("demux_destroy_failed", result));
        }
        demuxCreated_ = false;
    }
    if (clockCreated_) {
        const ERRORTYPE result =
            AW_MPI_CLOCK_DestroyChn(config_.playback.clockChannel);
        if (result != SUCCESS) {
            retain(mppFailure("clock_destroy_failed", result));
        }
        clockCreated_ = false;
    }
    if (sourceDescriptor_ >= 0) {
        ::close(sourceDescriptor_);
        sourceDescriptor_ = -1;
    }
    sourcePath_.clear();
    std::memset(&mediaInfo_, 0, sizeof(mediaInfo_));
    state_ = PlaybackState::Idle;
    return firstFailure;
}

void PlaybackPipeline::markEnded() {
    if (state_ == PlaybackState::Playing || state_ == PlaybackState::Paused) {
        state_ = PlaybackState::Ended;
    }
}

int PlaybackPipeline::positionMs() const {
    if (!clockCreated_) {
        return 0;
    }
    int position = 0;
    return AW_MPI_CLOCK_GetCurrentMediaTime(
               config_.playback.clockChannel, &position) == SUCCESS
               ? position
               : -1;
}

int PlaybackPipeline::sourceWidth() const {
    return mediaInfo_.mVideoNum > 0
               ? mediaInfo_.mVideoStreamInfo[mediaInfo_.mVideoIndex].mWidth
               : 0;
}

int PlaybackPipeline::sourceHeight() const {
    return mediaInfo_.mVideoNum > 0
               ? mediaInfo_.mVideoStreamInfo[mediaInfo_.mVideoIndex].mHeight
               : 0;
}

int PlaybackPipeline::sourceFrameRateMilliFps() const {
    return mediaInfo_.mVideoNum > 0
               ? mediaInfo_.mVideoStreamInfo[mediaInfo_.mVideoIndex].mFrameRate
               : 0;
}

int PlaybackPipeline::sourceAverageBitRate() const {
    return mediaInfo_.mVideoNum > 0
               ? mediaInfo_.mVideoStreamInfo[mediaInfo_.mVideoIndex].mAvgBitsRate
               : 0;
}

int PlaybackPipeline::sourceMaximumBitRate() const {
    return mediaInfo_.mVideoNum > 0
               ? mediaInfo_.mVideoStreamInfo[mediaInfo_.mVideoIndex].mMaxBitsRate
               : 0;
}

int PlaybackPipeline::codec() const {
    return mediaInfo_.mVideoNum > 0
               ? static_cast<int>(
                     mediaInfo_.mVideoStreamInfo[mediaInfo_.mVideoIndex].mCodecType)
               : -1;
}

std::string PlaybackPipeline::codecName() const {
    return mediaInfo_.mVideoNum > 0
               ? payloadName(
                     mediaInfo_.mVideoStreamInfo[mediaInfo_.mVideoIndex].mCodecType)
               : "unknown";
}

ERRORTYPE PlaybackPipeline::callback(void* cookie, MPP_CHN_S* channel,
                                     MPP_EVENT_TYPE event, void*) {
    auto* self = static_cast<PlaybackPipeline*>(cookie);
    if (self == nullptr || channel == nullptr) {
        return SUCCESS;
    }
    if (channel->mModId == MOD_ID_DEMUX && event == MPP_EVENT_NOTIFY_EOF) {
        AW_MPI_VDEC_SetStreamEof(self->config_.playback.vdecChannel, TRUE);
    } else if (channel->mModId == MOD_ID_VDEC &&
               event == MPP_EVENT_NOTIFY_EOF) {
        AW_MPI_VO_SetStreamEof(self->config_.display.videoLayer,
                              self->config_.display.videoChannel, TRUE);
    } else if (channel->mModId == MOD_ID_VOU &&
               event == MPP_EVENT_NOTIFY_EOF) {
        self->mailbox_.post({PipelineEventType::PlaybackEof, "playback_eof",
                             ""});
    } else if (channel->mModId == MOD_ID_VOU &&
               event == MPP_EVENT_RENDERING_START) {
        self->mailbox_.post({PipelineEventType::VoRenderingStarted,
                             "playback_rendering", "Playback rendering"});
    }
    return SUCCESS;
}

}  // namespace mpp
}  // namespace camera
