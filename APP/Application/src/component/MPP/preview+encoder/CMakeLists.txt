set(recording_reference_dir
    "${SUNXI_MPP_IMPORTED_ROOT}/sample/sample_virvi2venc2muxer")

add_executable(camera_mpp_recording_reference
    "${recording_reference_dir}/sample_vi2venc2muxer.c"
    "${SUNXI_MPP_IMPORTED_ROOT}/sample/common/sample_common_venc.c"
)

target_include_directories(camera_mpp_recording_reference PRIVATE
    "${recording_reference_dir}"
)
target_compile_options(camera_mpp_recording_reference PRIVATE -include mpi_vi.h)
target_link_libraries(camera_mpp_recording_reference PRIVATE
    sunxi_mpp::recording)
set_target_properties(camera_mpp_recording_reference PROPERTIES
    OUTPUT_NAME "camera-mpp-recording-reference"
    LINKER_LANGUAGE CXX
)

install(TARGETS camera_mpp_recording_reference
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/v851s-camera")
install(FILES "${recording_reference_dir}/sample_vi2venc2muxer.conf"
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/v851s-camera")

