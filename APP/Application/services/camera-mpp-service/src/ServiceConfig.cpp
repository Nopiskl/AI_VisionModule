#include "camera/mpp/ServiceConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace camera {
namespace mpp {
namespace {

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    return first >= last ? std::string{} : std::string(first, last);
}

bool parseInt(const std::map<std::string, std::string>& values,
              const std::string& key, int* output, std::string* error) {
    const auto found = values.find(key);
    if (found == values.end()) {
        return true;
    }
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(found->second.c_str(), &end, 10);
    if (errno != 0 || end == found->second.c_str() || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
        *error = "invalid integer for " + key + ": " + found->second;
        return false;
    }
    *output = static_cast<int>(parsed);
    return true;
}

void parseString(const std::map<std::string, std::string>& values,
                 const std::string& key, std::string* output) {
    const auto found = values.find(key);
    if (found != values.end()) {
        *output = found->second;
    }
}

bool inRange(int value, int minimum, int maximum) {
    return value >= minimum && value <= maximum;
}

bool isSafeToken(const std::string& value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
           });
}

bool containsParentTraversal(const std::string& path) {
    return path == ".." || path.compare(0, 3, "../") == 0 ||
           path.find("/../") != std::string::npos ||
           (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0);
}

bool isVideoDevicePath(const std::string& path) {
    constexpr char kPrefix[] = "/dev/video";
    constexpr std::size_t kPrefixLength = sizeof(kPrefix) - 1;
    return path.size() > kPrefixLength &&
           path.compare(0, kPrefixLength, kPrefix) == 0 &&
           std::all_of(path.begin() + kPrefixLength, path.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool insideRoot(const std::string& path, const std::string& root) {
    if (root == "/") {
        return !path.empty() && path.front() == '/';
    }
    return path == root ||
           (path.size() > root.size() && path.compare(0, root.size(), root) == 0 &&
            path[root.size()] == '/');
}

}  // namespace

Status ServiceConfig::load(const std::string& path, ServiceConfig* output) {
    if (output == nullptr) {
        return Status::failure("invalid_argument", "output config is null");
    }

    std::ifstream stream(path);
    if (!stream) {
        return Status::failure("config_open_failed", "cannot open " + path);
    }

    std::map<std::string, std::string> values;
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            return Status::failure(
                "config_parse_failed",
                "line " + std::to_string(lineNumber) + " has no '='");
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key.empty() || value.empty()) {
            return Status::failure(
                "config_parse_failed",
                "line " + std::to_string(lineNumber) + " has an empty key/value");
        }
        if (!values.emplace(key, value).second) {
            return Status::failure("config_parse_failed", "duplicate key: " + key);
        }
    }

    ServiceConfig config;
    parseString(values, "ipc.socket_path", &config.socketPath);
    parseString(values, "ipc.backend_lock_path", &config.backendLockPath);
    parseString(values, "storage.root", &config.storageRoot);
    parseString(values, "playback.media_root", &config.mediaRoot);
    parseString(values, "camera.pixel_format", &config.camera.pixelFormat);
    parseString(values, "record.codec", &config.record.codec);
    parseString(values, "record.container", &config.record.container);
    parseString(values, "display.interface", &config.display.interfaceType);
    parseString(values, "display.sync", &config.display.interfaceSync);
    parseString(values, "display.scale_mode", &config.display.scaleMode);
    parseString(values, "rtsp.codec", &config.rtsp.codec);
    parseString(values, "rtsp.network_interface", &config.rtsp.networkInterface);
    parseString(values, "rtsp.stream_name", &config.rtsp.streamName);
    parseString(values, "uvc.video_device", &config.uvc.videoDevice);

    std::string error;
