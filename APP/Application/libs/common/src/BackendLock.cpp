#include "camera/common/BackendLock.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <utility>

namespace camera {
namespace common {
namespace {

bool writeAll(int descriptor, const char* data, std::size_t length) {
    while (length > 0) {
        const ssize_t count = write(descriptor, data, length);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            errno = EIO;
            return false;
        }
        data += count;
        length -= static_cast<std::size_t>(count);
    }
    return true;
}

}  // namespace

BackendLockResult BackendLockResult::success() {
    return {true, {}, {}};
}

BackendLockResult BackendLockResult::failure(std::string code,
                                             std::string detail) {
    return {false, std::move(code), std::move(detail)};
}

BackendLock::~BackendLock() {
    release();
}

BackendLockResult BackendLock::acquire(const std::string& path,
                                       const std::string& owner) {
    if (held()) {
        return BackendLockResult::failure("lock_already_held", path);
    }
    descriptor_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (descriptor_ < 0) {
        return BackendLockResult::failure("lock_open_failed",
                                          std::strerror(errno));
    }
    if (flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
        const std::string detail = std::strerror(errno);
        close(descriptor_);
        descriptor_ = -1;
        return BackendLockResult::failure("backend_busy", detail);
    }

    const std::string record = owner + " pid=" + std::to_string(getpid()) + "\n";
    if (ftruncate(descriptor_, 0) != 0 || lseek(descriptor_, 0, SEEK_SET) < 0 ||
        !writeAll(descriptor_, record.data(), record.size()) ||
        fsync(descriptor_) != 0) {
        const std::string detail = std::strerror(errno);
        release();
        return BackendLockResult::failure("lock_write_failed", detail);
    }
    return BackendLockResult::success();
}

void BackendLock::release() noexcept {
    if (descriptor_ < 0) {
        return;
    }
    flock(descriptor_, LOCK_UN);
    close(descriptor_);
    descriptor_ = -1;
}

}  // namespace common
}  // namespace camera
