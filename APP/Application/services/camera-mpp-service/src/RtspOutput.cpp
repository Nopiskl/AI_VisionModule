#include "camera/mpp/RtspOutput.hpp"
#include "camera/mpp/VencIspLink.hpp"

#include <MediaStream.h>
#include <TinyServer.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <exception>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace camera {
namespace mpp {
namespace {

Status mppFailure(const char* operation, ERRORTYPE result) {
    return Status::failure(operation, std::to_string(result));
}

Status interfaceAddress(const std::string& name, std::string* output) {
    if (name.empty() || name.size() >= IFNAMSIZ) {
        return Status::failure("rtsp_interface_invalid", name);
    }
    const int descriptor = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0) {
        return Status::failure("rtsp_address_socket_failed", std::strerror(errno));
    }

    struct ifreq request {};
    std::strncpy(request.ifr_name, name.c_str(), IFNAMSIZ - 1);
    request.ifr_addr.sa_family = AF_INET;
    if (ioctl(descriptor, SIOCGIFADDR, &request) != 0) {
        const std::string detail = name + ": " + std::strerror(errno);
        close(descriptor);
        return Status::failure("rtsp_interface_unavailable", detail);
    }
    close(descriptor);

    char address[INET_ADDRSTRLEN] = {};
    const auto* socketAddress =
        reinterpret_cast<const struct sockaddr_in*>(&request.ifr_addr);
    if (inet_ntop(AF_INET, &socketAddress->sin_addr, address,
                  sizeof(address)) == nullptr) {
        return Status::failure("rtsp_address_format_failed", std::strerror(errno));
    }
    *output = address;
    return Status::success();
}

Status preflightEndpoint(const std::string& address, int port) {
    const int descriptor = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0) {
        return Status::failure("rtsp_preflight_socket_failed",
                               std::strerror(errno));
    }

    struct sockaddr_in endpoint {};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(static_cast<unsigned short>(port));
    if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1) {
        close(descriptor);
        return Status::failure("rtsp_address_invalid", address);
    }
    if (bind(descriptor, reinterpret_cast<const struct sockaddr*>(&endpoint),
             sizeof(endpoint)) != 0) {
        const std::string detail = address + ":" + std::to_string(port) +
                                   ": " + std::strerror(errno);
        close(descriptor);
        return Status::failure("rtsp_endpoint_unavailable", detail);
    }
    close(descriptor);
    return Status::success();
}

bool appendSize(std::size_t part, std::size_t limit, std::size_t* total) {
    if (part > limit - *total) {
        return false;
    }
    *total += part;
    return true;
}

}  // namespace

RtspOutput::RtspOutput(CameraConfig camera, RtspConfig rtsp,
                       EventMailbox& mailbox)
    : camera_(std::move(camera)), rtsp_(std::move(rtsp)), mailbox_(mailbox) {}

RtspOutput::~RtspOutput() {
    stop();
}

