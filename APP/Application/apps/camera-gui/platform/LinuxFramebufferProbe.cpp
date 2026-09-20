#include "platform/LinuxFramebufferProbe.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace camera {
namespace gui {

FramebufferInfo LinuxFramebufferProbe::inspect(const QString& device) {
    FramebufferInfo result;
    result.device = device;
    const QByteArray encoded = device.toLocal8Bit();
    const int descriptor = open(encoded.constData(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        result.detail = QString::fromLocal8Bit(std::strerror(errno));
        return result;
    }

    fb_var_screeninfo variable {};
    fb_fix_screeninfo fixed {};
    if (ioctl(descriptor, FBIOGET_VSCREENINFO, &variable) != 0 ||
        ioctl(descriptor, FBIOGET_FSCREENINFO, &fixed) != 0) {
        result.detail = QString::fromLocal8Bit(std::strerror(errno));
        close(descriptor);
        return result;
    }
    close(descriptor);

    result.available = true;
    result.width = static_cast<int>(variable.xres);
    result.height = static_cast<int>(variable.yres);
    result.bitsPerPixel = static_cast<int>(variable.bits_per_pixel);
    result.alphaBits = static_cast<int>(variable.transp.length);
    result.detail = QStringLiteral("%1 %2x%3 %4bpp alpha=%5 line=%6")
                        .arg(QString::fromLatin1(
                                 fixed.id, static_cast<int>(sizeof(fixed.id)))
                                 .trimmed())
                        .arg(result.width)
                        .arg(result.height)
                        .arg(result.bitsPerPixel)
                        .arg(result.alphaBits)
                        .arg(fixed.line_length);
    return result;
}

}  // namespace gui
}  // namespace camera
