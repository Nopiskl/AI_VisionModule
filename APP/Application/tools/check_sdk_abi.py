#!/usr/bin/env python3
"""Compile ABI probes against the imported bundle and this SDK; never run ARM code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", required=True, type=Path)
    parser.add_argument("--bundle", type=Path,
                        default=Path(__file__).resolve().parents[1]/"third_party/sunxi-mpp-sdk")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    sdk, bundle, out = args.sdk_root.resolve(), args.bundle.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    meta = json.loads((bundle/"SDK_SOURCE.json").read_text())
    for name, expected in meta["sdk_files"].items():
        assert hashlib.sha256((sdk/name).read_bytes()).hexdigest() == expected, "SDK changed: "+name
    for name, record in meta["libraries"].items():
        assert hashlib.sha256((sdk/record["source"]).read_bytes()).hexdigest() == record["sha256"], name
        assert hashlib.sha256((bundle/name).read_bytes()).hexdigest() == record["sha256"], name
    toolchain = sdk/"prebuilt/gcc/linux-x86/arm/toolchain-sunxi-musl/toolchain"
    prefix = toolchain/"bin/arm-openwrt-linux-muslgnueabi-"
    env = dict(os.environ, STAGING_DIR=str(sdk/"out"/meta["board"]/"staging_dir/target"))
    arch = ["-DAWCHIP=0x1886", "--sysroot="+str(toolchain), "-mcpu=cortex-a7", "-mfpu=neon-vfpv4", "-mfloat-abi=hard"]
    def compile_constants(name, code, includes=()):
        source, assembly = out/(name+".c"), out/(name+".s")
        source.write_text(code)
        subprocess.run([str(prefix)+"gcc"]+arch+["-S","-O0"]+
                       ["-I"+str(x) for x in includes]+[str(source),"-o",str(assembly)],
                       env=env,check=True)
        values = {k:int(v)&0xffffffff for k,v in
                  re.findall(r"(abi_\w+):\n\s*\.word\s+(-?\d+)", assembly.read_text())}
        assert values, "No compiler constants: "+name
        return values
    constants = """
const unsigned int abi_sensor_config_size = sizeof(struct sensor_config);
const unsigned int abi_sensor_exp_gain_size = sizeof(struct sensor_exp_gain);
const unsigned int abi_gain_min_offset = offsetof(struct sensor_config, gain_min);
const unsigned int abi_mbus_code_offset = offsetof(struct sensor_config, mbus_code);
const unsigned int abi_cfg_ioctl = VIDIOC_VIN_SENSOR_CFG_REQ;
const unsigned int abi_exp_gain_ioctl = VIDIOC_VIN_SENSOR_EXP_GAIN;
"""
    header = bundle/"include/mpp/middleware/media/LIBRARY/libisp/include/V4l2Camera/sunxi_camera_v2.h"
    result = {"bundle_sensor":compile_constants("bundle_sensor",
        '#include <stddef.h>\n#include <sys/ioctl.h>\n#include "'+str(header)+'"\n'+constants)}
    kernel = (sdk/"lichee/linux-4.9/include/media/sunxi_camera_v2.h").read_text()
    parts = ["#include <stddef.h>\n#include <sys/ioctl.h>\n#include <linux/videodev2.h>\n"]
    # Verbatim independent definitions; retain preprocessor guards inside structs.
    for name in ("sensor_config", "sensor_exp_gain"):
        parts.append(re.search(r"struct "+name+r"\s*\{.*?\n\};",kernel,re.S).group(0))
    for name in ("VIDIOC_VIN_SENSOR_CFG_REQ","VIDIOC_VIN_SENSOR_EXP_GAIN"):
        parts.append(re.search(r"#define "+name+r"\s*\\\n[^\n]+",kernel).group(0))
    result["kernel_sensor"] = compile_constants("kernel_sensor","\n".join(parts)+"\n"+constants)
    assert result["bundle_sensor"] == result["kernel_sensor"], "sensor ioctl/struct ABI mismatch"
    public = """
#include <mm_comm_vi.h>
const unsigned int abi_vi_attr_size = sizeof(VI_ATTR_S);
const unsigned int abi_frame_size = sizeof(VIDEO_FRAME_S);
const unsigned int abi_frame_info_size = sizeof(VIDEO_FRAME_INFO_S);
"""
    for label,root in [
        ("bundle_public",bundle/"include/mpp/middleware"),
        ("sdk_public",sdk/"external/eyesee-mpp/middleware/sun8iw21")]:
        result[label] = compile_constants(label,public,[
            root/"include/media",root/"include/utils",
            root/"media/LIBRARY/libisp/include/V4l2Camera"])
    assert result["bundle_public"] == result["sdk_public"], "public MPP ABI mismatch"
    app = Path(__file__).resolve().parents[1]
    uvc = out/"uvc_kernel.cpp"
    uvc.write_text('#include <cstddef>\n#include "'+
        str(app/"services/camera-mpp-service/include/camera/mpp/UvcGadget.hpp")+'"\n#include "'+
        str(sdk/"lichee/linux-4.9/drivers/usb/gadget/function/uvc.h")+'''"
using namespace camera::mpp::uvc;
static_assert(sizeof(RequestData)==sizeof(uvc_request_data), "UVC request size");
static_assert(sizeof(Event)==sizeof(uvc_event), "UVC event size");
static_assert(offsetof(RequestData,data)==offsetof(uvc_request_data,data), "UVC data offset");
static_assert(kSendResponse==UVCIOC_SEND_RESPONSE, "UVC response ioctl");
static_assert(kEventConnect==UVC_EVENT_CONNECT && kEventDisconnect==UVC_EVENT_DISCONNECT &&
              kEventStreamOn==UVC_EVENT_STREAMON && kEventStreamOff==UVC_EVENT_STREAMOFF &&
              kEventSetup==UVC_EVENT_SETUP && kEventData==UVC_EVENT_DATA, "UVC events");
''')
    subprocess.run([str(prefix)+"g++"]+arch+["-std=c++14","-c",str(uvc),"-o",str(out/"uvc_kernel.o")],
                   env=env,check=True)
    result["uvc_kernel_abi"]="compile-time assertions passed"
    (out/"results.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result,indent=2))
    print("PASS: bundle/source identity, ARM sensor/public ABI and kernel UVC ABI.")
if __name__ == "__main__":
    main()