Status RtspOutput::start(VI_DEV viDevice) {
    if (rtsp_.enabled == 0) {
        return Status::failure("rtsp_disabled", "RTSP is disabled by configuration");
    }
    if (active_ || viCreated_ || vencCreated_ || server_ != nullptr ||
        streamThread_.joinable()) {
        return Status::failure("rtsp_already_active", "RTSP output is busy");
    }

    ++generation_;
    viDevice_ = viDevice;
    stopRequested_.store(false);
    faultPosted_.store(false);
    keyFrameRequested_.store(false);
    ERRORTYPE result =
        AW_MPI_VI_CreateVirChn(viDevice_, rtsp_.viChannel, nullptr);
    if (result != SUCCESS) {
        viDevice_ = -1;
        return mppFailure("rtsp_vi_create_failed", result);
    }
    viCreated_ = true;

    Status status = prepareVenc();
    if (!status.ok) {
        stop();
        return status;
    }

    MPP_CHN_S vi = {MOD_ID_VIU, viDevice_, rtsp_.viChannel};
    MPP_CHN_S venc = {MOD_ID_VENC, 0, rtsp_.vencChannel};
    result = AW_MPI_SYS_Bind(&vi, &venc);
    if (result != SUCCESS) {
        status = mppFailure("rtsp_bind_vi_venc_failed", result);
        stop();
        return status;
    }
    viVencBound_ = true;

    result = AW_MPI_VI_EnableVirChn(viDevice_, rtsp_.viChannel);
    if (result != SUCCESS) {
        status = mppFailure("rtsp_vi_enable_failed", result);
        stop();
        return status;
    }
    viEnabled_ = true;
    result = AW_MPI_VENC_StartRecvPic(rtsp_.vencChannel);
    if (result != SUCCESS) {
        status = mppFailure("rtsp_venc_start_failed", result);
        stop();
        return status;
    }
    vencStarted_ = true;

    status = prepareCodecHeader();
    if (!status.ok) {
        stop();
        return status;
    }

    status = prepareServer();
    if (!status.ok) {
        stop();
        return status;
    }

    result = AW_MPI_VENC_RequestIDR(rtsp_.vencChannel, TRUE);
    if (result != SUCCESS) {
        status = mppFailure("rtsp_initial_idr_failed", result);
        stop();
        return status;
    }

    try {
        streamThread_ = std::thread(&RtspOutput::streamLoop, this);
    } catch (const std::exception& error) {
        status = Status::failure("rtsp_stream_thread_failed", error.what());
        stop();
        return status;
    }
    active_ = true;
    return Status::success();
}

Status RtspOutput::prepareVenc() {
    VENC_CHN_ATTR_S attributes;
    std::memset(&attributes, 0, sizeof(attributes));
    attributes.VeAttr.Type = PT_H264;
    attributes.VeAttr.MaxKeyInterval = rtsp_.frameRate;
    attributes.VeAttr.SrcPicWidth = camera_.captureWidth;
    attributes.VeAttr.SrcPicHeight = camera_.captureHeight;
    attributes.VeAttr.Field = VIDEO_FIELD_FRAME;
    attributes.VeAttr.PixelFormat = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    attributes.VeAttr.mColorSpace = V4L2_COLORSPACE_JPEG;
    attributes.VeAttr.Rotate = ROTATE_NONE;
    attributes.VeAttr.AttrH264e.BufSize = rtsp_.vbvBufferBytes;
    attributes.VeAttr.AttrH264e.mThreshSize = rtsp_.vbvThresholdBytes;
    attributes.VeAttr.AttrH264e.bByFrame = TRUE;
    attributes.VeAttr.AttrH264e.Profile = 1;
    attributes.VeAttr.AttrH264e.mLevel = H264_LEVEL_Default;
    attributes.VeAttr.AttrH264e.PicWidth = rtsp_.width;
    attributes.VeAttr.AttrH264e.PicHeight = rtsp_.height;
    attributes.VeAttr.AttrH264e.mbPIntraEnable = TRUE;
    attributes.RcAttr.mRcMode = VENC_RC_MODE_H264CBR;
    attributes.RcAttr.mAttrH264Cbr.mSrcFrmRate = camera_.frameRate;
    attributes.RcAttr.mAttrH264Cbr.mDstFrmRate = rtsp_.frameRate;
    attributes.RcAttr.mAttrH264Cbr.mGop = rtsp_.frameRate;
    attributes.RcAttr.mAttrH264Cbr.mBitRate = rtsp_.bitRate;
    attributes.GopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    attributes.GopAttr.mGopSize = rtsp_.frameRate;
    attributes.EncppAttr.mbEncppEnable = TRUE;

    ERRORTYPE result = AW_MPI_VENC_CreateChn(rtsp_.vencChannel, &attributes);
    if (result != SUCCESS) {
        return mppFailure("rtsp_venc_create_failed", result);
    }
    vencCreated_ = true;
    MPPCallbackInfo callbackInfo {};
    callbackInfo.cookie = this;
    callbackInfo.callback = &RtspOutput::vencCallback;
    result = AW_MPI_VENC_RegisterCallback(rtsp_.vencChannel, &callbackInfo);
    if (result != SUCCESS) {
        return mppFailure("rtsp_venc_callback_failed", result);
    }

    VENC_FRAME_RATE_S frameRate;
    std::memset(&frameRate, 0, sizeof(frameRate));
    frameRate.SrcFrmRate = camera_.frameRate;
    frameRate.DstFrmRate = rtsp_.frameRate;
    result = AW_MPI_VENC_SetFrameRate(rtsp_.vencChannel, &frameRate);
    return result == SUCCESS ? Status::success()
                             : mppFailure("rtsp_venc_fps_failed", result);
}

