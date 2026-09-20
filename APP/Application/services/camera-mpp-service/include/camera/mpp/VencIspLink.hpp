#pragma once

#include <mpi_venc.h>
#include <mpi_vi.h>

namespace camera {
namespace mpp {

// ISP2VE eventData is borrowed only until the callback returns.
ERRORTYPE updateVencIspParameters(VI_DEV vipp, VENC_CHN channel, void* eventData);

}  // namespace mpp
}  // namespace camera
