#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
app_dir="$(CDPATH= cd -- "${script_dir}/.." && pwd)"
: "${SUNXI_TINA_SDK_ROOT:?Set SUNXI_TINA_SDK_ROOT to the current built SDK}"
sdk="$(CDPATH= cd -- "${SUNXI_TINA_SDK_ROOT}" && pwd)"
board="${SUNXI_TINA_BOARD:-v853-100ask}"
toolchain="${sdk}/prebuilt/gcc/linux-x86/arm/toolchain-sunxi-musl/toolchain"
cmake_bin="${CMAKE_BIN:-cmake}"
build_dir="${APPLICATION_BUILD_DIR:-${app_dir}/../build/current-sdk}"
bundle="${SUNXI_MPP_ROOT:-${app_dir}/third_party/sunxi-mpp-sdk}"
export STAGING_DIR="${sdk}/out/${board}/staging_dir/target"

# Imports are explicit so an existing bundle is never silently overwritten.
python3 "${script_dir}/check_sdk_abi.py" --sdk-root "${sdk}" \
    --bundle "${bundle}" --output "${build_dir}/abi"
"${cmake_bin}" -S "${app_dir}" -B "${build_dir}" \
    -DCMAKE_TOOLCHAIN_FILE="${app_dir}/cmake/toolchains/v851s-musl.cmake" \
    -DV851S_C_COMPILER="${toolchain}/bin/arm-openwrt-linux-muslgnueabi-gcc" \
    -DV851S_CXX_COMPILER="${toolchain}/bin/arm-openwrt-linux-muslgnueabi-g++" \
    -DV851S_SYSROOT="${toolchain}" \
    -DSUNXI_MPP_ROOT="${bundle}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DAPPLICATION_BUILD_MPP=ON \
    -DAPPLICATION_BUILD_GUI=OFF \
    -DAPPLICATION_BUILD_YOLOV8=OFF \
    -DAPPLICATION_BUILD_MPP_EXAMPLES=OFF "$@"
"${cmake_bin}" --build "${build_dir}" --parallel "${BUILD_JOBS:-4}"
