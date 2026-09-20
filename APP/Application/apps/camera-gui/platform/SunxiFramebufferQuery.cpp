#include "SunxiFramebufferQuery.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>
// The vendor display header also declares kernel callbacks using these aliases.
using u32 = std::uint32_t;
using s32 = std::int32_t;
#include <video/sunxi_display2.h>

namespace camera {
namespace gui {

bool queryFramebufferLayer(const char* framebuffer, int displayOutput,
                           int candidateLayer, int* actualLayer,
                           std::string* detail) {
    *actualLayer = -1;
    if ((displayOutput != 0 && displayOutput != 1) ||
        candidateLayer < 0 || candidateLayer >= 16) {
        *detail = "invalid DISP2 output/layer";
        return false;
    }
    const int fb = open(framebuffer, O_RDONLY | O_CLOEXEC);
    if (fb < 0) {
        *detail = std::string(framebuffer) + ": " + std::strerror(errno);
        return false;
    }
    struct fb_fix_screeninfo fixed {};
    const int fbResult = ioctl(fb, FBIOGET_FSCREENINFO, &fixed);
    const int fbError = errno;
    close(fb);
    if (fbResult != 0 || fixed.smem_start == 0 || fixed.smem_len == 0) {
        *detail = fbResult != 0 ? std::strerror(fbError)
                               : "framebuffer physical memory is unavailable";
        return false;
    }

    const int display = open("/dev/disp", O_RDONLY | O_CLOEXEC);
    if (display < 0) {
        *detail = std::string("/dev/disp: ") + std::strerror(errno);
        return false;
    }
    struct disp_layer_config layer {};
    layer.channel = static_cast<unsigned int>(candidateLayer / 4);
    layer.layer_id = static_cast<unsigned int>(candidateLayer % 4);
    unsigned long arguments[4] = {
        static_cast<unsigned long>(displayOutput),
        reinterpret_cast<unsigned long>(&layer), 1, 0};
    const int queryResult = ioctl(display, DISP_LAYER_GET_CONFIG, arguments);
    const int queryError = errno;
    close(display);
    if (queryResult != 0) {
        *detail = std::string("DISP_LAYER_GET_CONFIG: ") + std::strerror(queryError);
        return false;
    }
    const std::uint64_t begin = fixed.smem_start;
    const std::uint64_t end = begin + fixed.smem_len;
    if (!layer.enable || layer.info.mode != LAYER_MODE_BUFFER ||
        layer.channel != static_cast<unsigned int>(candidateLayer / 4) ||
        layer.layer_id != static_cast<unsigned int>(candidateLayer % 4) ||
        layer.info.fb.addr[0] < begin || layer.info.fb.addr[0] >= end) {
        *detail = "configured UI layer does not reference the active framebuffer";
        return false;
    }
    *actualLayer = candidateLayer;
    detail->clear();
    return true;
}

}  // namespace gui
}  // namespace camera
