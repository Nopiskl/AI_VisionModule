#pragma once

#include <string>

namespace camera {
namespace gui {

// Read-only verification against the current BSP. No Qt or MPP dependency.
bool queryFramebufferLayer(const char* framebuffer, int displayOutput,
                           int candidateLayer, int* actualLayer,
                           std::string* detail);

}  // namespace gui
}  // namespace camera
