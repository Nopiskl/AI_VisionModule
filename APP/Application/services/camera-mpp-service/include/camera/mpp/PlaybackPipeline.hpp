#pragma once

#include "camera/mpp/DisplayOutput.hpp"
#include "camera/mpp/EventMailbox.hpp"
#include "camera/mpp/ServiceConfig.hpp"
#include "camera/mpp/Status.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <mpi_clock.h>
#include <mpi_demux.h>
#include <mpi_sys.h>
#include <mpi_vdec.h>

namespace camera {
namespace mpp {

enum class PlaybackState {
    Idle,
    Ready,
    Playing,
    Paused,
    Ended,
};

class PlaybackPipeline {
public:
    PlaybackPipeline(const ServiceConfig& config, EventMailbox& mailbox);
    ~PlaybackPipeline();

    PlaybackPipeline(const PlaybackPipeline&) = delete;
    PlaybackPipeline& operator=(const PlaybackPipeline&) = delete;

    Status load(const std::string& path);
    Status play();
    Status pause();
    Status resume();
    Status stop();
    Status close() noexcept;
    void markEnded();

    PlaybackState state() const { return state_; }
    const std::string& sourcePath() const { return sourcePath_; }
    std::uint32_t durationMs() const { return mediaInfo_.mDuration; }
    int positionMs() const;
    int sourceWidth() const;
    int sourceHeight() const;
    int sourceFrameRateMilliFps() const;
    int sourceAverageBitRate() const;
    int sourceMaximumBitRate() const;
    int codec() const;
    std::string codecName() const;
    int displayX() const { return fittedDisplay_.x; }
    int displayY() const { return fittedDisplay_.y; }
    int displayWidth() const { return fittedDisplay_.width; }
    int displayHeight() const { return fittedDisplay_.height; }

private:
    static ERRORTYPE callback(void* cookie, MPP_CHN_S* channel,
                              MPP_EVENT_TYPE event, void* eventData);
    Status stopStreams() noexcept;

    ServiceConfig config_;
    EventMailbox& mailbox_;
    PlaybackState state_{PlaybackState::Idle};
    std::string sourcePath_;
    int sourceDescriptor_{-1};
    DEMUX_MEDIA_INFO_S mediaInfo_{};
    DisplayConfig fittedDisplay_{};
    std::unique_ptr<DisplayOutput> display_;
    bool demuxCreated_{false};
    bool vdecCreated_{false};
    bool clockCreated_{false};
    bool demuxVdecBound_{false};
    bool vdecVoBound_{false};
    bool clockDemuxBound_{false};
    bool clockVoBound_{false};
    bool clockStarted_{false};
    bool vdecStarted_{false};
    bool demuxStarted_{false};
};

const char* toString(PlaybackState state);

}  // namespace mpp
}  // namespace camera