Status RtspOutput::prepareCodecHeader() {
    VencHeaderData header;
    std::memset(&header, 0, sizeof(header));
    const ERRORTYPE result =
        AW_MPI_VENC_GetH264SpsPpsInfo(rtsp_.vencChannel, &header);
    if (result != SUCCESS) {
        return mppFailure("rtsp_sps_pps_failed", result);
    }
    if (header.pBuffer == nullptr || header.nLength == 0) {
        return Status::failure("rtsp_sps_pps_empty",
                               "VENC returned an empty H.264 header");
    }
    if (header.nLength > static_cast<unsigned int>(rtsp_.maxFrameBytes)) {
        return Status::failure(
            "rtsp_sps_pps_too_large",
            std::to_string(header.nLength) + " bytes, limit " +
                std::to_string(rtsp_.maxFrameBytes));
    }
    try {
        codecHeader_.assign(header.pBuffer, header.pBuffer + header.nLength);
    } catch (const std::exception& error) {
        return Status::failure("rtsp_sps_pps_copy_failed", error.what());
    }
    return Status::success();
}

Status RtspOutput::prepareServer() {
    std::string address;
    Status status = interfaceAddress(rtsp_.networkInterface, &address);
    if (!status.ok) {
        return status;
    }

    status = preflightEndpoint(address, rtsp_.port);
    if (!status.ok) {
        return status;
    }

    try {
        server_ = TinyServer::createServer(address, rtsp_.port);
        if (server_ == nullptr) {
            return Status::failure("rtsp_server_create_failed", address);
        }
        MediaStream::MediaStreamAttr attributes {};
        attributes.videoType = MediaStream::MediaStreamAttr::VIDEO_TYPE_H264;
        // TinyServer's public ABI has no "audio disabled" enum.  This output
        // never creates AENC or appends audio data; the remaining SDP caveat is
        // documented in known-limitations.md.
        attributes.audioType = MediaStream::MediaStreamAttr::AUDIO_TYPE_AAC;
        attributes.streamType =
            MediaStream::MediaStreamAttr::STREAM_TYPE_UNICAST;
        stream_ = server_->createMediaStream(rtsp_.streamName, attributes);
        if (stream_ == nullptr) {
            return Status::failure("rtsp_media_stream_create_failed",
                                   rtsp_.streamName);
        }
        stream_->setVideoFrameRate(rtsp_.frameRate);
        stream_->setNewClientCallback(&RtspOutput::newClientCallback, this);

        // FRAME_DATA_TYPE_HEADER makes the pinned TinyServer parse and cache
        // SPS/PPS for the H.264 SDP.  Merely prepending these bytes to an I
        // frame, as the vendor samples do, only enqueues NAL units and leaves
        // the SDP parameter-set cache empty.
        stream_->appendVideoData(
            codecHeader_.data(), static_cast<unsigned int>(codecHeader_.size()),
            0, MediaStream::FRAME_DATA_TYPE_HEADER);

        // The pinned legacy MediaStream::streamURL() implementation releases
        // the live555 URL buffer with scalar delete even though live555 returns
        // an array allocation.  Construct the validated equivalent locally.
        url_ = "rtsp://" + address + ":" + std::to_string(rtsp_.port) + "/" +
               rtsp_.streamName;
        if (server_->runWithNewThread() != 0) {
            return Status::failure("rtsp_server_start_failed", url_);
        }
    } catch (const std::exception& error) {
        return Status::failure("rtsp_server_exception", error.what());
    } catch (...) {
        return Status::failure("rtsp_server_exception", "unknown exception");
    }
    serverStarted_ = true;
    return Status::success();
}

