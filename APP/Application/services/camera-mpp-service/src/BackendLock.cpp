#include "camera/mpp/BackendLock.hpp"

namespace camera {
namespace mpp {

BackendLock::~BackendLock() {
    release();
}

Status BackendLock::acquire(const std::string& path, const std::string& owner) {
    const camera::common::BackendLockResult result = lock_.acquire(path, owner);
    return result.ok ? Status::success()
                     : Status::failure(result.code, result.detail);
}

void BackendLock::release() noexcept {
    lock_.release();
}

}  // namespace mpp
}  // namespace camera
