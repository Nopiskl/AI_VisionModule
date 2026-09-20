#include "camera/mpp/SnapshotOutput.hpp"
#include "camera/mpp/VencIspLink.hpp"

#include "camera/mpp/FileCommit.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <utility>
#include <unistd.h>

namespace camera {
namespace mpp {
namespace {

Status mppFailure(const char* operation, ERRORTYPE result) {
    return Status::failure(operation, std::to_string(result));
}

bool writeAll(int descriptor, const unsigned char* data, std::size_t length) {
    while (length > 0) {
        const ssize_t written = write(descriptor, data, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            errno = EIO;
            return false;
        }
        data += written;
        length -= static_cast<std::size_t>(written);
    }
    return true;
}

}  // namespace

SnapshotOutput::SnapshotOutput(CameraConfig camera, SnapshotConfig snapshot)
    : camera_(std::move(camera)), snapshot_(std::move(snapshot)) {}

Status SnapshotOutput::capture(VI_DEV viDevice, VI_CHN viChannel,
                               const std::string& finalPath) {
    std::lock_guard<std::mutex> captureGuard(captureMutex_);
    const std::string temporaryPath = finalPath + ".part";

    VIDEO_FRAME_INFO_S frame;
    std::memset(&frame, 0, sizeof(frame));
    ERRORTYPE result = SUCCESS;
    bool frameHeld = false;
    bool encoderCreated = false;
    bool encoderStarted = false;
    bool temporaryReady = false;

    VENC_CHN_ATTR_S attributes;
    std::memset(&attributes, 0, sizeof(attributes));
    const int threshold = snapshot_.width * snapshot_.height;
    const int bufferSize = threshold + 1024 * 1024;
    attributes.VeAttr.Type = PT_JPEG;
    attributes.VeAttr.AttrJpeg.BufSize = bufferSize;
    attributes.VeAttr.AttrJpeg.mThreshSize = threshold;
    attributes.VeAttr.AttrJpeg.bByFrame = TRUE;
    attributes.VeAttr.AttrJpeg.PicWidth = snapshot_.width;
    attributes.VeAttr.AttrJpeg.PicHeight = snapshot_.height;
    attributes.VeAttr.AttrJpeg.bSupportDCF = FALSE;
    attributes.VeAttr.MaxKeyInterval = 1;
    attributes.VeAttr.SrcPicWidth = camera_.captureWidth;
    attributes.VeAttr.SrcPicHeight = camera_.captureHeight;
    attributes.VeAttr.Field = VIDEO_FIELD_FRAME;
    attributes.VeAttr.PixelFormat = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    attributes.VeAttr.mColorSpace = V4L2_COLORSPACE_JPEG;
    attributes.VeAttr.Rotate = ROTATE_NONE;
    attributes.EncppAttr.mbEncppEnable = TRUE;

    Status status = Status::success();
    linkageError_.store(SUCCESS);
    result = AW_MPI_VENC_CreateChn(snapshot_.vencChannel, &attributes);
    if (result != SUCCESS) {
        status = mppFailure("snapshot_venc_create_failed", result);
        goto cleanup;
    }
    encoderCreated = true;

    {
        MPPCallbackInfo callbackInfo;
        callbackInfo.cookie = this;
        callbackInfo.callback = &SnapshotOutput::callback;
        result = AW_MPI_VENC_RegisterCallback(snapshot_.vencChannel, &callbackInfo);
        if (result != SUCCESS) {
            status = mppFailure("snapshot_callback_failed", result);
            goto cleanup;
        }
    }

    {
        VENC_PARAM_JPEG_S jpeg;
        std::memset(&jpeg, 0, sizeof(jpeg));
        jpeg.Qfactor = snapshot_.quality;
        result = AW_MPI_VENC_SetJpegParam(snapshot_.vencChannel, &jpeg);
        if (result != SUCCESS) {
            status = mppFailure("snapshot_jpeg_config_failed", result);
            goto cleanup;
        }
    }

    result = AW_MPI_VENC_ForbidDiscardingFrame(snapshot_.vencChannel, TRUE);
    if (result != SUCCESS) {
        status = mppFailure("snapshot_discard_policy_failed", result);
        goto cleanup;
    }
    result = AW_MPI_VENC_StartRecvPic(snapshot_.vencChannel);
    if (result != SUCCESS) {
        status = mppFailure("snapshot_venc_start_failed", result);
        goto cleanup;
    }
    encoderStarted = true;

    result = AW_MPI_VI_GetFrame(
        viDevice, viChannel, &frame, snapshot_.timeoutMs);
    if (result != SUCCESS) {
        status = mppFailure("snapshot_get_frame_failed", result);
        goto cleanup;
    }
    frameHeld = true;

    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        waitingFrameId_ = frame.mId;
        waitingForFrameReturn_ = true;
        frameReturned_ = false;
    }
    result = AW_MPI_VENC_SendFrame(snapshot_.vencChannel, &frame, 0);
    if (result != SUCCESS) {
        status = mppFailure("snapshot_send_frame_failed", result);
        goto cleanup;
    }

    {
        std::unique_lock<std::mutex> lock(callbackMutex_);
        if (!callbackCondition_.wait_for(
                lock, std::chrono::milliseconds(snapshot_.timeoutMs),
                [this] { return frameReturned_ || linkageError_.load() != SUCCESS; })) {
            status = Status::failure(
                "snapshot_frame_return_timeout",
                "VENC did not release the VI frame before timeout");
            goto cleanup;
        }
    }