#define PARSE_INT(KEY, MEMBER)                                                  \
    do {                                                                        \
        if (!parseInt(values, KEY, &(MEMBER), &error)) {                        \
            return Status::failure("config_parse_failed", error);              \
        }                                                                       \
    } while (false)

    PARSE_INT("mpp.align_width", config.sysAlignWidth);
    PARSE_INT("storage.require_media_root_mount", config.requireMediaRootMount);
    PARSE_INT("display.vo_device", config.display.voDevice);
    PARSE_INT("display.video_layer", config.display.videoLayer);
    PARSE_INT("display.video_channel", config.display.videoChannel);
    PARSE_INT("display.ui_outside_layer", config.display.uiOutsideLayer);
    PARSE_INT("display.x", config.display.x);
    PARSE_INT("display.y", config.display.y);
    PARSE_INT("display.width", config.display.width);
    PARSE_INT("display.height", config.display.height);
    PARSE_INT("camera.isp_device", config.camera.ispDevice);
    PARSE_INT("camera.vipp_device", config.camera.vippDevice);
    PARSE_INT("camera.preview_channel", config.camera.previewChannel);
    PARSE_INT("camera.record_channel", config.camera.recordChannel);
    PARSE_INT("camera.capture_width", config.camera.captureWidth);
    PARSE_INT("camera.capture_height", config.camera.captureHeight);
    PARSE_INT("camera.media_vipp_device", config.camera.mediaVippDevice);
    PARSE_INT("camera.media_capture_width", config.camera.mediaCaptureWidth);
    PARSE_INT("camera.media_capture_height", config.camera.mediaCaptureHeight);
    PARSE_INT("camera.frame_rate", config.camera.frameRate);
    PARSE_INT("camera.vi_buffer_count", config.camera.viBufferCount);
    PARSE_INT("record.venc_channel", config.record.vencChannel);
    PARSE_INT("record.mux_group", config.record.muxGroup);
    PARSE_INT("record.mux_channel", config.record.muxChannel);
    PARSE_INT("record.width", config.record.width);
    PARSE_INT("record.height", config.record.height);
    PARSE_INT("record.frame_rate", config.record.frameRate);
    PARSE_INT("record.bit_rate", config.record.bitRate);
    PARSE_INT("record.vbv_buffer_bytes", config.record.vbvBufferBytes);
    PARSE_INT("record.vbv_threshold_bytes", config.record.vbvThresholdBytes);
    PARSE_INT("record.add_repair_info", config.record.addRepairInfo);
    PARSE_INT("record.repair_backup_interval_us",
              config.record.repairBackupIntervalUs);
    PARSE_INT("record.storage_poll_ms", config.record.storagePollMs);
    PARSE_INT("record.storage_reserve_bytes", config.record.storageReserveBytes);
    PARSE_INT("rtsp.enabled", config.rtsp.enabled);
    PARSE_INT("rtsp.vi_channel", config.rtsp.viChannel);
    PARSE_INT("rtsp.venc_channel", config.rtsp.vencChannel);
    PARSE_INT("rtsp.width", config.rtsp.width);
    PARSE_INT("rtsp.height", config.rtsp.height);
    PARSE_INT("rtsp.frame_rate", config.rtsp.frameRate);
    PARSE_INT("rtsp.bit_rate", config.rtsp.bitRate);
    PARSE_INT("rtsp.vbv_buffer_bytes", config.rtsp.vbvBufferBytes);
    PARSE_INT("rtsp.vbv_threshold_bytes", config.rtsp.vbvThresholdBytes);
    PARSE_INT("rtsp.max_frame_bytes", config.rtsp.maxFrameBytes);
    PARSE_INT("rtsp.port", config.rtsp.port);
    PARSE_INT("snapshot.venc_channel", config.snapshot.vencChannel);
    PARSE_INT("snapshot.width", config.snapshot.width);
    PARSE_INT("snapshot.height", config.snapshot.height);
    PARSE_INT("snapshot.quality", config.snapshot.quality);
    PARSE_INT("snapshot.timeout_ms", config.snapshot.timeoutMs);
    PARSE_INT("playback.demux_channel", config.playback.demuxChannel);
    PARSE_INT("playback.vdec_channel", config.playback.vdecChannel);
    PARSE_INT("playback.clock_channel", config.playback.clockChannel);
    PARSE_INT("playback.max_source_width", config.playback.maxSourceWidth);
    PARSE_INT("playback.max_source_height", config.playback.maxSourceHeight);
    PARSE_INT("playback.max_frame_rate", config.playback.maxFrameRate);
    PARSE_INT("playback.max_bit_rate", config.playback.maxBitRate);
    PARSE_INT("playback.allow_h264", config.playback.allowH264);
    PARSE_INT("playback.allow_h265", config.playback.allowH265);
    PARSE_INT("playback.allow_mjpeg", config.playback.allowMjpeg);
    PARSE_INT("playback.support_b_frames", config.playback.supportBFrames);
    PARSE_INT("playback.force_frame_package", config.playback.forceFramePackage);
    PARSE_INT("playback.ve_frequency_mhz", config.playback.veFrequencyMHz);
    PARSE_INT("playback.display_x", config.playback.displayX);
    PARSE_INT("playback.display_y", config.playback.displayY);
    PARSE_INT("playback.display_width", config.playback.displayWidth);
    PARSE_INT("playback.display_height", config.playback.displayHeight);
    PARSE_INT("uvc.enabled", config.uvc.enabled);
    PARSE_INT("uvc.bulk_mode", config.uvc.bulkMode);
    PARSE_INT("uvc.isp_device", config.uvc.ispDevice);
    PARSE_INT("uvc.vipp_device", config.uvc.vippDevice);
    PARSE_INT("uvc.vi_channel", config.uvc.viChannel);
    PARSE_INT("uvc.venc_channel", config.uvc.vencChannel);
    PARSE_INT("uvc.vi_buffer_count", config.uvc.viBufferCount);
    PARSE_INT("uvc.gadget_buffer_count", config.uvc.gadgetBufferCount);
    PARSE_INT("uvc.frame_queue_depth", config.uvc.frameQueueDepth);
    PARSE_INT("uvc.frame_timeout_ms", config.uvc.frameTimeoutMs);
    PARSE_INT("uvc.mjpeg_bit_rate", config.uvc.mjpegBitRate);
    PARSE_INT("uvc.h264_bit_rate", config.uvc.h264BitRate);
    PARSE_INT("uvc.venc_vbv_buffer_bytes", config.uvc.vencVbvBufferBytes);
    PARSE_INT("uvc.venc_vbv_threshold_bytes",
              config.uvc.vencVbvThresholdBytes);
    PARSE_INT("uvc.max_frame_bytes", config.uvc.maxFrameBytes);
