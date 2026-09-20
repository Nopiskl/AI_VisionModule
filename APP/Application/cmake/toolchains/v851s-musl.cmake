set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(V851S_C_COMPILER "" CACHE FILEPATH
    "Path to the standalone arm-openwrt-linux C compiler")
set(V851S_CXX_COMPILER "" CACHE FILEPATH
    "Path to the standalone arm-openwrt-linux C++ compiler")
set(V851S_SYSROOT "" CACHE PATH
    "Path to the cross-toolchain sysroot; this does not need to be TinaSDK")

if(NOT IS_ABSOLUTE "${V851S_C_COMPILER}" OR
   NOT EXISTS "${V851S_C_COMPILER}")
    message(FATAL_ERROR "Set V851S_C_COMPILER to an existing absolute path")
endif()
if(NOT IS_ABSOLUTE "${V851S_CXX_COMPILER}" OR
   NOT EXISTS "${V851S_CXX_COMPILER}")
    message(FATAL_ERROR "Set V851S_CXX_COMPILER to an existing absolute path")
endif()
if(NOT IS_ABSOLUTE "${V851S_SYSROOT}" OR
   NOT IS_DIRECTORY "${V851S_SYSROOT}")
    message(FATAL_ERROR "Set V851S_SYSROOT to an existing absolute directory")
endif()

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    V851S_C_COMPILER V851S_CXX_COMPILER V851S_SYSROOT V851S_ARCH_FLAGS V851S_DEPENDENCY_ROOTS)

set(CMAKE_C_COMPILER "${V851S_C_COMPILER}")
set(CMAKE_CXX_COMPILER "${V851S_CXX_COMPILER}")
set(CMAKE_SYSROOT "${V851S_SYSROOT}")

# The imported archives advertise ARMv7, VFPv3 and Tag_ABI_VFP_args=VFP.
set(V851S_ARCH_FLAGS "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"
    CACHE STRING "Architecture flags compatible with the prebuilt MPP bundle")
set(CMAKE_C_FLAGS_INIT "${V851S_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${V851S_ARCH_FLAGS}")

set(V851S_DEPENDENCY_ROOTS "" CACHE STRING "Additional target dependency prefixes")
set(CMAKE_FIND_ROOT_PATH "${V851S_SYSROOT}" ${V851S_DEPENDENCY_ROOTS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