    if (linkageError_.load() != SUCCESS) {
        status = mppFailure("snapshot_isp_link_failed", linkageError_.load());
        goto cleanup;
    }

    result = AW_MPI_VI_ReleaseFrame(viDevice, viChannel, &frame);
    frameHeld = false;
    if (result != SUCCESS) {
        status = mppFailure("snapshot_release_frame_failed", result);
        goto cleanup;
    }

    {
        VENC_PACK_S pack;
        VENC_STREAM_S stream;
        std::memset(&pack, 0, sizeof(pack));
        std::memset(&stream, 0, sizeof(stream));
        stream.mpPack = &pack;
        stream.mPackCount = 1;
        result = AW_MPI_VENC_GetStream(
            snapshot_.vencChannel, &stream, snapshot_.timeoutMs);
        if (result != SUCCESS) {
            status = mppFailure("snapshot_get_stream_failed", result);
            goto cleanup;
        }
        status = writeJpegTemporary(stream, temporaryPath);
        temporaryReady = status.ok;
        const ERRORTYPE release =
            AW_MPI_VENC_ReleaseStream(snapshot_.vencChannel, &stream);
        if (status.ok && release != SUCCESS) {
            status = mppFailure("snapshot_release_stream_failed", release);
        }
    }

cleanup:
    if (encoderStarted) {
        const ERRORTYPE stop = AW_MPI_VENC_StopRecvPic(snapshot_.vencChannel);
        if (status.ok && stop != SUCCESS) {
            status = mppFailure("snapshot_venc_stop_failed", stop);
        }
    }
    if (encoderCreated) {
        const ERRORTYPE destroy = AW_MPI_VENC_DestroyChn(snapshot_.vencChannel);
        if (status.ok && destroy != SUCCESS) {
            status = mppFailure("snapshot_venc_destroy_failed", destroy);
        }
    }
    if (frameHeld) {
        const ERRORTYPE release =
            AW_MPI_VI_ReleaseFrame(viDevice, viChannel, &frame);
        frameHeld = false;
        if (status.ok && release != SUCCESS) {
            status = mppFailure("snapshot_release_frame_failed", release);
        }
    }
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        waitingForFrameReturn_ = false;
        frameReturned_ = false;
    }

    if (status.ok && temporaryReady) {
        status = commitFileNoReplace(temporaryPath, finalPath, "snapshot");
    }
    if (!status.ok && temporaryReady) {
        unlink(temporaryPath.c_str());
    }
    return status;
}

ERRORTYPE SnapshotOutput::callback(void* cookie, MPP_CHN_S* channel,
                                   MPP_EVENT_TYPE event, void* eventData) {
    auto* self = static_cast<SnapshotOutput*>(cookie);
    if (self != nullptr && channel != nullptr &&
        channel->mModId == MOD_ID_VENC &&
        event == MPP_EVENT_LINKAGE_ISP2VE_PARAM) {
        const ERRORTYPE result = updateVencIspParameters(
            self->camera_.vippDevice, channel->mChnId, eventData);
        if (result != SUCCESS) {
            self->linkageError_.store(result);
            self->callbackCondition_.notify_one();
        }
        return result;
    }
    if (self == nullptr || event != MPP_EVENT_RELEASE_VIDEO_BUFFER ||
        eventData == nullptr) {
        return SUCCESS;
    }
    const auto* frame = static_cast<const VIDEO_FRAME_INFO_S*>(eventData);
    {
        std::lock_guard<std::mutex> lock(self->callbackMutex_);
        if (!self->waitingForFrameReturn_ ||
            frame->mId != self->waitingFrameId_) {
            return SUCCESS;
        }
        self->waitingForFrameReturn_ = false;
        self->frameReturned_ = true;
    }
    self->callbackCondition_.notify_one();
    return SUCCESS;
}

Status SnapshotOutput::writeJpegTemporary(
    const VENC_STREAM_S& stream, const std::string& temporaryPath) {
    if (stream.mpPack == nullptr || stream.mPackCount == 0) {
        return Status::failure("snapshot_empty_stream", "JPEG stream has no packs");
    }
    const int descriptor = open(
        temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        return Status::failure("snapshot_file_open_failed", std::strerror(errno));
    }

    bool ok = true;
    for (unsigned int index = 0; index < stream.mPackCount && ok; ++index) {
        const VENC_PACK_S& pack = stream.mpPack[index];
        if (pack.mpAddr0 != nullptr && pack.mLen0 > 0) {
            ok = writeAll(descriptor, pack.mpAddr0, pack.mLen0);
        }
        if (ok && pack.mpAddr1 != nullptr && pack.mLen1 > 0) {
            ok = writeAll(descriptor, pack.mpAddr1, pack.mLen1);
        }
        if (ok && pack.mpAddr2 != nullptr && pack.mLen2 > 0) {
            ok = writeAll(descriptor, pack.mpAddr2, pack.mLen2);
        }
    }
    int savedError = 0;
    if (!ok) {
        savedError = errno;
    } else if (fsync(descriptor) != 0) {
        savedError = errno;
        ok = false;
    }
    if (close(descriptor) != 0 && ok) {
        savedError = errno;
        ok = false;
    }
    if (!ok) {
        unlink(temporaryPath.c_str());
        return Status::failure("snapshot_file_write_failed", std::strerror(savedError));
    }
    return Status::success();
}

}  // namespace mpp
}  // namespace camera
