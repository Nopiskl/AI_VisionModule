include_guard(GLOBAL)

include(CMakeParseArguments)
find_package(Threads REQUIRED)

function(_sunxi_mpp_collect_manifest manifest root)
    if(NOT EXISTS "${manifest}")
        message(FATAL_ERROR "sunxi-mpp manifest does not exist: ${manifest}")
    endif()

    file(STRINGS "${manifest}" manifest_lines)
    foreach(line IN LISTS manifest_lines)
        string(STRIP "${line}" line)
        if(line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif()

        # The manifest is deliberately space-delimited. CMake's regular
        # expression dialect does not treat \t as a tab escape here; using it
        # inside the character class would also exclude the literal letter
        # "t" from paths such as lib/libmedia_utils.a.
        if(NOT line MATCHES "^([a-z0-9_]+) +([^ ]+)$")
            message(FATAL_ERROR "Malformed sunxi-mpp manifest line: ${line}")
        endif()

        string(TOUPPER "${CMAKE_MATCH_1}" group)
        set(relative_path "${CMAKE_MATCH_2}")
        set(absolute_path "${root}/${relative_path}")
        if(NOT EXISTS "${absolute_path}")
            message(FATAL_ERROR
                "Required sunxi-mpp artifact is missing: ${absolute_path}\n"
                "Run Application/tools/fetch_sunxi_mpp.sh first, or set "
                "SUNXI_MPP_ROOT to a prepared bundle.")
        endif()
        list(APPEND "SUNXI_MPP_${group}_FILES" "${absolute_path}")
    endforeach()

    foreach(group
            CORE VI VO VENC MUXER RTSP DEMUX VDEC
            RUNTIME_LINK RUNTIME_DATA NPU RECORD_SAMPLE)
        set("SUNXI_MPP_${group}_FILES"
            "${SUNXI_MPP_${group}_FILES}" PARENT_SCOPE)
    endforeach()
endfunction()

function(_sunxi_mpp_add_profile profile)
    set(options)
    set(one_value_args)
    set(multi_value_args GROUPS)
    cmake_parse_arguments(PROFILE
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(archives)
    foreach(group IN LISTS PROFILE_GROUPS)
        string(TOUPPER "${group}" group_upper)
        list(APPEND archives ${SUNXI_MPP_${group_upper}_FILES})
    endforeach()

    # libaw_mpp's configured component registry references this SDK closure.
    list(APPEND archives ${SUNXI_MPP_CORE_FILES} ${SUNXI_MPP_VI_FILES}
        ${SUNXI_MPP_VO_FILES} ${SUNXI_MPP_VENC_FILES} ${SUNXI_MPP_MUXER_FILES}
        ${SUNXI_MPP_DEMUX_FILES} ${SUNXI_MPP_VDEC_FILES})
    list(REMOVE_DUPLICATES archives)

    add_library("sunxi_mpp_${profile}" INTERFACE)
    add_library("sunxi_mpp::${profile}" ALIAS "sunxi_mpp_${profile}")
    target_link_libraries("sunxi_mpp_${profile}" INTERFACE
        sunxi_mpp::headers
        "-Wl,--start-group"
        ${archives}
        "-Wl,--end-group"
        ${SUNXI_MPP_RUNTIME_LINK_FILES}
        Threads::Threads
        ${CMAKE_DL_LIBS}
        rt
        m
        stdc++
    )
endfunction()

function(sunxi_mpp_import)
    set(options)
    set(one_value_args ROOT MANIFEST)
    set(multi_value_args)
    cmake_parse_arguments(MPP
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT MPP_ROOT)
        message(FATAL_ERROR "sunxi_mpp_import requires ROOT")
    endif()
    if(NOT MPP_MANIFEST)
        message(FATAL_ERROR "sunxi_mpp_import requires MANIFEST")
    endif()
    if(NOT IS_DIRECTORY "${MPP_ROOT}/include" OR
       NOT IS_DIRECTORY "${MPP_ROOT}/lib")
        message(FATAL_ERROR
            "Standalone sunxi-mpp bundle not found at ${MPP_ROOT}.\n"
            "Run Application/tools/fetch_sunxi_mpp.sh first.")
    endif()

    if(NOT EXISTS "${MPP_ROOT}/SDK_SOURCE.json" OR
       NOT EXISTS "${MPP_ROOT}/SDK_FEATURES.cmake")
        message(FATAL_ERROR
            "Import the current Tina SDK with tools/import_sunxi_mpp.py; "
            "the legacy GitHub binary bundle is not ABI compatible.")
    endif()
    file(SHA256 "${MPP_MANIFEST}" expected_manifest_sha)
    file(SHA256 "${MPP_ROOT}/BUNDLE_MANIFEST" imported_manifest_sha)
    if(NOT expected_manifest_sha STREQUAL imported_manifest_sha)
        message(FATAL_ERROR "Bundle manifest changed; reimport into a new bundle directory")
    endif()
    include("${MPP_ROOT}/SDK_FEATURES.cmake")
    _sunxi_mpp_collect_manifest("${MPP_MANIFEST}" "${MPP_ROOT}")

    # Public include roots only. Recursively adding every directory makes
    # json/features.h shadow musl's <features.h> and breaks C++ compilation.
    set(sunxi_mpp_include_dirs
        "${MPP_ROOT}/include"
        "${MPP_ROOT}/include/rtsp"
        "${MPP_ROOT}/include/viplite"
        "${MPP_ROOT}/include/mpp/middleware/include"
        "${MPP_ROOT}/include/mpp/middleware/include/media"
        "${MPP_ROOT}/include/mpp/middleware/include/utils"
        "${MPP_ROOT}/include/mpp/middleware/media/include"
        "${MPP_ROOT}/include/mpp/middleware/media/include/component"
        "${MPP_ROOT}/include/mpp/middleware/media/include/utils"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libcedarx/libcore/common/iniparser"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libisp/isp_tuning"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libisp/isp_dev"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libisp"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libisp/include"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libisp/include/V4l2Camera"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libisp/include/device"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libcedarc/include"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/libcedarx/libcore/base/include"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/include_stream"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/include_muxer"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/include_demux"
        "${MPP_ROOT}/include/mpp/middleware/media/LIBRARY/include_FsWriter"
        "${MPP_ROOT}/include/mpp/middleware/sample/configfileparser"
        "${MPP_ROOT}/include/mpp/system/public/include"
        "${MPP_ROOT}/include/mpp/system/public/libion/include")

    add_library(sunxi_mpp_headers INTERFACE)
    add_library(sunxi_mpp::headers ALIAS sunxi_mpp_headers)
    target_include_directories(sunxi_mpp_headers SYSTEM INTERFACE
        ${sunxi_mpp_include_dirs})

    target_compile_definitions(sunxi_mpp_headers INTERFACE AWCHIP=0x1886)
    foreach(codec VDEC_H264 VDEC_H265 VDEC_JPEG VENC_H264 VENC_H265 VENC_JPEG)
        target_compile_definitions(sunxi_mpp_headers INTERFACE
            "SUNXI_MPP_${codec}=${SUNXI_MPP_${codec}}")
    endforeach()

    _sunxi_mpp_add_profile(preview GROUPS CORE VI VO)
    _sunxi_mpp_add_profile(streaming GROUPS CORE VI VENC RTSP)
    _sunxi_mpp_add_profile(recording GROUPS CORE VI VENC MUXER)
    _sunxi_mpp_add_profile(preview_recording GROUPS CORE VI VO VENC MUXER)
    _sunxi_mpp_add_profile(service
        GROUPS CORE VI VO VENC MUXER RTSP DEMUX VDEC)

    add_library(sunxi_mpp_npu INTERFACE)
    add_library(sunxi_mpp::npu ALIAS sunxi_mpp_npu)
    target_link_libraries(sunxi_mpp_npu INTERFACE
        sunxi_mpp::headers
        ${SUNXI_MPP_NPU_FILES}
        ${CMAKE_DL_LIBS}
    )

    set(SUNXI_MPP_IMPORTED_ROOT "${MPP_ROOT}" CACHE INTERNAL
        "Imported standalone sunxi-mpp root")
    set(SUNXI_MPP_IMPORTED_RUNTIME_FILES
        "${SUNXI_MPP_RUNTIME_LINK_FILES};${SUNXI_MPP_RUNTIME_DATA_FILES}"
        CACHE INTERNAL "Imported standalone sunxi-mpp runtime libraries")
    set(SUNXI_MPP_IMPORTED_NPU_FILES "${SUNXI_MPP_NPU_FILES}"
        CACHE INTERNAL "Imported standalone VIPLite runtime libraries")
endfunction()

function(sunxi_mpp_install_runtime)
    if(NOT SUNXI_MPP_IMPORTED_ROOT)
        message(FATAL_ERROR "sunxi_mpp_import must run before installation")
    endif()

    if(APPLICATION_BUILD_MPP OR APPLICATION_BUILD_MPP_EXAMPLES)
        # Preserve the upstream symlink chain and SONAME-compatible aliases.
        install(DIRECTORY "${SUNXI_MPP_IMPORTED_ROOT}/lib/"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            USE_SOURCE_PERMISSIONS
            FILES_MATCHING
            PATTERN "libglog.so*"
            PATTERN "libunwind.so*"
            PATTERN "libasound.so*"
        )
    endif()
    if(APPLICATION_BUILD_YOLOV8)
        install(FILES ${SUNXI_MPP_IMPORTED_NPU_FILES}
            DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
endfunction()
