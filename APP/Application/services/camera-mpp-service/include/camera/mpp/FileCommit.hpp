#pragma once

#include "camera/mpp/Status.hpp"

#include <string>

namespace camera {
namespace mpp {

// Commit a fully written temporary file without replacing an existing target.
// The temporary and final paths must be on the same filesystem.
Status commitFileNoReplace(const std::string& temporaryPath,
                           const std::string& finalPath,
                           const std::string& errorPrefix);

}  // namespace mpp
}  // namespace camera
