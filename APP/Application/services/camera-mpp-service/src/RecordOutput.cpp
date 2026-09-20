#include "camera/mpp/RecordOutput.hpp"
#include "camera/mpp/VencIspLink.hpp"

#include "camera/mpp/FileCommit.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utility>

namespace camera {
namespace mpp {
namespace {

Status mppFailure(const char* operation, ERRORTYPE result) {
    return Status::failure(operation, std::to_string(result));
}

std::string parentPath(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "." :
           slash == 0 ? "/" : path.substr(0, slash);
}

bool isMountPoint(const std::string& path) {
    if (path == "/") {
        return true;
    }
    struct stat pathMetadata {};
    struct stat parentMetadata {};
    return stat(path.c_str(), &pathMetadata) == 0 &&
           stat(parentPath(path).c_str(), &parentMetadata) == 0 &&
           pathMetadata.st_dev != parentMetadata.st_dev;
}

}  // namespace

RecordOutput::RecordOutput(CameraConfig camera, RecordConfig record,
                           std::string mediaRoot, bool requireMediaRootMount,
                           EventMailbox& mailbox)
    : camera_(std::move(camera)),
      record_(std::move(record)),
      mediaRoot_(std::move(mediaRoot)),
      requireMediaRootMount_(requireMediaRootMount),
      mailbox_(mailbox) {}

RecordOutput::~RecordOutput() {
    abort();
}

Status RecordOutput::start(VI_DEV viDevice, const std::string& finalPath) {
    if (started_ || viCreated_ || vencCreated_ || muxChannelCreated_) {
        return Status::failure("record_already_active", "record output is busy");
    }
    ++generation_;
    viDevice_ = viDevice;
    finalPath_ = finalPath;
    temporaryPath_ = finalPath + ".part";
    Status status = checkStorage();
    if (!status.ok) {
        finalPath_.clear();
        temporaryPath_.clear();
        viDevice_ = -1;
        return status;
    }
    fileDescriptor_ = open(temporaryPath_.c_str(),
                           O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fileDescriptor_ < 0) {
        return Status::failure("record_file_open_failed", std::strerror(errno));
    }

    ERRORTYPE result = AW_MPI_VI_CreateVirChn(
        viDevice_, camera_.recordChannel, nullptr);
    if (result != SUCCESS) {
        const Status status = mppFailure("record_vi_create_failed", result);
        cleanup(false);
        return status;
    }
    viCreated_ = true;

    status = prepareVenc();
    if (!status.ok) {
        cleanup(false);
        return status;
    }
    status = prepareMux();
    if (!status.ok) {
        cleanup(false);
        return status;
    }

    MPP_CHN_S vi = {MOD_ID_VIU, viDevice_, camera_.recordChannel};
    MPP_CHN_S venc = {MOD_ID_VENC, 0, record_.vencChannel};
    MPP_CHN_S mux = {MOD_ID_MUX, 0, record_.muxChannel};
    result = AW_MPI_SYS_Bind(&vi, &venc);
    if (result != SUCCESS) {
        status = mppFailure("record_bind_vi_venc_failed", result);
        cleanup(false);
        return status;
    }
    viVencBound_ = true;
    result = AW_MPI_SYS_Bind(&venc, &mux);
    if (result != SUCCESS) {
        status = mppFailure("record_bind_venc_mux_failed", result);
        cleanup(false);
        return status;
    }
    vencMuxBound_ = true;

    // Binding establishes the VENC-to-stream mapping used by ExtraData.
    VencHeaderData header{};
    result = AW_MPI_VENC_GetH264SpsPpsInfo(record_.vencChannel, &header);
    if (result != SUCCESS) {
        status = mppFailure("record_sps_pps_failed", result);
        cleanup(false);
        return status;
    }
    if (header.pBuffer == nullptr || header.nLength == 0) {
        cleanup(false);
        return Status::failure("record_sps_pps_empty", "VENC returned an empty H.264 header");
    }
    result = AW_MPI_MUX_SetH264SpsPpsInfo(
        record_.muxChannel, record_.vencChannel, &header);
    if (result != SUCCESS) {
        status = mppFailure("record_mux_sps_pps_failed", result);
        cleanup(false);
        return status;
    }

    result = AW_MPI_VI_EnableVirChn(viDevice_, camera_.recordChannel);
    if (result != SUCCESS) {
        status = mppFailure("record_vi_enable_failed", result);
        cleanup(false);
        return status;
    }
    viEnabled_ = true;
    result = AW_MPI_VENC_StartRecvPic(record_.vencChannel);
    if (result != SUCCESS) {
        status = mppFailure("record_venc_start_failed", result);
        cleanup(false);
        return status;
    }
    vencStarted_ = true;
    result = AW_MPI_MUX_StartChn(record_.muxChannel);
    if (result != SUCCESS) {
        status = mppFailure("record_mux_start_failed", result);
        cleanup(false);
        return status;
    }
    muxStarted_ = true;
    started_ = true;
    status = startStorageMonitor();
    if (!status.ok) {
        cleanup(false);
        return status;
    }
    return Status::success();
}

Status RecordOutput::prepareVenc() {
    VENC_CHN_ATTR_S attributes;
    std::memset(&attributes, 0, sizeof(attributes));
    attributes.VeAttr.Type = PT_H264;
    attributes.VeAttr.MaxKeyInterval = record_.frameRate;
    attributes.VeAttr.SrcPicWidth = camera_.captureWidth;
    attributes.VeAttr.SrcPicHeight = camera_.captureHeight;
    attributes.VeAttr.Field = VIDEO_FIELD_FRAME;
    attributes.VeAttr.PixelFormat = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    attributes.VeAttr.mColorSpace = V4L2_COLORSPACE_JPEG;
    attributes.VeAttr.Rotate = ROTATE_NONE;
    attributes.VeAttr.AttrH264e.BufSize = record_.vbvBufferBytes;
    attributes.VeAttr.AttrH264e.mThreshSize = record_.vbvThresholdBytes;
    attributes.VeAttr.AttrH264e.bByFrame = TRUE;
    attributes.VeAttr.AttrH264e.Profile = 1;  // Main profile in this MPP API.
    attributes.VeAttr.AttrH264e.mLevel = H264_LEVEL_Default;
    attributes.VeAttr.AttrH264e.PicWidth = record_.width;
    attributes.VeAttr.AttrH264e.PicHeight = record_.height;
    attributes.VeAttr.AttrH264e.mbPIntraEnable = TRUE;
    attributes.RcAttr.mRcMode = VENC_RC_MODE_H264CBR;
    attributes.RcAttr.mAttrH264Cbr.mSrcFrmRate = camera_.frameRate;
    attributes.RcAttr.mAttrH264Cbr.mDstFrmRate = record_.frameRate;
    attributes.RcAttr.mAttrH264Cbr.mGop = record_.frameRate;
    attributes.RcAttr.mAttrH264Cbr.mBitRate = record_.bitRate;
    attributes.GopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    attributes.GopAttr.mGopSize = record_.frameRate;
    attributes.EncppAttr.mbEncppEnable = TRUE;

    ERRORTYPE result = AW_MPI_VENC_CreateChn(record_.vencChannel, &attributes);
    if (result != SUCCESS) {
        return mppFailure("record_venc_create_failed", result);
    }
    vencCreated_ = true;
    MPPCallbackInfo callbackInfo {};
    callbackInfo.cookie = this;
    callbackInfo.callback = &RecordOutput::callback;
    result = AW_MPI_VENC_RegisterCallback(record_.vencChannel, &callbackInfo);
    if (result != SUCCESS) {
        return mppFailure("record_venc_callback_failed", result);
    }

    VENC_FRAME_RATE_S frameRate;
    std::memset(&frameRate, 0, sizeof(frameRate));
    frameRate.SrcFrmRate = camera_.frameRate;
    frameRate.DstFrmRate = record_.frameRate;
    result = AW_MPI_VENC_SetFrameRate(record_.vencChannel, &frameRate);
    if (result != SUCCESS) {
        return mppFailure("record_venc_fps_failed", result);
    }
    return Status::success();
}

Status RecordOutput::prepareMux() {
    // Current sun8iw21 SDK uses one MUX channel, not a group plus subchannel.
    MUX_CHN_ATTR_S channel{};
    channel.mVideoAttrValidNum = 1;
    channel.mVideoAttr[0].mVideoEncodeType = PT_H264;
    channel.mVideoAttr[0].mWidth = record_.width;
    channel.mVideoAttr[0].mHeight = record_.height;
    channel.mVideoAttr[0].mVideoFrmRate = record_.frameRate * 1000;
    channel.mVideoAttr[0].mMaxKeyInterval = record_.frameRate;
    channel.mVideoAttr[0].mVeChn = record_.vencChannel;
    channel.mAudioEncodeType = PT_MAX;
    channel.mTextEncodeType = PT_MAX;
    channel.mMuxerId = 0;
    channel.mMediaFileFormat = MEDIA_FILE_FORMAT_MP4;
    channel.mMaxFileDuration = 0;
    channel.mMaxFileSizeBytes = 0;
    channel.mCallbackOutFlag = FALSE;
    channel.mFsWriteMode = FSWRITEMODE_SIMPLECACHE;
    channel.mSimpleCacheSize = 512 * 1024;
    channel.mAddRepairInfo = record_.addRepairInfo;
    channel.mMaxFrmsTagInterval = record_.repairBackupIntervalUs;

    ERRORTYPE result = AW_MPI_MUX_CreateChn(
        record_.muxChannel, &channel, fileDescriptor_, 0);
    if (result != SUCCESS) {
        return mppFailure("record_mux_channel_create_failed", result);
    }
    muxChannelCreated_ = true;

    MPPCallbackInfo callbackInfo{};
    callbackInfo.cookie = this;
    callbackInfo.callback = &RecordOutput::callback;
    result = AW_MPI_MUX_RegisterCallback(record_.muxChannel, &callbackInfo);
    return result == SUCCESS ? Status::success()
                             : mppFailure("record_mux_callback_failed", result);
}

Status RecordOutput::stop() {
    if (!started_ && !viCreated_ && !vencCreated_ && !muxChannelCreated_) {
        return Status::success();
    }
    return cleanup(true);
}

Status RecordOutput::abort() noexcept {
    return cleanup(false);
}

Status RecordOutput::cleanup(bool commitFile) noexcept {
    Status firstFailure = Status::success();
    const auto retain = [&firstFailure](Status status) {
        if (firstFailure.ok && !status.ok) {
            firstFailure = std::move(status);
        }
    };

    stopStorageMonitor();
    started_ = false;
    if (viEnabled_) {
        const ERRORTYPE result =
            AW_MPI_VI_DisableVirChn(viDevice_, camera_.recordChannel);
        if (result != SUCCESS) {
            retain(mppFailure("record_vi_disable_failed", result));
        }
        viEnabled_ = false;
    }
    if (vencStarted_) {
        const ERRORTYPE result = AW_MPI_VENC_StopRecvPic(record_.vencChannel);
        if (result != SUCCESS) {
            retain(mppFailure("record_venc_stop_failed", result));
        }
        vencStarted_ = false;
    }
    if (muxStarted_) {
        const ERRORTYPE result = AW_MPI_MUX_StopChn(record_.muxChannel, commitFile ? FALSE : TRUE);
        if (result != SUCCESS) {
            retain(mppFailure("record_mux_stop_failed", result));
        }
        muxStarted_ = false;
    }

    MPP_CHN_S vi = {MOD_ID_VIU, viDevice_, camera_.recordChannel};
    MPP_CHN_S venc = {MOD_ID_VENC, 0, record_.vencChannel};
    MPP_CHN_S mux = {MOD_ID_MUX, 0, record_.muxChannel};
    if (vencMuxBound_) {
        const ERRORTYPE result = AW_MPI_SYS_UnBind(&venc, &mux);
        if (result != SUCCESS) {
            retain(mppFailure("record_unbind_venc_mux_failed", result));
        }
        vencMuxBound_ = false;
    }
    if (viVencBound_) {
        const ERRORTYPE result = AW_MPI_SYS_UnBind(&vi, &venc);
        if (result != SUCCESS) {
            retain(mppFailure("record_unbind_vi_venc_failed", result));
        }
        viVencBound_ = false;
    }
    if (muxChannelCreated_) {
        const ERRORTYPE result = AW_MPI_MUX_DestroyChn(record_.muxChannel);
        if (result != SUCCESS) {
            retain(mppFailure("record_mux_channel_destroy_failed", result));
        }
        muxChannelCreated_ = false;
    }
    if (vencCreated_) {
        const ERRORTYPE result = AW_MPI_VENC_DestroyChn(record_.vencChannel);
        if (result != SUCCESS) {
            retain(mppFailure("record_venc_destroy_failed", result));
        }
        vencCreated_ = false;
    }
    if (viCreated_) {
        const ERRORTYPE result =
            AW_MPI_VI_DestroyVirChn(viDevice_, camera_.recordChannel);
        if (result != SUCCESS) {
            retain(mppFailure("record_vi_destroy_failed", result));
        }
        viCreated_ = false;
    }

    if (fileDescriptor_ >= 0) {
        if (fsync(fileDescriptor_) != 0) {
            retain(Status::failure("record_fsync_failed", std::strerror(errno)));
        }
        if (close(fileDescriptor_) != 0) {
            retain(Status::failure("record_close_failed", std::strerror(errno)));
        }
        fileDescriptor_ = -1;
    }

    if (commitFile && firstFailure.ok && !temporaryPath_.empty()) {
        retain(commitFileNoReplace(temporaryPath_, finalPath_, "record"));
    }

    const bool preserveForRepair =
        commitFile && !firstFailure.ok && record_.addRepairInfo != 0 &&
        !temporaryPath_.empty() && access(temporaryPath_.c_str(), F_OK) == 0;
    if ((!commitFile || !firstFailure.ok) && !temporaryPath_.empty() &&
        !preserveForRepair) {
        unlink(temporaryPath_.c_str());
    }
    temporaryPath_.clear();
    if (!commitFile || !firstFailure.ok) {
        finalPath_.clear();
    }
    viDevice_ = -1;
    return firstFailure;
}

Status RecordOutput::checkStorage() const {
    if (requireMediaRootMount_ && !isMountPoint(mediaRoot_)) {
        return Status::failure(
            "record_storage_removed", mediaRoot_ + " is not a mounted filesystem");
    }

    struct statvfs filesystem {};
    const std::string directory = parentPath(finalPath_);
    if (statvfs(directory.c_str(), &filesystem) != 0) {
        return Status::failure("record_storage_removed", std::strerror(errno));
    }
    const std::uint64_t blockSize = filesystem.f_frsize != 0
                                        ? filesystem.f_frsize
                                        : filesystem.f_bsize;
    const std::uint64_t available =
        static_cast<std::uint64_t>(filesystem.f_bavail) * blockSize;
    if (available <= static_cast<std::uint64_t>(record_.storageReserveBytes)) {
        return Status::failure(
            "record_storage_full",
            "available bytes " + std::to_string(available) +
                " <= reserve " + std::to_string(record_.storageReserveBytes));
    }
    return Status::success();
}

Status RecordOutput::startStorageMonitor() {
    monitorStop_.store(false);
    try {
        monitorThread_ = std::thread(&RecordOutput::monitorStorage, this);
    } catch (const std::exception& error) {
        monitorStop_.store(true);
        return Status::failure("record_storage_monitor_failed", error.what());
    }
    return Status::success();
}

void RecordOutput::stopStorageMonitor() noexcept {
    monitorStop_.store(true);
    monitorCondition_.notify_all();
    if (monitorThread_.joinable() &&
        monitorThread_.get_id() != std::this_thread::get_id()) {
        monitorThread_.join();
    }
}

void RecordOutput::monitorStorage() noexcept {
    std::unique_lock<std::mutex> lock(monitorMutex_);
    while (!monitorStop_.load()) {
        if (monitorCondition_.wait_for(
                lock, std::chrono::milliseconds(record_.storagePollMs),
                [this] { return monitorStop_.load(); })) {
            break;
        }
        lock.unlock();
        const Status status = checkStorage();
        lock.lock();
        if (!status.ok && !monitorStop_.exchange(true)) {
            mailbox_.postCritical({PipelineEventType::StorageFault,
                                   status.code, status.detail, generation_});
            break;
        }
    }
}

ERRORTYPE RecordOutput::callback(void* cookie, MPP_CHN_S* channel,
                                 MPP_EVENT_TYPE event, void* eventData) {
    auto* self = static_cast<RecordOutput*>(cookie);
    if (self != nullptr && channel != nullptr &&
        channel->mModId == MOD_ID_VENC &&
        event == MPP_EVENT_LINKAGE_ISP2VE_PARAM) {
        const ERRORTYPE result = updateVencIspParameters(
            self->viDevice_, channel->mChnId, eventData);
        if (result != SUCCESS) {
            self->mailbox_.postCritical({PipelineEventType::PipelineError,
                                        "record_isp_link_failed", std::to_string(result)});
        }
        return result;
    }
    if (self == nullptr || channel == nullptr || channel->mModId != MOD_ID_MUX) {
        return SUCCESS;
    }
    if (event == MPP_EVENT_RECORD_DONE) {
        self->mailbox_.post(
            {PipelineEventType::RecordDone, "record_done", ""});
    } else if (event == MPP_EVENT_NEED_NEXT_FD) {
        self->mailbox_.post({PipelineEventType::PipelineError,
                             "record_next_file_required",
                             "segmented recording is not implemented"});
    }
    return SUCCESS;
}

}  // namespace mpp
}  // namespace camera
