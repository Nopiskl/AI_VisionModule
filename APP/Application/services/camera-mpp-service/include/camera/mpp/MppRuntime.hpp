#pragma once

#include "camera/mpp/Status.hpp"

namespace camera {
namespace mpp {

class MppRuntime {
public:
    MppRuntime() = default;
    ~MppRuntime();

    MppRuntime(const MppRuntime&) = delete;
    MppRuntime& operator=(const MppRuntime&) = delete;

    Status start(int alignmentWidth);
    Status stop() noexcept;
    bool active() const { return active_; }

private:
    bool active_{false};
};

}  // namespace mpp
}  // namespace camera