void RtspOutput::streamLoop() noexcept {
    try {
        std::vector<unsigned char> buffer;
        buffer.resize(static_cast<std::size_t>(rtsp_.maxFrameBytes));
        VENC_PACK_S pack;
        VENC_STREAM_S encoded;
        std::memset(&pack, 0, sizeof(pack));
        std::memset(&encoded, 0, sizeof(encoded));
        // The pinned sun8iw21 VideoEnc component reports and fills exactly one
        // pack per by-frame output, with up to three address fragments.
        encoded.mPackCount = 1;
        encoded.mpPack = &pack;
        bool waitingForIntra = true;

        while (!stopRequested_.load()) {
            if (keyFrameRequested_.exchange(false)) {
                waitingForIntra = true;
                const ERRORTYPE idrResult =
                    AW_MPI_VENC_RequestIDR(rtsp_.vencChannel, TRUE);
                if (idrResult != SUCCESS) {
                    postFaultOnce("rtsp_client_idr_failed",
                                  std::to_string(idrResult));
                    return;
                }
            }

            std::memset(&pack, 0, sizeof(pack));
            const ERRORTYPE result =
                AW_MPI_VENC_GetStream(rtsp_.vencChannel, &encoded, 200);
            if (result != SUCCESS) {
                if (stopRequested_.load()) {
                    break;
                }
                if (result == ERR_VENC_BUF_EMPTY) {
                    continue;
                }
                postFaultOnce("rtsp_stream_get_failed", std::to_string(result));
                return;
            }

            const bool intra =
                pack.mDataType.enH264EType == H264E_NALU_ISLICE ||
                pack.mDataType.enH264EType == H264E_NALU_IPSLICE;
            const unsigned char* const addresses[] = {
                pack.mpAddr0, pack.mpAddr1, pack.mpAddr2};
            const unsigned int lengths[] = {
                pack.mLen0, pack.mLen1, pack.mLen2};
            bool validAddresses = true;
            bool validSize = true;
            std::size_t frameBytes = 0;
            for (std::size_t index = 0; index < 3; ++index) {
                if (lengths[index] != 0 && addresses[index] == nullptr) {
                    validAddresses = false;
                }
                if (validSize &&
                    !appendSize(static_cast<std::size_t>(lengths[index]),
                                buffer.size(), &frameBytes)) {
                    validSize = false;
                }
            }

            if (validAddresses && validSize && frameBytes > 0) {
                std::size_t offset = 0;
                for (std::size_t index = 0; index < 3; ++index) {
                    if (lengths[index] != 0) {
                        std::memcpy(buffer.data() + offset, addresses[index],
                                    lengths[index]);
                        offset += lengths[index];
                    }
                }
            }
            const std::uint64_t pts = pack.mPTS;

            // The payload is now owned by this worker, so return the MPP stream
            // before any TinyServer call that can wait behind a slow client.
            const ERRORTYPE releaseResult =
                AW_MPI_VENC_ReleaseStream(rtsp_.vencChannel, &encoded);
            if (releaseResult != SUCCESS) {
                postFaultOnce("rtsp_stream_release_failed",
                              std::to_string(releaseResult));
                return;
            }
            if (stopRequested_.load()) {
                break;
            }
            if (!validAddresses || !validSize || frameBytes == 0) {
                const char* code = !validAddresses
                                       ? "rtsp_frame_address_invalid"
                                       : !validSize ? "rtsp_frame_too_large"
                                                    : "rtsp_frame_empty";
                postFaultOnce(code,
                              std::to_string(frameBytes) + " bytes, limit " +
                                  std::to_string(buffer.size()));
                return;
            }

            // A PLAY callback can race with GetStream.  Re-check after the MPP
            // stream is copied/released, request a fresh IDR, and suppress P
            // frames until a decodable boundary is observed.
            if (keyFrameRequested_.exchange(false)) {
                waitingForIntra = true;
                const ERRORTYPE idrResult =
                    AW_MPI_VENC_RequestIDR(rtsp_.vencChannel, TRUE);
                if (idrResult != SUCCESS) {
                    postFaultOnce("rtsp_client_idr_failed",
                                  std::to_string(idrResult));
                    return;
                }
            }
            if (waitingForIntra && !intra) {
                continue;
            }
            if (intra) {
                stream_->appendVideoData(
                    codecHeader_.data(),
                    static_cast<unsigned int>(codecHeader_.size()), pts,
                    MediaStream::FRAME_DATA_TYPE_HEADER);
                waitingForIntra = false;
            }
            stream_->appendVideoData(
                buffer.data(), static_cast<unsigned int>(frameBytes), pts,
                intra ? MediaStream::FRAME_DATA_TYPE_I
                      : MediaStream::FRAME_DATA_TYPE_P);
        }
    } catch (const std::exception& error) {
        postFaultOnce("rtsp_stream_exception", error.what());
    } catch (...) {
        postFaultOnce("rtsp_stream_exception", "unknown exception");
    }
}

