add_executable(camera_mpp_service
    src/BackendLock.cpp
    src/CameraPipeline.cpp
    src/DisplayOutput.cpp
    src/EventMailbox.cpp
    src/FileCommit.cpp
    src/IpcServer.cpp
    src/MppRuntime.cpp
    src/MppService.cpp
    src/PlaybackPipeline.cpp
    src/RecordOutput.cpp
    src/RtspOutput.cpp
    src/ServiceConfig.cpp
    src/SnapshotOutput.cpp
    src/UvcPipeline.cpp
    src/VencIspLink.cpp
    src/main.cpp
)

set_target_properties(camera_mpp_service PROPERTIES
    OUTPUT_NAME camera-mpp-service
)

target_include_directories(camera_mpp_service PRIVATE include)
target_link_libraries(camera_mpp_service PRIVATE
    camera::common
    sunxi_mpp::service
)
target_link_options(camera_mpp_service PRIVATE
    "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/camera-mpp-service.map")
target_compile_features(camera_mpp_service PRIVATE cxx_std_14)
target_compile_options(camera_mpp_service PRIVATE
    -Wall -Wextra -Wpedantic
)

install(TARGETS camera_mpp_service
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_SOURCE_DIR}/configs/mpp-service.conf"
    DESTINATION "${APPLICATION_SYSCONFDIR}")
