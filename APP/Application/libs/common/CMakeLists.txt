add_library(camera_common STATIC
    src/BackendLock.cpp
    src/IpcMessage.cpp
)

add_library(camera::common ALIAS camera_common)

target_include_directories(camera_common
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_compile_features(camera_common PUBLIC cxx_std_14)
target_compile_options(camera_common PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS camera_common
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
