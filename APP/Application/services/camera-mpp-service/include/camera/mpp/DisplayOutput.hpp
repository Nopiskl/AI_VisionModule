#pragma once

#include "camera/mpp/ServiceConfig.hpp"
#include "camera/mpp/Status.hpp"

#include <mpi_sys.h>
#include <mpi_vo.h>

namespace camera {
namespace mpp {

class DisplayOutput {
public:
    explicit DisplayOutput(DisplayConfig config);
    ~DisplayOutput();

    DisplayOutput(const DisplayOutput&) = delete;
    DisplayOutput& operator=(const DisplayOutput&) = delete;

    Status create(PIXEL_FORMAT_E pixelFormat, MPPCallbackInfo* callback = nullptr);
    Status start();
    Status pause();
    Status resume();
    Status stop() noexcept;
    Status destroy() noexcept;

    bool created() const { return channelCreated_; }
    bool started() const { return started_; }
    VO_LAYER layer() const { return config_.videoLayer; }
    VO_CHN channel() const { return config_.videoChannel; }
    MPP_CHN_S mppChannel() const;

    static DisplayConfig fitWithin(const DisplayConfig& canvas,
                                   int sourceWidth, int sourceHeight);

private:
    DisplayConfig config_;
    bool deviceEnabled_{false};
    bool outsideRegistered_{false};
    bool layerEnabled_{false};
    bool channelCreated_{false};
    bool started_{false};
};

}  // namespace mpp
}  // namespace camera
