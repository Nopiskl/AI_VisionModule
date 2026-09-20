#pragma once

#include <QString>

namespace camera {
namespace gui {

struct FramebufferInfo {
    bool available{false};
    int width{0};
    int height{0};
    int bitsPerPixel{0};
    int alphaBits{0};
    QString device;
    QString detail;
};

class LinuxFramebufferProbe {
public:
    static FramebufferInfo inspect(const QString& device);
};

}  // namespace gui
}  // namespace camera
