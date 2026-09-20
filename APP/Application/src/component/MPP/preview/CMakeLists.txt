add_executable(camera_mpp_preview
    sample_virvi2vo.c
)

target_include_directories(camera_mpp_preview PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(camera_mpp_preview PRIVATE sunxi_mpp::preview)
set_target_properties(camera_mpp_preview PROPERTIES
    OUTPUT_NAME "camera-mpp-preview"
    LINKER_LANGUAGE CXX
)

install(TARGETS camera_mpp_preview
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/v851s-camera")
install(FILES sample_virvi2vo.conf
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/v851s-camera")

