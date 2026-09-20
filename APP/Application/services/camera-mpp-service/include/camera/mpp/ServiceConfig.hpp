#pragma once

#include "camera/mpp/Status.hpp"

#include <string>

namespace camera {
namespace mpp {

struct DisplayConfig {
    int voDevice{0};
    int videoLayer{0};
    int videoChannel{0};
    int uiOutsideLayer{4};
    int x{0};
    int y{0};
    int width{480};
    int height{800};
    std::string interfaceType{"lcd"};
    std::string interfaceSync{"ntsc"};
    std::string scaleMode{"stretch"};
};

struct CameraConfig {
    int ispDevice{0};
    int vippDevice{0};
    int previewChannel{0};
    int recordChannel{1};
    int captureWidth{480};
    int captureHeight{800};
    int mediaVippDevice{4};
    int mediaCaptureWidth{1280};
    int mediaCaptureHeight{720};
    int frameRate{20};
    int viBufferCount{5};
    std::string pixelFormat{"nv21"};
};

struct RecordConfig {
    int vencChannel{0};
    int muxGroup{0};
    int muxChannel{0};
    int width{1280};
    int height{720};
    int frameRate{20};
    int bitRate{4000000};
    int vbvBufferBytes{2 * 1024 * 1024};
    int vbvThresholdBytes{1024 * 1024};
    int addRepairInfo{1};
    int repairBackupIntervalUs{100000};
    int storagePollMs{1000};
    int storageReserveBytes{64 * 1024 * 1024};
    std::string codec{"h264"};
    std::string container{"mp4"};
};

struct RtspConfig {
    int enabled{1};
    int viChannel{2};
    int vencChannel{2};
    int width{1280};
    int height{720};
    int frameRate{15};
    int bitRate{1500000};
    int vbvBufferBytes{1024 * 1024};
    int vbvThresholdBytes{512 * 1024};
    int maxFrameBytes{3 * 1280 * 720};
    int port{8554};
    std::string codec{"h264"};
    std::string networkInterface{"eth0"};
    std::string streamName{"ch0"};
};

struct SnapshotConfig {
    int vencChannel{1};
    int width{1280};
    int height{720};
    int quality{90};
    int timeoutMs{2000};
};

struct PlaybackConfig {
    int demuxChannel{0};
    int vdecChannel{0};
    int clockChannel{0};
    int maxSourceWidth{1280};
    int maxSourceHeight{720};
    int maxFrameRate{30};
    int maxBitRate{8000000};
    int allowH264{1};
    int allowH265{0};
    int allowMjpeg{1};
    int supportBFrames{0};
    int forceFramePackage{0};
    int veFrequencyMHz{0};
    int displayX{52};
    int displayY{144};
    int displayWidth{388};
    int displayHeight{644};
};

struct UvcConfig {
    int enabled{1};
    std::string videoDevice{"/dev/video2"};
    int bulkMode{0};
    int ispDevice{0};
    int vippDevice{0};
    int viChannel{0};
    int vencChannel{0};
    int viBufferCount{5};
    int gadgetBufferCount{5};
    int frameQueueDepth{3};
    int frameTimeoutMs{200};
    int mjpegBitRate{4194304};
    int h264BitRate{4194304};
    int vencVbvBufferBytes{4194304};
    int vencVbvThresholdBytes{2097152};
    int maxFrameBytes{4194304};
};

struct ServiceConfig {
    std::string socketPath{"/run/v851s-camera/mpp.sock"};
    std::string backendLockPath{"/run/v851s-camera/backend.lock"};
    std::string storageRoot{"/mnt/extsd/v851s-camera"};
    std::string mediaRoot{"/mnt/extsd"};
    int requireMediaRootMount{1};
    int sysAlignWidth{32};
    DisplayConfig display;
    CameraConfig camera;
    RecordConfig record;
    RtspConfig rtsp;
    SnapshotConfig snapshot;
    PlaybackConfig playback;
    UvcConfig uvc;

    static Status load(const std::string& path, ServiceConfig* output);
    Status validate() const;
};

}  // namespace mpp
}  // namespace camera
