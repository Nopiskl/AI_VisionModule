find_package(OpenCV REQUIRED COMPONENTS
    core
    dnn
    imgcodecs
    imgproc
    ml
    videoio
)

add_executable(camera_yolov8_opencv
    src/main.cpp
    src/NeuralNetworkRuntime.cpp
    src/YoloV8Processor.cpp
)

target_include_directories(camera_yolov8_opencv PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
)
target_link_libraries(camera_yolov8_opencv PRIVATE
    camera::common
    sunxi_mpp::npu
    ${OpenCV_LIBS}
    Threads::Threads
    ${CMAKE_DL_LIBS}
    rt
)
set_target_properties(camera_yolov8_opencv PROPERTIES
    OUTPUT_NAME "camera-yolo-worker"
)
target_compile_features(camera_yolov8_opencv PRIVATE cxx_std_14)
target_compile_options(camera_yolov8_opencv PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS camera_yolov8_opencv
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/v851s-camera")
