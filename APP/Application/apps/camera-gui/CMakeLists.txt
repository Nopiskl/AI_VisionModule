find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(camera_gui
    backend/BackendSupervisor.hpp
    backend/BackendSupervisor.cpp
    backend/IpcClient.hpp
    backend/IpcClient.cpp
    platform/FramebufferSession.hpp
    platform/FramebufferSession.cpp
    platform/DisplayGeometry.hpp
    platform/Disp2LayerAdapter.hpp
    platform/Disp2LayerAdapter.cpp
    platform/SunxiFramebufferQuery.cpp
    platform/SunxiFramebufferQuery.hpp
    platform/LinuxFramebufferProbe.hpp
    platform/LinuxFramebufferProbe.cpp
    ui/MainWindow.hpp
    ui/MainWindow.cpp
    ui/design/dashboard.h
    ui/design/dashboard.cpp
    ui/design/pages/album_page.cpp
    ui/design/pages/camera_page.cpp
    ui/design/pages/home_page.cpp
    ui/design/pages/navigation.cpp
    ui/design/pages/opencv_page.cpp
    ui/design/pages/uvc_page.cpp
    ui/design/ui/button_styles.h
    ui/design/ui/button_styles.cpp
    ui/design/ui/canvas_button.h
    ui/design/ui/canvas_button.cpp
    ui/design/ui/device_illustrations.h
    ui/design/ui/device_illustrations.cpp
    ui/design/ui/icons.h
    ui/design/ui/icons.cpp
    ui/design/ui/metrics.h
    ui/design/ui/painting.h
    ui/design/ui/painting.cpp
    ui/design/ui/theme.h
    ui/design/ui/theme.cpp
    main.cpp
)

set_target_properties(camera_gui PROPERTIES
    OUTPUT_NAME camera-gui
    AUTOMOC ON
)

set(SUNXI_DISPLAY_UAPI_ROOT "${SUNXI_MPP_ROOT}/include/kernel-uapi"
    CACHE PATH "Current BSP display UAPI headers (no MPP library dependency)")
if(NOT EXISTS "${SUNXI_DISPLAY_UAPI_ROOT}/video/sunxi_display2.h")
    message(FATAL_ERROR "Import the current SDK display UAPI headers first")
endif()
target_include_directories(camera_gui SYSTEM PRIVATE "${SUNXI_DISPLAY_UAPI_ROOT}")
target_include_directories(camera_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(camera_gui PRIVATE
    camera::common
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)
target_compile_features(camera_gui PRIVATE cxx_std_14)
target_compile_options(camera_gui PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS camera_gui
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