void RtspOutput::newClientCallback(void* context) {
    auto* self = static_cast<RtspOutput*>(context);
    if (self != nullptr && !self->stopRequested_.load()) {
        self->keyFrameRequested_.store(true);
    }
}

void RtspOutput::postFaultOnce(const std::string& code,
                               const std::string& detail) noexcept {
    if (!faultPosted_.exchange(true)) {
        mailbox_.postCritical(
            {PipelineEventType::RtspFault, code, detail, generation_});
    }
}

Status RtspOutput::stop() noexcept {
    Status firstFailure = Status::success();
    const auto retain = [&firstFailure](Status status) {
        if (firstFailure.ok && !status.ok) {
            firstFailure = std::move(status);
        }
    };
    const auto retainMpp = [&retain](const char* operation, ERRORTYPE result) {
        if (result != SUCCESS) {
            retain(mppFailure(operation, result));
        }
    };

    active_ = false;
    stopRequested_.store(true);
    if (streamThread_.joinable()) {
        streamThread_.join();
    }
    if (serverStarted_ && server_ != nullptr) {
        server_->stop();
        serverStarted_ = false;
    }
    delete stream_;
    stream_ = nullptr;
    delete server_;
    server_ = nullptr;
    codecHeader_.clear();
    url_.clear();
    keyFrameRequested_.store(false);

    if (viEnabled_) {
        retainMpp("rtsp_vi_disable_failed",
                  AW_MPI_VI_DisableVirChn(viDevice_, rtsp_.viChannel));
        viEnabled_ = false;
    }
    if (vencStarted_) {
        retainMpp("rtsp_venc_stop_failed",
                  AW_MPI_VENC_StopRecvPic(rtsp_.vencChannel));
        vencStarted_ = false;
    }

    MPP_CHN_S vi = {MOD_ID_VIU, viDevice_, rtsp_.viChannel};
    MPP_CHN_S venc = {MOD_ID_VENC, 0, rtsp_.vencChannel};
    if (viVencBound_) {
        retainMpp("rtsp_unbind_vi_venc_failed", AW_MPI_SYS_UnBind(&vi, &venc));
        viVencBound_ = false;
    }
    if (vencCreated_) {
        retainMpp("rtsp_venc_destroy_failed",
                  AW_MPI_VENC_DestroyChn(rtsp_.vencChannel));
        vencCreated_ = false;
    }
    if (viCreated_) {
        retainMpp("rtsp_vi_destroy_failed",
                  AW_MPI_VI_DestroyVirChn(viDevice_, rtsp_.viChannel));
        viCreated_ = false;
    }
    viDevice_ = -1;
    return firstFailure;
}

ERRORTYPE RtspOutput::vencCallback(void* cookie, MPP_CHN_S* channel,
                                      MPP_EVENT_TYPE event, void* eventData) {
    auto* self = static_cast<RtspOutput*>(cookie);
    if (self == nullptr || channel == nullptr || channel->mModId != MOD_ID_VENC ||
        event != MPP_EVENT_LINKAGE_ISP2VE_PARAM) {
        return SUCCESS;
    }
    const ERRORTYPE result = updateVencIspParameters(
        self->viDevice_, channel->mChnId, eventData);
    if (result != SUCCESS) {
        self->postFaultOnce("rtsp_isp_link_failed", std::to_string(result));
    }
    return result;
}

}  // namespace mpp
}  // namespace camera