#undef PARSE_INT

    for (const auto& value : values) {
        static const char* const known[] = {
            "ipc.socket_path", "ipc.backend_lock_path", "storage.root",
            "storage.require_media_root_mount",
            "mpp.align_width", "display.vo_device", "display.video_layer",
            "display.video_channel", "display.ui_outside_layer", "display.x",
            "display.y", "display.width", "display.height",
            "display.interface", "display.sync", "display.scale_mode",
            "camera.isp_device", "camera.vipp_device", "camera.preview_channel",
            "camera.record_channel", "camera.capture_width",
            "camera.capture_height", "camera.media_vipp_device",
            "camera.media_capture_width", "camera.media_capture_height", "camera.frame_rate",
            "camera.vi_buffer_count", "camera.pixel_format",
            "record.venc_channel", "record.mux_group", "record.mux_channel",
            "record.width", "record.height", "record.frame_rate",
            "record.bit_rate", "record.vbv_buffer_bytes",
            "record.vbv_threshold_bytes", "record.codec", "record.container",
            "record.add_repair_info", "record.repair_backup_interval_us",
            "record.storage_poll_ms", "record.storage_reserve_bytes",
            "rtsp.enabled", "rtsp.vi_channel", "rtsp.venc_channel",
            "rtsp.width", "rtsp.height", "rtsp.frame_rate", "rtsp.bit_rate",
            "rtsp.vbv_buffer_bytes", "rtsp.vbv_threshold_bytes",
            "rtsp.max_frame_bytes", "rtsp.port", "rtsp.codec",
            "rtsp.network_interface", "rtsp.stream_name",
            "snapshot.venc_channel", "snapshot.width", "snapshot.height",
            "snapshot.quality", "snapshot.timeout_ms", "playback.media_root",
            "playback.demux_channel", "playback.vdec_channel",
            "playback.clock_channel", "playback.max_source_width",
            "playback.max_source_height", "playback.max_frame_rate",
            "playback.max_bit_rate", "playback.allow_h264",
            "playback.allow_h265", "playback.allow_mjpeg",
            "playback.support_b_frames", "playback.force_frame_package",
            "playback.ve_frequency_mhz", "playback.display_x",
            "playback.display_y", "playback.display_width",
            "playback.display_height", "uvc.enabled", "uvc.video_device",
            "uvc.bulk_mode", "uvc.isp_device", "uvc.vipp_device",
            "uvc.vi_channel", "uvc.venc_channel", "uvc.vi_buffer_count",
            "uvc.gadget_buffer_count", "uvc.frame_queue_depth",
            "uvc.frame_timeout_ms", "uvc.mjpeg_bit_rate",
            "uvc.h264_bit_rate", "uvc.venc_vbv_buffer_bytes",
            "uvc.venc_vbv_threshold_bytes", "uvc.max_frame_bytes"};
        if (std::find(std::begin(known), std::end(known), value.first) ==
            std::end(known)) {
            return Status::failure("config_unknown_key", value.first);
        }
    }

    const auto status = config.validate();
    if (!status.ok) {
        return status;
    }
    *output = std::move(config);
    return Status::success();
}

