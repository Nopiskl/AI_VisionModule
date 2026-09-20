#include "camera/mpp/MppRuntime.hpp"

#include <cstring>

#include <mpi_sys.h>

namespace camera {
namespace mpp {

MppRuntime::~MppRuntime() {
    stop();
}

Status MppRuntime::start(int alignmentWidth) {
    if (active_) {
        return Status::success();
    }
    MPP_SYS_CONF_S configuration;
    std::memset(&configuration, 0, sizeof(configuration));
    configuration.nAlignWidth = alignmentWidth;
    ERRORTYPE result = AW_MPI_SYS_SetConf(&configuration);
    if (result != SUCCESS) {
        return Status::failure("mpp_sys_config_failed", std::to_string(result));
    }
    result = AW_MPI_SYS_Init();
    if (result != SUCCESS) {
        return Status::failure("mpp_sys_init_failed", std::to_string(result));
    }
    active_ = true;
    return Status::success();
}

Status MppRuntime::stop() noexcept {
    if (!active_) {
        return Status::success();
    }
    const ERRORTYPE result = AW_MPI_SYS_Exit();
    active_ = false;
    if (result != SUCCESS) {
        return Status::failure("mpp_sys_exit_failed", std::to_string(result));
    }
    return Status::success();
}

}  // namespace mpp
}  // namespace camera
