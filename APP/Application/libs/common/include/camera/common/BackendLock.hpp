#pragma once

#include <string>

namespace camera {
namespace common {

struct BackendLockResult {
    bool ok{false};
    std::string code;
    std::string detail;

    static BackendLockResult success();
    static BackendLockResult failure(std::string code, std::string detail);
};

// A process-scoped, non-blocking flock used by every camera backend. The
// descriptor remains open for the lifetime of the lease, so process death also
// releases the kernel lock.
class BackendLock {
public:
    BackendLock() = default;
    ~BackendLock();

    BackendLock(const BackendLock&) = delete;
    BackendLock& operator=(const BackendLock&) = delete;

    BackendLockResult acquire(const std::string& path,
                              const std::string& owner);
    void release() noexcept;
    bool held() const { return descriptor_ >= 0; }

private:
    int descriptor_{-1};
};

}  // namespace common
}  // namespace camera
