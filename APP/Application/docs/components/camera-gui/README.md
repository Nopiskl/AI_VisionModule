# camera-gui

`camera-gui` 是 Qt 5.12 Widgets 产品入口和会话监督器。外层进程不创建
`QApplication`，按会话启动同一程序的 Qt UI 子进程或 `camera-yolo-worker`。它只链接
QtCore、QtGui/QtWidgets 和 `camera_common`；不链接 MPP、OpenCV、VIPLite、Qt
Multimedia 或 QtNetwork。

组件源码和构建入口位于 [`../../../apps/camera-gui/`](../../../apps/camera-gui/)。

## 职责

- 外层 session loop 监督 Qt UI→YOLO→Qt 的进程级 framebuffer 交接；
- Qt UI 子进程中的 `BackendSupervisor` 使用 `QProcess` 监督
  `camera-mpp-service`，并要求服务随父进程死亡；
- `IpcClient` 使用 POSIX Unix socket + `QSocketNotifier` 接入 Qt 事件循环；
- `Dashboard` 提供用户设计的 800×480 Home/Camera/Album/UVC 视图，`MainWindow` 负责
  IPC 接线、媒体目录模型和 response/event 状态翻译；
- `LinuxFramebufferProbe` 查询 framebuffer 几何；`Disp2LayerAdapter` 查询实际 DISP2
  handle，并在启动 MPP 前强制 UI/video layer 和双方 backend lock 契约一致。

首页提供 Camera、UVC、OpenCV 三个入口，以及右上角“退出程序”。Camera 页面提供拍照、录像和按 capability
启用的 RTSP，RTSP 实际 URL 显示在顶部；检测按钮本阶段可见但禁用。Album 页面扫描
服务声明的产品媒体目录，照片由 Qt 显示，MP4 由 MPP Playback 在局部 VO 矩形显示。
UVC 页面展示等待 Host、连接、COMMIT、传输和错误状态，格式与 STREAMON/OFF 由 USB
Host 控制。完整页面/状态接线见 [`ui-integration.md`](ui-integration.md)。

后端确认进入 Recording 后，Camera 页面切换为专用录像态：隐藏导航、拍照、RTSP、
检测和普通状态控件，只保留白色圆环包围红色圆角方块的 iOS 风格停止按钮；顶部使用
IPC `record_elapsed_ms` 显示已录时长。停止成功、存储异常中断或后端断开后恢复普通
Camera 控件。

YOLO 文件完整时首页 OpenCV 卡片启用：点击后先向 MPP 发送
`shutdown`，等待服务退出，再用 Qt 特殊退出码通知外层监督器启动 worker。MPP 命令和
事件字段只以
[`../../interfaces/ipc-v1.md`](../../interfaces/ipc-v1.md) 为准。

## 启动

```text
camera-gui [--service PATH] [--service-config PATH] [--socket PATH]
           [--backend-lock PATH] [--framebuffer PATH] [--display-output 0|1]
           [--yolo PATH] [--yolo-model PATH] [--yolo-classes PATH]
           [--yolo-memory BYTES] [--yolo-camera INDEX] [--windowed]
```

未设置 `QT_QPA_PLATFORM` 时，非 windowed UI 子进程默认使用
`linuxfb:fb=/dev/fb0`；`--windowed` 仅用于开发诊断并跳过 DISP2 门禁。默认 YOLO
产物、模型和类别路径分别是：

```text
/usr/libexec/v851s-camera/camera-yolo-worker
/etc/v851s-camera/yolov8.nb
/etc/v851s-camera/yolov8-classes.txt
```

仓库不提交或安装模型；模型与类别文件不可读时 YOLO 按钮保持禁用并显示原因。

## 显示边界

Camera 中除 overlay 外的内容区透明；Album 视频中只有后端 `media_loaded/get_status`
返回的实际 VO 矩形透明，照片和空状态保持不透明。导航、按钮和状态面板由 Qt UI layer
绘制，MPP video layer 位于更低层。GUI 不操作 MPP video layer，也不自行计算 VO 比例。

UVC 模式不创建 MPP VO layer，Qt UI 子进程继续独占 framebuffer/UI layer，只显示 Host
与传输状态。因此 UVC 不需要额外的视频 layer，也不会让 gadget 数据写入 LCD。

`Disp2LayerAdapter` 从 service 配置读取 `display.video_layer`、
`display.ui_outside_layer` 和 `ipc.backend_lock_path`，使用
`FBIOGET_FSCREENINFO` 和 `DISP_LAYER_GET_CONFIG` 只读核对 linuxfb 内存与候选 layer。handle 重叠、实际 UI handle 与
outside 配置不符，或 MPP/YOLO lock 路径不同都会阻止 MPP 启动。alpha、z-order、
rotation 和透明 backing store 仍需板端确认。详细契约见
[`../../platform/display/linuxfb-disp2.md`](../../platform/display/linuxfb-disp2.md)。

YOLO 中不做分层合成：Qt UI 子进程完全退出，worker 独占 framebuffer。worker 退出后
外层监督器自动恢复 Qt/MPP；运行期间向外层 `camera-gui` 进程发送一次 `SIGUSR1` 可请求
返回 Qt，第二次用于强制结束未响应的 worker。

## 状态与限制

当前实现/验证成熟度统一查看 [`../../STATUS.md`](../../STATUS.md)。尤其注意：

- 按钮点击不代表成功，busy/recording/RTSP/UVC streaming/playing/error 均以后端确认为准；
- GUI 不接收视频帧，也不在 UI 线程编码或写媒体文件；
- 相册最多展示 `storage_root` 第一层最新 512 个 JPEG/MP4；不扫描整个 media root；
- Seek、音频和 Qt Multimedia 不在当前产品范围；
- YOLO 运行期间没有 Qt 控件；返回操作由 worker 自身退出、外部终止或监督器
  `SIGUSR1` 完成。

## 应用退出

主页“退出程序”向 MPP 发送 shutdown 并等待后端退出；正在拍照时等任务结束重试。
10 秒未完成时交给 BackendSupervisor 的 TERM/等待回收路径。窗口关闭、SIGTERM/SIGINT
复用同一路径。录像页面先停止录像、返回主页，再退出。
FramebufferSession 的生命周期包住 QApplication：Qt 停止绘制/析构后才清空 framebuffer
全部虚拟页的颜色与 alpha，并通过本 BSP 的 FBIOBLANK 关闭自身 UI 层。外层监督器可在
UI 子进程异常终止、后端锁已释放时做同样清理。边界见平台显示契约。
