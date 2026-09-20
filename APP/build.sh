#!/usr/bin/env bash
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
DEV="$ROOT/sdk-dev"
TC="$DEV/toolchain"
BUILD="${APPLICATION_BUILD_DIR:-$ROOT/build}"
INSTALL="${APPLICATION_INSTALL_DIR:-$ROOT/install}"
CMAKE="${CMAKE_BIN:-$DEV/host-tools/cmake/bin/cmake}"
export STAGING_DIR="$DEV/target"
unset CMAKE_PREFIX_PATH CMAKE_TOOLCHAIN_FILE Qt5_DIR OpenCV_DIR
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH LD_LIBRARY_PATH
"$CMAKE" -S "$ROOT/Application" -B "$BUILD" \
 -DCMAKE_TOOLCHAIN_FILE="$ROOT/Application/cmake/toolchains/v851s-musl.cmake" \
 -DV851S_C_COMPILER="$TC/bin/arm-openwrt-linux-muslgnueabi-gcc" \
 -DV851S_CXX_COMPILER="$TC/bin/arm-openwrt-linux-muslgnueabi-g++" \
 -DV851S_SYSROOT="$TC" \
 -DV851S_DEPENDENCY_ROOTS="$DEV/qt/usr;$DEV/opencv/usr;$DEV/isp-adapter/usr;$DEV/target/usr" \
 -DCMAKE_PREFIX_PATH="$DEV/qt/usr;$DEV/opencv/usr;$DEV/isp-adapter/usr;$DEV/target/usr" \
 -DQt5_DIR="$DEV/qt/usr/lib/cmake/Qt5" \
 -DOpenCV_DIR="$DEV/opencv/usr/lib/cmake/opencv4" \
 -DSUNXI_MPP_ROOT="$ROOT/Application/third_party/sunxi-mpp-sdk" \
 -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$DEV/isp-adapter/usr/lib -Wl,-rpath-link,$DEV/target/usr/lib -Wl,-rpath-link,$DEV/opencv/usr/lib -Wl,-rpath-link,$ROOT/Application/third_party/sunxi-mpp-sdk/lib" \
 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr \
 -DCMAKE_SKIP_RPATH=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
 -DAPPLICATION_BUILD_MPP=ON -DAPPLICATION_BUILD_GUI=ON \
 -DAPPLICATION_BUILD_YOLOV8=ON -DAPPLICATION_BUILD_MPP_EXAMPLES=OFF \
 -DAPPLICATION_INSTALL_VENDOR_RUNTIME=OFF "$@"
"$CMAKE" --build "$BUILD" --parallel "${BUILD_JOBS:-4}"
DESTDIR="$INSTALL" "$CMAKE" --install "$BUILD"
install -d "$INSTALL/usr/bin" "$INSTALL/etc/v851s-camera"
install -m 0755 "$ROOT/runtime/camera-start" "$INSTALL/usr/bin/camera-start"
install -m 0644 "$ROOT/runtime/board-env.sh" "$INSTALL/etc/v851s-camera/board-env.sh"
printf 'APP installed to %s\n' "$INSTALL"
