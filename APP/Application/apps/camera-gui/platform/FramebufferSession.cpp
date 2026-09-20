#include "platform/FramebufferSession.hpp"
#include <QDebug>
#include <QFile>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace camera { namespace gui {
FramebufferSession::FramebufferSession(QString device, bool enabled)
    : device_(std::move(device)), enabled_(enabled) {
    if (!enabled_) return;
    const int fd = ::open(QFile::encodeName(device_).constData(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        qWarning("UI framebuffer open failed: %s", std::strerror(errno));
        return;
    }
    // The preceding session disabled only this framebuffer's layer.
    // fb_open alone does not re-enable it when the LCD is already running.
    if (::ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK) != 0)
        qWarning("UI framebuffer unblank failed: %s", std::strerror(errno));
    ::close(fd);
}

FramebufferSession::~FramebufferSession() {
    if (enabled_) release(device_);
}

bool FramebufferSession::release(const QString& device) {
    const int fd = ::open(QFile::encodeName(device).constData(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        qWarning("UI framebuffer release open failed: %s", std::strerror(errno));
        return false;
    }
    bool ok = true;
    fb_fix_screeninfo fixed {};
    fb_var_screeninfo variable {};
    if (::ioctl(fd, FBIOGET_FSCREENINFO, &fixed) != 0 ||
        ::ioctl(fd, FBIOGET_VSCREENINFO, &variable) != 0 ||
        !fixed.smem_len || fixed.smem_len > 64U * 1024U * 1024U ||
        variable.bits_per_pixel != 32 || variable.transp.length != 8) {
        qWarning("UI framebuffer clear refused: unsupported geometry/alpha");
        ok = false;
    } else {
        void* memory = ::mmap(nullptr, fixed.smem_len, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
        if (memory == MAP_FAILED) {
            qWarning("UI framebuffer clear mmap failed: %s", std::strerror(errno));
            ok = false;
        } else {
            // Clear every virtual page, including alpha. A future fb_open can
            // restore the cached layer config, so blanking alone is insufficient.
            std::memset(memory, 0, fixed.smem_len);
            ::munmap(memory, fixed.smem_len);
        }
    }
    // Current dev_fb.c implements POWERDOWN as config.enable=0 for this
    // framebuffer layer only. It does not blank lower VO layers or LCD.
    if (::ioctl(fd, FBIOBLANK, FB_BLANK_POWERDOWN) != 0) {
        qWarning("UI framebuffer layer disable failed: %s", std::strerror(errno));
        ok = false;
    }
    ::close(fd);
    qInfo("UI framebuffer released: cleared and layer disabled, ok=%d", ok);
    return ok;
}
}}
