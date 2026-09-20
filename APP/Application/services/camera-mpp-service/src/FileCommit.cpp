#include "camera/mpp/FileCommit.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0)
#endif

namespace camera {
namespace mpp {
namespace {

std::string parentPath(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "." :
           slash == 0 ? "/" : path.substr(0, slash);
}

void syncParentDirectory(const std::string& path) {
    const int descriptor =
        open(parentPath(path).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor >= 0) {
        fsync(descriptor);
        close(descriptor);
    }
}

Status failure(const std::string& prefix, int error) {
    return Status::failure(prefix + "_file_commit_failed", std::strerror(error));
}

}  // namespace

Status commitFileNoReplace(const std::string& temporaryPath,
                           const std::string& finalPath,
                           const std::string& errorPrefix) {
#ifdef SYS_renameat2
    if (syscall(SYS_renameat2, AT_FDCWD, temporaryPath.c_str(), AT_FDCWD,
                finalPath.c_str(), RENAME_NOREPLACE) == 0) {
        syncParentDirectory(finalPath);
        return Status::success();
    }
    const int renameAtError = errno;
    if (renameAtError != ENOSYS && renameAtError != EINVAL &&
        renameAtError != EOPNOTSUPP) {
        return failure(errorPrefix, renameAtError);
    }
#endif

    // Old kernels/filesystems may not implement RENAME_NOREPLACE. Reserve the
    // name atomically, then replace only the inode created by this function.
    const int reservation = open(finalPath.c_str(),
                                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (reservation < 0) {
        return failure(errorPrefix, errno);
    }
    if (close(reservation) != 0) {
        const int closeError = errno;
        unlink(finalPath.c_str());
        return failure(errorPrefix, closeError);
    }
    if (rename(temporaryPath.c_str(), finalPath.c_str()) != 0) {
        const int renameError = errno;
        unlink(finalPath.c_str());
        return failure(errorPrefix, renameError);
    }
    syncParentDirectory(finalPath);
    return Status::success();
}

}  // namespace mpp
}  // namespace camera
