#include "camera/mpp/VencIspLink.hpp"

#include <cstring>
#include <mpi_isp.h>

namespace camera {
namespace mpp {

ERRORTYPE updateVencIspParameters(VI_DEV vipp, VENC_CHN channel, void* eventData) {
    if (eventData == nullptr) {
        return ERR_VENC_NULL_PTR;
    }
    auto* output = static_cast<VencIsp2VeParam*>(eventData);
    ISP_DEV isp = 0;
    ERRORTYPE result = AW_MPI_VI_GetIspDev(vipp, &isp);
    if (result != SUCCESS) {
        return result;
    }
    struct isp_ae_stats_s ae {};
    struct enc_VencIsp2VeParam input {};
    input.AeStatsInfo = &ae;
    result = AW_MPI_ISP_GetIsp2VeParam(isp, &input);
    if (result != SUCCESS) {
        return result;
    }

    // These are the paired ISP/encoder ABI types in the current SDK.
    static_assert(sizeof(output->mIspAeStatus) == sizeof(ae), "ISP/VE AE ABI");
    static_assert(sizeof(output->mSharpParam.mDynamicParam) ==
                  sizeof(input.mDynamicSharpCfg), "ISP/VE dynamic sharp ABI");
    static_assert(sizeof(output->mSharpParam.mStaticParam) ==
                  sizeof(input.mStaticSharpCfg), "ISP/VE static sharp ABI");
    std::memcpy(&output->mIspAeStatus, &ae, sizeof(ae));
    if (input.encpp_en) {
        std::memcpy(&output->mSharpParam.mDynamicParam, &input.mDynamicSharpCfg,
                    sizeof(input.mDynamicSharpCfg));
        std::memcpy(&output->mSharpParam.mStaticParam, &input.mStaticSharpCfg,
                    sizeof(input.mStaticSharpCfg));
    }

    VENC_CHN_ATTR_S attributes {};
    result = AW_MPI_VENC_GetChnAttr(channel, &attributes);
    if (result != SUCCESS) {
        return result;
    }
    const BOOL enabled = input.encpp_en ? TRUE : FALSE;
    if (attributes.EncppAttr.mbEncppEnable != enabled) {
        attributes.EncppAttr.mbEncppEnable = enabled;
        return AW_MPI_VENC_SetChnAttr(channel, &attributes);
    }
    return SUCCESS;
}

}  // namespace mpp
}  // namespace camera
