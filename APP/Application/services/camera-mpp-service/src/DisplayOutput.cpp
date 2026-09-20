#include "camera/mpp/DisplayOutput.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <unistd.h>

namespace camera {
namespace mpp {
namespace {

Status mppFailure(const char* operation, ERRORTYPE result) {
    return Status::failure(operation, std::to_string(result));
}

VO_INTF_TYPE_E interfaceType(const std::string& value) {
    if (value == "hdmi") {
        return static_cast<VO_INTF_TYPE_E>(VO_INTF_HDMI);
    }
    if (value == "cvbs") {
        return static_cast<VO_INTF_TYPE_E>(VO_INTF_CVBS);
    }
    return static_cast<VO_INTF_TYPE_E>(VO_INTF_LCD);
}

VO_INTF_SYNC_E interfaceSync(const std::string& value) {
    if (value == "720p60") {
        return VO_OUTPUT_720P60;
    }
    if (value == "1080p30") {
        return VO_OUTPUT_1080P30;
    }
    if (value == "2160p30") {
        return VO_OUTPUT_3840x2160_30;
    }
    return VO_OUTPUT_NTSC;
}

}  // namespace

DisplayOutput::DisplayOutput(DisplayConfig config) : config_(std::move(config)) {}

DisplayOutput::~DisplayOutput() {
    destroy();
}

Status DisplayOutput::create(PIXEL_FORMAT_E pixelFormat,
                             MPPCallbackInfo* callback) {
    if (channelCreated_) {
        return Status::failure("vo_already_created", "display output already exists");
    }

    ERRORTYPE result = AW_MPI_VO_Enable(config_.voDevice);
    if (result != SUCCESS) {
        return mppFailure("vo_enable_failed", result);
    }
    deviceEnabled_ = true;

    // sample_virvi2vo applies the public interface/sync attributes after
    // enabling VO and before creating a layer.  Keep the same ordering, but
    // take the values from the product display profile instead of hard-coding
    // its LCD/NTSC defaults.
    VO_PUB_ATTR_S publicAttributes;
    std::memset(&publicAttributes, 0, sizeof(publicAttributes));
    result = AW_MPI_VO_GetPubAttr(config_.voDevice, &publicAttributes);
    if (result != SUCCESS) {
        destroy();
        return mppFailure("vo_public_attr_get_failed", result);
    }
    publicAttributes.enIntfType = interfaceType(config_.interfaceType);
    publicAttributes.enIntfSync = interfaceSync(config_.interfaceSync);
    result = AW_MPI_VO_SetPubAttr(config_.voDevice, &publicAttributes);
    if (result != SUCCESS) {
        destroy();
        return mppFailure("vo_public_attr_set_failed", result);
    }

    if (config_.uiOutsideLayer >= 0) {
        result = AW_MPI_VO_AddOutsideVideoLayer(config_.uiOutsideLayer);
        if (result != SUCCESS) {
            destroy();
            return mppFailure("vo_register_ui_layer_failed", result);
        }
        outsideRegistered_ = true;
    }

    result = AW_MPI_VO_EnableVideoLayer(config_.videoLayer);
    if (result != SUCCESS) {
        destroy();
        return mppFailure("vo_layer_enable_failed", result);
    }
    layerEnabled_ = true;

    VO_VIDEO_LAYER_ATTR_S attributes;
    std::memset(&attributes, 0, sizeof(attributes));
    result = AW_MPI_VO_GetVideoLayerAttr(config_.videoLayer, &attributes);
    if (result != SUCCESS) {
        destroy();
        return mppFailure("vo_layer_get_attr_failed", result);
    }
    attributes.stDispRect.X = config_.x;
    attributes.stDispRect.Y = config_.y;
    attributes.stDispRect.Width = config_.width;
    attributes.stDispRect.Height = config_.height;
    attributes.enPixFormat = pixelFormat;
    result = AW_MPI_VO_SetVideoLayerAttr(config_.videoLayer, &attributes);
    if (result != SUCCESS) {
        destroy();
        return mppFailure("vo_layer_set_attr_failed", result);
    }

    result = AW_MPI_VO_CreateChn(config_.videoLayer, config_.videoChannel);
    if (result != SUCCESS) {
        destroy();
        return mppFailure("vo_channel_create_failed", result);
    }
    channelCreated_ = true;

    if (callback != nullptr) {
        result = AW_MPI_VO_RegisterCallback(
            config_.videoLayer, config_.videoChannel, callback);
        if (result != SUCCESS) {
            destroy();
            return mppFailure("vo_callback_register_failed", result);
        }
    }
    result = AW_MPI_VO_SetChnDispBufNum(
        config_.videoLayer, config_.videoChannel, 2);
    if (result != SUCCESS) {
        destroy();
        return mppFailure("vo_buffer_count_failed", result);
    }
    return Status::success();
}

Status DisplayOutput::start() {
    if (!channelCreated_) {
        return Status::failure("vo_not_created", "display output is not prepared");
    }
    if (started_) {
        return Status::success();
    }
    const ERRORTYPE result =
        AW_MPI_VO_StartChn(config_.videoLayer, config_.videoChannel);
    if (result != SUCCESS) {
        return mppFailure("vo_start_failed", result);
    }
    started_ = true;
    return Status::success();
}

Status DisplayOutput::pause() {
    if (!started_) {
        return Status::failure("vo_not_running", "display output is not running");
    }
    const ERRORTYPE result =
        AW_MPI_VO_PauseChn(config_.videoLayer, config_.videoChannel);
    return result == SUCCESS ? Status::success()
                             : mppFailure("vo_pause_failed", result);
}

Status DisplayOutput::resume() {
    if (!started_) {
        return Status::failure("vo_not_running", "display output is not running");
    }
    const ERRORTYPE result =
        AW_MPI_VO_ResumeChn(config_.videoLayer, config_.videoChannel);
    return result == SUCCESS ? Status::success()
                             : mppFailure("vo_resume_failed", result);
}

Status DisplayOutput::stop() noexcept {
    if (!started_) {
        return Status::success();
    }
    const ERRORTYPE result =
        AW_MPI_VO_StopChn(config_.videoLayer, config_.videoChannel);
    started_ = false;
    return result == SUCCESS ? Status::success()
                             : mppFailure("vo_stop_failed", result);
}

Status DisplayOutput::destroy() noexcept {
    Status firstFailure = Status::success();
    const auto retain = [&firstFailure](Status status) {
        if (firstFailure.ok && !status.ok) {
            firstFailure = std::move(status);
        }
    };

    retain(stop());
    if (channelCreated_) {
        const ERRORTYPE result =
            AW_MPI_VO_DestroyChn(config_.videoLayer, config_.videoChannel);
        if (result != SUCCESS) {
            retain(mppFailure("vo_channel_destroy_failed", result));
        }
        channelCreated_ = false;
    }
    if (layerEnabled_) {
        const ERRORTYPE result = AW_MPI_VO_DisableVideoLayer(config_.videoLayer);
        if (result != SUCCESS) {
            retain(mppFailure("vo_layer_disable_failed", result));
        }
        layerEnabled_ = false;
        usleep(50 * 1000);
    }
    if (outsideRegistered_) {
        const ERRORTYPE result =
            AW_MPI_VO_RemoveOutsideVideoLayer(config_.uiOutsideLayer);
        if (result != SUCCESS) {
            retain(mppFailure("vo_unregister_ui_layer_failed", result));
        }
        outsideRegistered_ = false;
    }
    if (deviceEnabled_) {
        const ERRORTYPE result = AW_MPI_VO_Disable(config_.voDevice);
        if (result != SUCCESS) {
            retain(mppFailure("vo_disable_failed", result));
        }
        deviceEnabled_ = false;
    }
    return firstFailure;
}

MPP_CHN_S DisplayOutput::mppChannel() const {
    MPP_CHN_S result = {MOD_ID_VOU, config_.videoLayer, config_.videoChannel};
    return result;
}

DisplayConfig DisplayOutput::fitWithin(const DisplayConfig& canvas,
                                       int sourceWidth, int sourceHeight) {
    DisplayConfig fitted = canvas;
    if (sourceWidth <= 0 || sourceHeight <= 0 || canvas.width <= 0 ||
        canvas.height <= 0) {
        return fitted;
    }
    // sample_virvi2vo passes its configured rectangle directly to VO.
    // Keep that behavior for the full-screen LCD profile; contain remains
    // available when preserving the source aspect is preferred.
    if (canvas.scaleMode == "stretch")
        return fitted;
    const std::int64_t widthByHeight =
        static_cast<std::int64_t>(canvas.height) * sourceWidth / sourceHeight;
    if (widthByHeight <= canvas.width) {
        fitted.width = std::max(1, static_cast<int>(widthByHeight));
        fitted.height = canvas.height;
        fitted.x = canvas.x + (canvas.width - fitted.width) / 2;
    } else {
        fitted.width = canvas.width;
        fitted.height = std::max(
            1, static_cast<int>(static_cast<std::int64_t>(canvas.width) *
                                sourceHeight / sourceWidth));
        fitted.y = canvas.y + (canvas.height - fitted.height) / 2;
    }
    // The active formats are 4:2:0. Keep the display size and origin on even
    // pixels when the canvas is large enough, then report this exact rect to
    // the GUI instead of making the UI repeat the alignment policy.
    if (fitted.width >= 2) {
        fitted.width &= ~1;
    }
    if (fitted.height >= 2) {
        fitted.height &= ~1;
    }
    fitted.x = canvas.x + (canvas.width - fitted.width) / 2;
    fitted.y = canvas.y + (canvas.height - fitted.height) / 2;
    if ((fitted.x & 1) != 0 && fitted.x + fitted.width < canvas.x + canvas.width) {
        ++fitted.x;
    }
    if ((fitted.y & 1) != 0 && fitted.y + fitted.height < canvas.y + canvas.height) {
        ++fitted.y;
    }
    return fitted;
}

}  // namespace mpp
}  // namespace camera
