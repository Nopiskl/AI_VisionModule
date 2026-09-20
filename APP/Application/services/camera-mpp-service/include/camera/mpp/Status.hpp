#pragma once

#include <string>
#include <utility>

namespace camera {
namespace mpp {

struct Status {
    bool ok{true};
    std::string code;
    std::string detail;

    static Status success() { return {}; }

    static Status failure(std::string errorCode, std::string errorDetail) {
        Status status;
        status.ok = false;
        status.code = std::move(errorCode);
        status.detail = std::move(errorDetail);
        return status;
    }
};

}  // namespace mpp
}  // namespace camera
