cmake_minimum_required(VERSION 3.16)

project(v851s_camera_application VERSION 0.1.0 LANGUAGES C CXX)

option(APPLICATION_BUILD_GUI "Build the Qt Widgets camera GUI" OFF)
option(APPLICATION_BUILD_MPP "Build the product camera-mpp-service" OFF)
option(APPLICATION_BUILD_MPP_EXAMPLES
       "Build transitional standalone Allwinner MPP sample targets" OFF)
option(APPLICATION_BUILD_YOLOV8 "Build the OpenCV/VIPLite YOLOv8 worker" OFF)
option(APPLICATION_INSTALL_VENDOR_RUNTIME
       "Install vendor shared libraries beside Application artifacts" OFF)

set(SUNXI_MPP_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sunxi-mpp-sdk"
    CACHE PATH
    "Standalone sunxi-mpp bundle prepared by tools/fetch_sunxi_mpp.sh")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(GNUInstallDirs)

set(APPLICATION_SYSCONFDIR "/etc/v851s-camera" CACHE PATH
    "Target directory for V851S camera configuration files")

if(APPLICATION_BUILD_GUI OR APPLICATION_BUILD_MPP OR APPLICATION_BUILD_YOLOV8)
    add_subdirectory(libs/common)
endif()

if(APPLICATION_BUILD_MPP OR APPLICATION_BUILD_MPP_EXAMPLES OR
   APPLICATION_BUILD_YOLOV8)
    include(cmake/SunxiMpp.cmake)
    sunxi_mpp_import(
        ROOT "${SUNXI_MPP_ROOT}"
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sunxi-mpp.manifest"
    )
endif()

if(APPLICATION_BUILD_MPP)
    add_subdirectory(services/camera-mpp-service)
endif()

if(APPLICATION_BUILD_MPP_EXAMPLES)
    add_subdirectory(src/component/MPP)
endif()

if(APPLICATION_BUILD_GUI)
    add_subdirectory(apps/camera-gui)
endif()

if(APPLICATION_BUILD_YOLOV8)
    add_subdirectory(src/component/YOLOv8)
endif()

if(APPLICATION_INSTALL_VENDOR_RUNTIME)
    if(NOT APPLICATION_BUILD_MPP AND NOT APPLICATION_BUILD_MPP_EXAMPLES AND
       NOT APPLICATION_BUILD_YOLOV8)
        message(FATAL_ERROR
            "APPLICATION_INSTALL_VENDOR_RUNTIME requires an enabled worker")
    endif()
    sunxi_mpp_install_runtime()
endif()
