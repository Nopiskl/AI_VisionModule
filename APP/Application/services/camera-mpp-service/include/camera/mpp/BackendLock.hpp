#pragma once

#include "camera/common/BackendLock.hpp"
#include "camera/mpp/Status.hpp"

#include <string>

namespace camera {
namespace mpp {

class BackendLock {
public:
    BackendLock() = default;
    ~BackendLock();

    BackendLock(const BackendLock&) = delete;
    BackendLock& operator=(const BackendLock&) = delete;

    Status acquire(const std::string& path, const std::string& owner);
    void release() noexcept;
    bool held() const { return lock_.held(); }

private:
    camera::common::BackendLock lock_;
};

}  // namespace mpp
}  // namespace camera
