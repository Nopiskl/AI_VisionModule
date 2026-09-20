#pragma once

#include <QString>

namespace camera {
namespace gui {

struct DisplayLayerContract {
    bool valid{false};
    int videoLayer{-1};
    int configuredUiLayer{-1};
    int framebufferUiLayer{-1};
    QString detail;
};

// Validates the linuxfb-to-DISP2 mapping before the MPP service is allowed to
// create its video layer. Qt continues to own and configure the framebuffer;
// this adapter deliberately performs no MPP calls and never touches the video
// layer.
class Disp2LayerAdapter {
public:
    static DisplayLayerContract validate(const QString& serviceConfig,
                                         const QString& framebuffer,
                                         const QString& backendLock,
                                         int displayOutput = 0);
};

}  // namespace gui
}  // namespace camera
