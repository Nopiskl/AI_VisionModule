add_executable(camera_mpp_rtsp
    sample_rtsp.c
    rtsp_server.cpp
)

target_include_directories(camera_mpp_rtsp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(camera_mpp_rtsp PRIVATE sunxi_mpp::streaming)
set_target_properties(camera_mpp_rtsp PROPERTIES
    OUTPUT_NAME "camera-mpp-rtsp"
)

install(TARGETS camera_mpp_rtsp
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/v851s-camera")
install(FILES sample_rtsp.conf
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/v851s-camera")

