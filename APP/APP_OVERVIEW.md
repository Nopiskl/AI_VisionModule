# APP 介绍

## 用途

这是基于 Qt 5.12.9、Allwinner MPP、OpenCV 4.1.0 和 VIPLite 的相机应用。源码提供相机预览、拍照、录像、相册回放、RTSP、UVC 和 YOLO 工作进程入口。具体模式需要固件提供相应驱动、设备节点和模型。

## 三个程序如何配合

| 程序 | 作用 |
|---|---|
| camera-gui | Qt 界面、模式切换、后端进程监督 |
| camera-mpp-service | 独占管理 MPP 采集、VO 显示、编码、录像和回放资源 |
| camera-yolo-worker | OpenCV 取帧及 VIPLite 模型推理 |

GUI 通过 IPC 控制 MPP 服务。YOLO 与 MPP 互斥使用摄像头/ISP；进入 YOLO 前释放 MPP 和 Qt UI，YOLO 退出后恢复 GUI。相机与回放时，Qt 管理 UI layer，MPP 管理视频 layer。

## 源码入口

| 路径 | 内容 |
|---|---|
| Application/apps/camera-gui | Qt GUI、后端监督、显示与退出处理 |
| Application/services/camera-mpp-service | 相机、回放、录像、RTSP、UVC |
| Application/src/component/YOLOv8 | 推理工作进程 |
| Application/libs/common | 公共协议与工具 |
| Application/configs/mpp-service.conf | 采集、显示、媒体通路和存储配置 |
| Application/third_party/sunxi-mpp-sdk | 与当前 SDK 匹配的 MPP 头文件和库 |
| sdk-dev | 树外编译所需工具链、sysroot、Qt/OpenCV/ISP 开发文件 |
| runtime | 启动脚本及板级 Qt 环境 |

原组件设计和接口说明保留在 Application/docs，日常使用从本目录的指南开始。

## 当前默认参数

- Qt 逻辑界面：800×480；framebuffer/VO 物理窗口：480×800。
- 预览：VIPP0，480×800，NV21。
- 媒体采集：按需使用 VIPP4，1280×720；固件需提供相应节点。
- 显示缩放：`display.scale_mode=stretch` 铺满；改为 `contain` 保持比例并允许黑边。
- 媒体目录：`/mnt/extsd/v851s-camera`；按实际挂载点修改。
- 屏幕旋转和触摸设备：`runtime/board-env.sh`，当前为 rotation=90、event3 和触摸 rotate=270。

VIPP 采集尺寸、VO 物理尺寸、Qt 逻辑尺寸分别配置。VO 铺满窗口不会自动旋转采集/编码的像素，也不会消除传感器端裁剪。

## 已合入源码的修复

退出与异常恢复、VO 帧归还和解绑顺序、Qt/VO 坐标映射、GUI 兼容处理及板级默认配置均在当前源码中。Qt 库本身的触摸兼容修改位于 SDK overlay 的 Qt patches。

正常使用 GUI“退出程序”或向监督进程发送 SIGTERM，使后端释放资源并清理 UI layer。SIGKILL 无法被进程捕获。

