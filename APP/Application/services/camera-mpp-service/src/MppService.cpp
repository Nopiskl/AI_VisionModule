#include "camera/mpp/MppService.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace camera {
namespace mpp {
namespace {

Status ensureDirectory(const std::string& path) {
    if (path.empty() || path.front() != '/') {
        return Status::failure("directory_invalid", path);
    }
    std::string current;
    std::size_t offset = 1;
    while (offset <= path.size()) {
        const auto slash = path.find('/', offset);
        const std::string part = path.substr(
            offset, slash == std::string::npos ? std::string::npos : slash - offset);
        if (!part.empty()) {
            current += "/" + part;
            struct stat metadata {};
            if (stat(current.c_str(), &metadata) == 0) {
                if (!S_ISDIR(metadata.st_mode)) {
                    return Status::failure("directory_conflict", current);
                }
            } else if (errno != ENOENT || mkdir(current.c_str(), 0755) != 0) {
                return Status::failure("directory_create_failed",
                                       current + ": " + std::strerror(errno));
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        offset = slash + 1;
    }
    return Status::success();
}

Status requireDirectory(const std::string& path) {
    struct stat metadata {};
    if (stat(path.c_str(), &metadata) != 0) {
        return Status::failure("directory_unavailable",
                               path + ": " + std::strerror(errno));
    }
    if (!S_ISDIR(metadata.st_mode)) {
        return Status::failure("directory_not_directory", path);
    }
    return Status::success();
}

std::string parentPath(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "." :
           slash == 0 ? "/" : path.substr(0, slash);
}

bool safeFilename(const std::string& filename) {
    if (filename.empty() || filename == "." || filename == "..") {
        return false;
    }
    return std::all_of(filename.begin(), filename.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
    });
}

std::string timestampName(const std::string& prefix, const std::string& extension,
                          std::uint64_t requestId) {
    std::time_t now = std::time(nullptr);
    std::tm time {};
    localtime_r(&now, &time);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &time);
    return prefix + "-" + buffer + "-" + std::to_string(requestId) + extension;
}

bool insideRoot(const std::string& path, const std::string& root) {
    if (root == "/") {
        return !path.empty() && path.front() == '/';
    }
    if (path == root) {
        return true;
    }
    return path.size() > root.size() &&
           path.compare(0, root.size(), root) == 0 && path[root.size()] == '/';
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

std::string playbackCodecList(const PlaybackConfig& config) {
    std::string result;
    const auto append = [&result](const char* codec) {
        if (!result.empty()) {
            result += ',';
        }
        result += codec;
    };
    if (SUNXI_MPP_VDEC_H264 && config.allowH264 != 0) {
        append("h264");
    }
    if (SUNXI_MPP_VDEC_H265 && config.allowH265 != 0) {
        append("h265");
    }
    if (SUNXI_MPP_VDEC_JPEG && config.allowMjpeg != 0) {
        append("mjpeg");
    }
    return result;
}

bool isUvcPipelineEvent(PipelineEventType type) {
    switch (type) {
        case PipelineEventType::UvcConnected:
        case PipelineEventType::UvcDisconnected:
        case PipelineEventType::UvcCommitted:
        case PipelineEventType::UvcStreamingStarted:
        case PipelineEventType::UvcStreamingStopped:
        case PipelineEventType::UvcFault:
            return true;
        default:
            return false;
    }
}

}  // namespace

const char* toString(ServiceMode mode) {
    switch (mode) {
        case ServiceMode::None:
            return "none";
        case ServiceMode::Camera:
            return "camera";
        case ServiceMode::Playback:
            return "playback";
        case ServiceMode::Uvc:
            return "uvc";
    }
    return "unknown";
}

MppService::MppService(ServiceConfig config)
    : config_(std::move(config)),
      mailbox_(64),
      camera_(config_, mailbox_),
      playback_(config_, mailbox_),
      uvc_(config_.uvc, mailbox_) {}

MppService::~MppService() {
    shutdown();
}

Status MppService::initialize() {
    if (initialized_) {
        return Status::success();
    }
    Status status = config_.validate();
    if (!status.ok) {
        return status;
    }
    status = ensureDirectory(parentPath(config_.socketPath));
    if (!status.ok) {
        return status;
    }
    status = ensureDirectory(parentPath(config_.backendLockPath));
    if (!status.ok) {
        return status;
    }
    status = backendLock_.acquire(config_.backendLockPath, "camera-mpp-service");
    if (!status.ok) {
        return status;
    }
    status = runtime_.start(config_.sysAlignWidth);
    if (!status.ok) {
        backendLock_.release();
        return status;
    }
    initialized_ = true;
    shutdownRequested_ = false;
    recordClockActive_ = false;
    return Status::success();
}

Status MppService::shutdown() noexcept {
    finishSnapshot();
    Status firstFailure = leaveMode();
    const Status runtimeStatus = runtime_.stop();
    if (firstFailure.ok && !runtimeStatus.ok) {
        firstFailure = runtimeStatus;
    }
    backendLock_.release();
    initialized_ = false;
    return firstFailure;
}

std::string MppService::stateName() const {
    if (!initialized_) {
        return "stopped";
    }
    if (mode_ == ServiceMode::Camera) {
        return camera_.recording() ? "recording" :
               camera_.active() ? "preview" : "starting";
    }
    if (mode_ == ServiceMode::Playback) {
        return toString(playback_.state());
    }
    if (mode_ == ServiceMode::Uvc) {
        return uvc_.stateName();
    }
    return "idle";
}

std::uint64_t MppService::recordElapsedMs() const {
    if (!recordClockActive_ || !camera_.recording()) {
        return 0;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - recordStartedAt_);
    return elapsed.count() > 0
               ? static_cast<std::uint64_t>(elapsed.count())
               : 0;
}

Status MppService::enterMode(ServiceMode requestedMode) {
    if (!initialized_) {
        return Status::failure("service_not_initialized", "MPP runtime is stopped");
    }
    if (requestedMode == ServiceMode::None) {
        return leaveMode();
    }
    if (mode_ == requestedMode) {
        return Status::success();
    }
    Status status = leaveMode();
    if (!status.ok) {
        return status;
    }
    if (requestedMode == ServiceMode::Camera) {
        status = camera_.start();
    } else if (requestedMode == ServiceMode::Playback) {
        status = Status::success();
    } else if (requestedMode == ServiceMode::Uvc) {
        status = uvc_.start();
    }
    if (status.ok) {
        mode_ = requestedMode;
    }
    return status;
}

Status MppService::startSnapshot(const std::string& path) {
    if (snapshotBusy_.load()) {
        return Status::failure("snapshot_busy", "another snapshot is in progress");
    }
    reapSnapshot();
    snapshotBusy_.store(true);
    try {
        snapshotThread_ = std::thread([this, path] {
            const Status status = camera_.takeSnapshot(path);
            snapshotBusy_.store(false);
            if (status.ok) {
                mailbox_.post({PipelineEventType::SnapshotCompleted,
                               "snapshot_completed", path});
            } else {
                mailbox_.post({PipelineEventType::PipelineError,
                               status.code, status.detail});
            }
        });
    } catch (const std::exception& error) {
        snapshotBusy_.store(false);
        return Status::failure("snapshot_thread_failed", error.what());
    }
    return Status::success();
}

void MppService::finishSnapshot() noexcept {
    if (snapshotThread_.joinable()) {
        snapshotThread_.join();
    }
    snapshotBusy_.store(false);
}

void MppService::reapSnapshot() noexcept {
    if (!snapshotBusy_.load() && snapshotThread_.joinable()) {
        snapshotThread_.join();
    }
}

Status MppService::leaveMode() noexcept {
    Status status = Status::success();
    if (mode_ == ServiceMode::Camera || camera_.active() || camera_.recording()) {
        status = camera_.stop();
        recordClockActive_ = false;
    } else if (mode_ == ServiceMode::Playback ||
               playback_.state() != PlaybackState::Idle) {
        status = playback_.close();
    } else if (mode_ == ServiceMode::Uvc || uvc_.active()) {
        status = uvc_.stop();
    }
    mode_ = ServiceMode::None;
    return status;
}

ipc::Message MppService::handle(const ipc::Message& request) {
    reapSnapshot();
    if (request.type != ipc::MessageType::Request) {
        return response(request, Status::failure("not_a_request", request.name));
    }
    if (request.version != ipc::kProtocolVersion) {
        return response(request,
                        Status::failure("version_unsupported",
                                        std::to_string(request.version)));
    }

    Status status = Status::success();
    ipc::Message result;
    if (request.name == "hello" || request.name == "get_capabilities") {
        result = response(request, status);
        result.fields["protocol_version"] = std::to_string(ipc::kProtocolVersion);
        result.fields["camera"] = "1";
        result.fields["snapshot"] = "1";
        result.fields["record"] = "1";
        result.fields["playback"] = "1";
        result.fields["uvc"] = config_.uvc.enabled != 0 ? "1" : "0";
        result.fields["rtsp"] = config_.rtsp.enabled != 0 ? "1" : "0";
        result.fields["audio"] = "0";
        result.fields["seek"] = "0";
        result.fields["record_width"] = std::to_string(config_.record.width);
        result.fields["record_height"] = std::to_string(config_.record.height);
        result.fields["display_width"] = std::to_string(config_.display.width);
        result.fields["display_height"] = std::to_string(config_.display.height);
        result.fields["display_interface"] = config_.display.interfaceType;
        result.fields["video_layer"] = std::to_string(config_.display.videoLayer);
        result.fields["ui_layer"] = std::to_string(config_.display.uiOutsideLayer);
        result.fields["storage_root"] = config_.storageRoot;
        result.fields["playback_media_root"] = config_.mediaRoot;
        result.fields["playback_display_x"] =
            std::to_string(config_.playback.displayX);
        result.fields["playback_display_y"] =
            std::to_string(config_.playback.displayY);
        result.fields["playback_display_width"] =
            std::to_string(config_.playback.displayWidth);
        result.fields["playback_display_height"] =
            std::to_string(config_.playback.displayHeight);
        result.fields["playback_codecs"] = playbackCodecList(config_.playback);
        result.fields["playback_max_fps"] =
            std::to_string(config_.playback.maxFrameRate);
        result.fields["playback_max_bit_rate"] =
            std::to_string(config_.playback.maxBitRate);
        result.fields["uvc_device"] = config_.uvc.videoDevice;
        result.fields["uvc_formats"] =
            "mjpeg:1920x1080,1280x720,640x480;"
            "yuyv:320x240;h264:1920x1080,1280x720;fps=30";
        return result;
    }
    if (request.name == "get_status") {
        result = response(request, status);
        if (mode_ == ServiceMode::Playback) {
            result.fields["position_ms"] = std::to_string(playback_.positionMs());
            result.fields["duration_ms"] = std::to_string(playback_.durationMs());
            result.fields["media"] = playback_.sourcePath();
            if (playback_.state() != PlaybackState::Idle) {
                result.fields["display_x"] = std::to_string(playback_.displayX());
                result.fields["display_y"] = std::to_string(playback_.displayY());
                result.fields["display_width"] =
                    std::to_string(playback_.displayWidth());
                result.fields["display_height"] =
                    std::to_string(playback_.displayHeight());
            }
        }
        if (mode_ == ServiceMode::Camera && camera_.recording()) {
            result.fields["record_path"] = camera_.recordPath();
        }
        if (mode_ == ServiceMode::Camera && camera_.rtspActive()) {
            result.fields["rtsp_url"] = camera_.rtspUrl();
        }
        return result;
    }
    if (request.name == "enter_mode") {
        if (snapshotBusy_.load()) {
            status = Status::failure(
                "snapshot_busy", "wait for snapshot completion before mode switch");
        } else {
            const std::string requested = request.field("mode");
            if (requested == "camera") {
                status = enterMode(ServiceMode::Camera);
            } else if (requested == "playback") {
                status = enterMode(ServiceMode::Playback);
            } else if (requested == "uvc") {
                status = config_.uvc.enabled != 0
                             ? enterMode(ServiceMode::Uvc)
                             : Status::failure("capability_disabled",
                                               "UVC is disabled");
            } else {
                status = Status::failure("invalid_mode", requested);
            }
        }
    } else if (request.name == "leave_mode") {
        status = snapshotBusy_.load()
                     ? Status::failure("snapshot_busy", "snapshot is in progress")
                     : leaveMode();
    } else if (request.name == "take_snapshot") {
        if (mode_ != ServiceMode::Camera) {
            status = Status::failure("wrong_mode", "Camera mode is required");
        } else if (camera_.recording()) {
            status = Status::failure(
                "snapshot_during_record_unsupported",
                "stop recording before taking a snapshot");
        } else {
            std::string path;
            status = makeOutputPath(request, "snapshot", ".jpg", &path);
            if (status.ok) {
                status = startSnapshot(path);
                if (status.ok) {
                    ipc::Message event = ipc::makeEvent("snapshot_started");
                    event.fields["path"] = path;
                    emit(std::move(event));
                }
            }
        }
    } else if (request.name == "start_record") {
        if (mode_ != ServiceMode::Camera) {
            status = Status::failure("wrong_mode", "Camera mode is required");
        } else if (snapshotBusy_.load()) {
            status = Status::failure("snapshot_busy", "snapshot is in progress");
        } else {
            std::string path;
            status = makeOutputPath(request, "record", ".mp4", &path);
            if (status.ok) {
                status = camera_.startRecord(path);
                if (status.ok) {
                    recordStartedAt_ = std::chrono::steady_clock::now();
                    recordClockActive_ = true;
                    ipc::Message event = ipc::makeEvent("record_started");
                    event.fields["path"] = path;
                    event.fields["width"] = "1280";
                    event.fields["height"] = "720";
                    event.fields["record_elapsed_ms"] = "0";
                    addStatusFields(&event);
                    emit(std::move(event));
                }
            }
        }
    } else if (request.name == "stop_record") {
        if (mode_ != ServiceMode::Camera) {
            status = Status::failure("wrong_mode", "Camera mode is required");
        } else {
            const std::string path = camera_.recordPath();
            status = camera_.stopRecord();
            if (!camera_.recording()) {
                recordClockActive_ = false;
            }
            if (status.ok) {
                ipc::Message event = ipc::makeEvent("record_stopped");
                event.fields["path"] = path;
                addStatusFields(&event);
                emit(std::move(event));
            }
        }
    } else if (request.name == "start_rtsp") {
        if (mode_ != ServiceMode::Camera) {
            status = Status::failure("wrong_mode", "Camera mode is required");
        } else if (config_.rtsp.enabled == 0) {
            status = Status::failure("capability_disabled", "RTSP is disabled");
        } else {
            status = camera_.startRtsp();
            if (status.ok) {
                ipc::Message event = ipc::makeEvent("rtsp_started");
                event.fields["url"] = camera_.rtspUrl();
                event.fields["width"] = std::to_string(config_.rtsp.width);
                event.fields["height"] = std::to_string(config_.rtsp.height);
                event.fields["frame_rate"] =
                    std::to_string(config_.rtsp.frameRate);
                emit(std::move(event));
            }
        }
    } else if (request.name == "stop_rtsp") {
        if (mode_ != ServiceMode::Camera) {
            status = Status::failure("wrong_mode", "Camera mode is required");
        } else {
            const std::string url = camera_.rtspUrl();
            status = camera_.stopRtsp();
            if (status.ok) {
                ipc::Message event = ipc::makeEvent("rtsp_stopped");
                event.fields["url"] = url;
                emit(std::move(event));
            }
        }
    } else if (request.name == "load_media") {
        std::string path;
        status = snapshotBusy_.load()
                     ? Status::failure("snapshot_busy", "snapshot is in progress")
                     : validateMediaPath(request.field("path"), &path);
        if (status.ok) {
            status = enterMode(ServiceMode::Playback);
        }
        if (status.ok && playback_.state() != PlaybackState::Idle) {
            status = playback_.close();
        }
        if (status.ok) {
            status = playback_.load(path);
        }
        if (status.ok) {
            ipc::Message event = ipc::makeEvent("media_loaded");
            event.fields["path"] = path;
            event.fields["duration_ms"] = std::to_string(playback_.durationMs());
            event.fields["source_width"] = std::to_string(playback_.sourceWidth());
            event.fields["source_height"] = std::to_string(playback_.sourceHeight());
            event.fields["codec_id"] = std::to_string(playback_.codec());
            event.fields["codec"] = playback_.codecName();
            event.fields["frame_rate_milli_fps"] =
                std::to_string(playback_.sourceFrameRateMilliFps());
            event.fields["average_bit_rate"] =
                std::to_string(playback_.sourceAverageBitRate());
            event.fields["maximum_bit_rate"] =
                std::to_string(playback_.sourceMaximumBitRate());
            event.fields["display_x"] = std::to_string(playback_.displayX());
            event.fields["display_y"] = std::to_string(playback_.displayY());
            event.fields["display_width"] =
                std::to_string(playback_.displayWidth());
            event.fields["display_height"] =
                std::to_string(playback_.displayHeight());
            emit(std::move(event));
        }
    } else if (request.name == "play") {
        status = mode_ == ServiceMode::Playback
                     ? playback_.play()
                     : Status::failure("wrong_mode", "Playback mode is required");
    } else if (request.name == "pause") {
        status = mode_ == ServiceMode::Playback
                     ? playback_.pause()
                     : Status::failure("wrong_mode", "Playback mode is required");
    } else if (request.name == "resume") {
        status = mode_ == ServiceMode::Playback
                     ? playback_.resume()
                     : Status::failure("wrong_mode", "Playback mode is required");
    } else if (request.name == "stop_playback") {
        status = mode_ == ServiceMode::Playback
                     ? playback_.close()
                     : Status::failure("wrong_mode", "Playback mode is required");
    } else if (request.name == "shutdown") {
        if (snapshotBusy_.load()) {
            status = Status::failure("snapshot_busy", "snapshot is in progress");
        } else {
            finishSnapshot();
            status = leaveMode();
            shutdownRequested_ = true;
        }
    } else if (request.name == "seek") {
        status = Status::failure("capability_deferred", "Playback seek is deferred");
    } else {
        status = Status::failure("unknown_command", request.name);
    }

    result = response(request, status);
    if (status.ok && request.name != "get_status") {
        ipc::Message event = ipc::makeEvent("state_changed");
        addStatusFields(&event);
        emit(std::move(event));
    }
    return result;
}

void MppService::drainPipelineEvents() {
    for (const PipelineEvent& pipelineEvent : mailbox_.drain()) {
        // UVC owns its own event thread. A final event may already be queued
        // when leaveMode() joins that thread; never project such an event into
        // the Camera/Playback mode that acquired the service afterwards.
        if (isUvcPipelineEvent(pipelineEvent.type) &&
            mode_ != ServiceMode::Uvc) {
            continue;
        }
        ipc::Message event;
        switch (pipelineEvent.type) {
            case PipelineEventType::PlaybackEof:
                event = ipc::makeEvent("playback_eof");
                event.fields["path"] = playback_.sourcePath();
                playback_.markEnded();
                break;
            case PipelineEventType::RecordDone:
                event = ipc::makeEvent("record_file_done");
                event.fields["path"] = camera_.recordPath();
                break;
            case PipelineEventType::StorageFault: {
                if (!camera_.recording() ||
                    pipelineEvent.generation != camera_.recordGeneration()) {
                    continue;
                }
                const std::string path = camera_.recordPath();
                const Status stopStatus = camera_.recording()
                                              ? camera_.stopRecord()
                                              : Status::success();
                recordClockActive_ = false;
                event = ipc::makeEvent("record_interrupted");
                event.fields["path"] = path;
                event.fields["code"] = pipelineEvent.code;
                event.fields["detail"] = pipelineEvent.detail;
                event.fields["committed"] = stopStatus.ok ? "1" : "0";
                if (!stopStatus.ok) {
                    event.fields["stop_code"] = stopStatus.code;
                    event.fields["stop_detail"] = stopStatus.detail;
                }
                break;
            }
            case PipelineEventType::RtspFault: {
                if (!camera_.rtspActive() ||
                    pipelineEvent.generation != camera_.rtspGeneration()) {
                    continue;
                }
                const std::string url = camera_.rtspUrl();
                const Status stopStatus = camera_.rtspActive()
                                              ? camera_.stopRtsp()
                                              : Status::success();
                event = ipc::makeEvent("rtsp_error");
                event.fields["url"] = url;
                event.fields["code"] = pipelineEvent.code;
                event.fields["detail"] = pipelineEvent.detail;
                if (!stopStatus.ok) {
                    event.fields["stop_code"] = stopStatus.code;
                    event.fields["stop_detail"] = stopStatus.detail;
                }
                break;
            }
            case PipelineEventType::UvcConnected:
                event = ipc::makeEvent("uvc_connected");
                event.fields["device"] = pipelineEvent.detail;
                break;
            case PipelineEventType::UvcDisconnected:
                event = ipc::makeEvent("uvc_disconnected");
                event.fields["device"] = pipelineEvent.detail;
                break;
            case PipelineEventType::UvcCommitted:
                event = ipc::makeEvent("uvc_committed");
                event.fields["profile"] = pipelineEvent.detail;
                break;
            case PipelineEventType::UvcStreamingStarted:
                event = ipc::makeEvent("uvc_streaming_started");
                event.fields["profile"] = pipelineEvent.detail;
                break;
            case PipelineEventType::UvcStreamingStopped:
                event = ipc::makeEvent("uvc_streaming_stopped");
                event.fields["device"] = pipelineEvent.detail;
                break;
            case PipelineEventType::UvcFault:
                event = ipc::makeEvent("uvc_error");
                event.fields["code"] = pipelineEvent.code;
                event.fields["detail"] = pipelineEvent.detail;
                break;
            case PipelineEventType::SnapshotCompleted:
                event = ipc::makeEvent("snapshot_completed");
                event.fields["path"] = pipelineEvent.detail;
                break;
            case PipelineEventType::VoRenderingStarted:
                event = ipc::makeEvent("rendering_started");
                event.fields["source"] = pipelineEvent.detail;
                break;
            case PipelineEventType::ViTimeout:
            case PipelineEventType::PipelineError:
                event = ipc::makeEvent("backend_error");
                event.fields["code"] = pipelineEvent.code;
                event.fields["detail"] = pipelineEvent.detail;
                break;
        }
        addStatusFields(&event);
        emit(std::move(event));
    }
}

Status MppService::validateMediaPath(const std::string& requested,
                                     std::string* resolved) const {
    if (requested.empty()) {
        return Status::failure("media_path_missing", "path is required");
    }
    if (requested.find('\0') != std::string::npos) {
        return Status::failure("media_path_invalid", "path contains a NUL byte");
    }
    char mediaBuffer[PATH_MAX];
    char rootBuffer[PATH_MAX];
    if (realpath(requested.c_str(), mediaBuffer) == nullptr) {
        return Status::failure("media_path_invalid", std::strerror(errno));
    }
    if (realpath(config_.mediaRoot.c_str(), rootBuffer) == nullptr) {
        return Status::failure("media_root_invalid", std::strerror(errno));
    }
    struct stat metadata {};
    if (stat(mediaBuffer, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        return Status::failure("media_not_regular_file", mediaBuffer);
    }
    if (!insideRoot(mediaBuffer, rootBuffer)) {
        return Status::failure("media_outside_root", mediaBuffer);
    }
    *resolved = mediaBuffer;
    return Status::success();
}

Status MppService::makeOutputPath(const ipc::Message& request,
                                  const std::string& prefix,
                                  const std::string& extension,
                                  std::string* output) const {
    // Camera Preview must remain available without removable media, but never
    // create the mount point itself on the root filesystem.
    Status storageStatus = requireDirectory(config_.mediaRoot);
    if (!storageStatus.ok) {
        return storageStatus;
    }
    if (config_.requireMediaRootMount != 0 &&
        !isMountPoint(config_.mediaRoot)) {
        return Status::failure(
            "storage_not_mounted",
            config_.mediaRoot + " is not a mounted filesystem");
    }
    storageStatus = ensureDirectory(config_.storageRoot);
    if (!storageStatus.ok) {
        return storageStatus;
    }

    char mediaBuffer[PATH_MAX];
    char storageBuffer[PATH_MAX];
    if (realpath(config_.mediaRoot.c_str(), mediaBuffer) == nullptr) {
        return Status::failure("media_root_invalid", std::strerror(errno));
    }
    if (realpath(config_.storageRoot.c_str(), storageBuffer) == nullptr) {
        return Status::failure("storage_root_invalid", std::strerror(errno));
    }
    if (!insideRoot(storageBuffer, mediaBuffer)) {
        return Status::failure("storage_outside_media_root", storageBuffer);
    }

    std::string filename = request.field("filename");
    if (filename.empty()) {
        filename = timestampName(prefix, extension, request.requestId);
    }
    if (!safeFilename(filename) || filename.size() > 128) {
        return Status::failure("filename_invalid", filename);
    }
    if (filename.size() < extension.size() ||
        filename.compare(filename.size() - extension.size(), extension.size(),
                         extension) != 0) {
        filename += extension;
    }
    *output = std::string(storageBuffer) + "/" + filename;
    struct stat metadata {};
    if (lstat(output->c_str(), &metadata) == 0) {
        return Status::failure("output_exists", *output);
    }
    if (errno != ENOENT) {
        return Status::failure("output_check_failed", std::strerror(errno));
    }
    return Status::success();
}

void MppService::emit(ipc::Message event) const {
    if (eventSink_) {
        eventSink_(event);
    }
}

ipc::Message MppService::response(const ipc::Message& request,
                                  const Status& status) const {
    ipc::Message result =
        ipc::makeResponse(request, status.ok, status.code, status.detail);
    addStatusFields(&result);
    return result;
}

void MppService::addStatusFields(ipc::Message* message) const {
    message->fields["mode"] = toString(mode_);
    message->fields["state"] = stateName();
    message->fields["mailbox_dropped"] = std::to_string(mailbox_.droppedCount());
    message->fields["snapshot_busy"] = snapshotBusy_.load() ? "1" : "0";
    message->fields["record_elapsed_ms"] =
        std::to_string(recordElapsedMs());
    message->fields["rtsp_active"] = camera_.rtspActive() ? "1" : "0";
    if (camera_.rtspActive()) {
        message->fields["rtsp_url"] = camera_.rtspUrl();
    }
    message->fields["uvc_host_connected"] =
        uvc_.hostConnected() ? "1" : "0";
    message->fields["uvc_streaming"] = uvc_.streaming() ? "1" : "0";
    message->fields["uvc_device"] = uvc_.devicePath();
    message->fields["uvc_format"] = uvc_.formatName();
    message->fields["uvc_width"] = std::to_string(uvc_.width());
    message->fields["uvc_height"] = std::to_string(uvc_.height());
    message->fields["uvc_frame_rate"] = std::to_string(uvc_.frameRate());
    message->fields["uvc_dropped_frames"] =
        std::to_string(uvc_.droppedFrames());
}

}  // namespace mpp
}  // namespace camera