Status ServiceConfig::validate() const {
    if (record.muxGroup != 0) {
        return Status::failure("config_mux_group_unsupported",
            "current SDK uses record.mux_channel; legacy record.mux_group must be 0");
    }

    if (socketPath.empty() || socketPath.front() != '/' ||
        backendLockPath.empty() || backendLockPath.front() != '/') {
        return Status::failure("config_invalid", "IPC paths must be absolute");
    }
    if (storageRoot.empty() || storageRoot.front() != '/' || mediaRoot.empty() ||
        mediaRoot.front() != '/') {
        return Status::failure("config_invalid", "storage/media roots must be absolute");
    }
    if (containsParentTraversal(storageRoot) || containsParentTraversal(mediaRoot) ||
        !insideRoot(storageRoot, mediaRoot)) {
        return Status::failure(
            "config_invalid", "storage.root must be inside playback.media_root");
    }
    if (sysAlignWidth <= 0 || (sysAlignWidth & (sysAlignWidth - 1)) != 0) {
        return Status::failure("config_invalid", "mpp.align_width must be a power of two");
    }
    if (display.voDevice < 0 || display.videoLayer < 0 ||
        display.videoChannel < 0 || display.uiOutsideLayer < 0 ||
        display.x < 0 || display.y < 0 || display.width <= 0 ||
        display.height <= 0 ||
        static_cast<std::int64_t>(display.x) + display.width >
            std::numeric_limits<int>::max() ||
        static_cast<std::int64_t>(display.y) + display.height >
            std::numeric_limits<int>::max()) {
        return Status::failure("config_invalid", "invalid display device/layer/channel");
    }
    if (display.uiOutsideLayer == display.videoLayer) {
        return Status::failure(
            "config_invalid", "UI and video layer handles must be different");
    }
    if (display.scaleMode != "contain" && display.scaleMode != "stretch") {
        return Status::failure("config_unsupported",
                               "display.scale_mode must be contain or stretch");
    }
    if (display.interfaceType != "lcd" && display.interfaceType != "hdmi" &&
        display.interfaceType != "cvbs") {
        return Status::failure("config_unsupported", "unsupported display.interface");
    }
    if (display.interfaceSync != "ntsc" &&
        display.interfaceSync != "720p60" &&
        display.interfaceSync != "1080p30" &&
        display.interfaceSync != "2160p30") {
        return Status::failure("config_unsupported", "unsupported display.sync");
    }
    if (rtsp.enabled != 0 && rtsp.enabled != 1) {
        return Status::failure("config_invalid", "rtsp.enabled must be 0 or 1");
    }
    if (!inRange(camera.captureWidth, 16, 1920) ||
        !inRange(camera.captureHeight, 16, 1088) ||
        camera.captureWidth % 2 || camera.captureHeight % 2 ||
        camera.mediaVippDevice < 0 || camera.mediaVippDevice == camera.vippDevice) {
        return Status::failure("config_invalid", "invalid preview size or media VIPP");
    }
    if (camera.mediaCaptureWidth != 1280 || camera.mediaCaptureHeight != 720 ||
        record.width != 1280 || record.height != 720) {
        return Status::failure("config_invalid", "Media capture/Record must remain 1280x720");
    }
    if (camera.previewChannel == camera.recordChannel ||
        record.vencChannel == snapshot.vencChannel ||
        (rtsp.enabled != 0 &&
         (camera.previewChannel == rtsp.viChannel ||
          camera.recordChannel == rtsp.viChannel ||
          record.vencChannel == rtsp.vencChannel ||
          snapshot.vencChannel == rtsp.vencChannel))) {
        return Status::failure("config_invalid", "MPP channel IDs overlap");
    }
    if (camera.ispDevice < 0 || camera.vippDevice < 0 ||
        camera.previewChannel < 0 || camera.recordChannel < 0 ||
        record.vencChannel < 0 || record.muxGroup < 0 ||
        record.muxChannel < 0 || snapshot.vencChannel < 0 ||
        playback.demuxChannel < 0 || playback.vdecChannel < 0 ||
        playback.clockChannel < 0 || camera.viBufferCount <= 0 ||
        (rtsp.enabled != 0 &&
         (rtsp.viChannel < 0 || rtsp.vencChannel < 0))) {
        return Status::failure("config_invalid", "negative channel/device value");
    }
    if (!inRange(camera.frameRate, 1, 60) ||
        !inRange(record.frameRate, 1, camera.frameRate) || record.bitRate <= 0 ||
        !inRange(snapshot.quality, 1, 100) || snapshot.timeoutMs <= 0) {
        return Status::failure("config_invalid", "invalid FPS/bitrate/snapshot values");
    }
    if ((requireMediaRootMount != 0 && requireMediaRootMount != 1) ||
        (record.addRepairInfo != 0 && record.addRepairInfo != 1) ||
        record.repairBackupIntervalUs <= 0 || record.storagePollMs < 100 ||
        record.storageReserveBytes < 0) {
        return Status::failure("config_invalid", "invalid storage/repair policy");
    }
    if (camera.pixelFormat != "nv21") {
        return Status::failure("config_unsupported", "only nv21 is implemented");
    }
    if (record.codec != "h264" || record.container != "mp4") {
        return Status::failure(
            "config_unsupported", "first implementation requires h264/mp4");
    }
    if (record.vbvBufferBytes <= record.vbvThresholdBytes ||
        record.vbvThresholdBytes <= 0) {
        return Status::failure("config_invalid", "invalid VENC VBV sizing");
    }
    if (rtsp.enabled != 0 &&
        (rtsp.codec != "h264" || rtsp.width <= 0 || rtsp.height <= 0 ||
         rtsp.width > camera.mediaCaptureWidth || rtsp.height > camera.mediaCaptureHeight ||
         rtsp.width % 2 != 0 || rtsp.height % 2 != 0 ||
         !inRange(rtsp.frameRate, 1, camera.frameRate) || rtsp.bitRate <= 0 ||
         rtsp.vbvBufferBytes <= rtsp.vbvThresholdBytes ||
         rtsp.vbvThresholdBytes <= 0 || rtsp.maxFrameBytes <= 0 ||
         !inRange(rtsp.port, 1, 65535) ||
         !isSafeToken(rtsp.networkInterface) || !isSafeToken(rtsp.streamName))) {
        return Status::failure("config_invalid", "invalid H.264 RTSP profile");
    }
    if (snapshot.width <= 0 || snapshot.height <= 0 ||
        snapshot.width % 2 != 0 || snapshot.height % 2 != 0 ||
        snapshot.width > camera.mediaCaptureWidth ||
        snapshot.height > camera.mediaCaptureHeight) {
        return Status::failure("config_invalid", "invalid Snapshot dimensions");
    }
    if (playback.maxSourceWidth <= 0 || playback.maxSourceHeight <= 0 ||
        playback.maxFrameRate <= 0 || playback.maxBitRate <= 0 ||
        (playback.allowH264 != 0 && playback.allowH264 != 1) ||
        (playback.allowH265 != 0 && playback.allowH265 != 1) ||
        (playback.allowMjpeg != 0 && playback.allowMjpeg != 1) ||
        (playback.supportBFrames != 0 && playback.supportBFrames != 1) ||
        (playback.forceFramePackage != 0 && playback.forceFramePackage != 1) ||
        playback.veFrequencyMHz < 0 ||
        (playback.allowH264 + playback.allowH265 + playback.allowMjpeg) == 0) {
        return Status::failure("config_invalid", "invalid Playback support matrix");
    }
    const std::int64_t displayRight =
        static_cast<std::int64_t>(display.x) + display.width;
    const std::int64_t displayBottom =
        static_cast<std::int64_t>(display.y) + display.height;
    const std::int64_t playbackRight =
        static_cast<std::int64_t>(playback.displayX) + playback.displayWidth;
    const std::int64_t playbackBottom =
        static_cast<std::int64_t>(playback.displayY) + playback.displayHeight;
    if (playback.displayX < display.x || playback.displayY < display.y ||
        playback.displayWidth <= 0 || playback.displayHeight <= 0 ||
        playbackRight > displayRight || playbackBottom > displayBottom) {
        return Status::failure(
            "config_invalid", "Playback display rectangle must be inside display canvas");
    }
    constexpr int kLargestSampleFrameBytes = 1920 * 1080 * 2;
    if ((uvc.enabled != 0 && uvc.enabled != 1) ||
        (uvc.bulkMode != 0 && uvc.bulkMode != 1)) {
        return Status::failure("config_invalid",
                               "uvc.enabled/bulk_mode must be 0 or 1");
    }
    if (uvc.enabled != 0 &&
        (!isVideoDevicePath(uvc.videoDevice) ||
         containsParentTraversal(uvc.videoDevice))) {
        return Status::failure(
            "config_invalid",
            "uvc.video_device must match an absolute /dev/videoN node");
    }
    if (uvc.ispDevice < 0 || uvc.vippDevice < 0 || uvc.viChannel < 0 ||
        uvc.vencChannel < 0 || !inRange(uvc.viBufferCount, 2, 16) ||
        !inRange(uvc.gadgetBufferCount, 2, 16) ||
        !inRange(uvc.frameQueueDepth, 1, 8) ||
        !inRange(uvc.frameTimeoutMs, 20, 5000) || uvc.mjpegBitRate <= 0 ||
        uvc.h264BitRate <= 0 ||
        uvc.vencVbvBufferBytes <= uvc.vencVbvThresholdBytes ||
        uvc.vencVbvThresholdBytes <= 0 ||
        uvc.maxFrameBytes < kLargestSampleFrameBytes) {
        return Status::failure("config_invalid", "invalid UVC output profile");
    }
    return Status::success();
}

}  // namespace mpp
}  // namespace camera
