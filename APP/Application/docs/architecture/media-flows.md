# 跨组件媒体与控制流

- 文档角色：描述控制消息和媒体数据跨组件的稳定流向
- 参数与 channel owner：组件配置和 Pipeline 文档
- 平台 API owner：[`../platform/`](../platform/)

本页只画跨组件拓扑，不规定具体 MPI 函数、channel ID、分辨率、码率或容器支持。

## 控制面

```text
User action
    |
    v
camera-gui / Backend supervision
    |
    +-- start/stop process ------------------------+
    |                                              |
    `-- versioned request --> active backend       |
                              |                    |
                              `-- response/event --+
```

GUI 根据 response/event 更新控件，不解析日志，也不因命令已发送而假定媒体操作成功。
MPP 命令和事件见 [`../interfaces/ipc-v1.md`](../interfaces/ipc-v1.md)；监督与切换规则见
[`runtime-model.md`](runtime-model.md)。

当前 GUI 的 Home 只是导航页。非录像态返回 Home 不发送 `leave_mode`，所以状态摘要必须
继续显示仍活动的 Camera/RTSP、Playback 或 UVC；Recording 期间 Camera 页面被锁定，
不提供返回 Home 或其它模式入口。选择另一功能时才通过下述既有命令路径完成互斥切换。
页面结构和按键映射见
[`camera-gui/ui-integration.md`](../components/camera-gui/ui-integration.md)。

## Camera Preview 与 UI 合成

```text
Sensor -> ISP/VI -> MPP video output -----> DISP2 video layer --+
                                                               +--> LCD
camera-gui -> Qt backing store -----------> DISP2 UI layer ----+
```

媒体帧不经过 GUI。Qt 与 MPP 各写自己的显示层，由 DISP2 合成；两者不得共同写同一
framebuffer。显示 owner 见 [`resource-ownership.md`](resource-ownership.md)，平台契约
见 [`../platform/display/`](../platform/display/)。

## Camera 按需输出

Camera 会话保留一个采集 owner，并在其内部增加按需输出：

```text
Camera capture
├── Preview ----------------------> MPP video layer
├── Snapshot operation -----------> image file
├── Record output ----------------> encoded container file
└── Optional stream output -------> RTSP client
```

这些输出不创建新的产品进程或第二个 sensor owner。是否允许某些输出并发、采用哪些
参数以及怎样启动/停止，只由
[`camera-mpp-service`](../components/camera-mpp-service/) 的配置、Pipeline、已知限制和
主维护者认可的目标板事实定义。

当前产品明确禁止 Snapshot 与 Record 并发。后端确认 Recording 后，GUI 锁定 Camera
专用录像态，只保留停止录像操作并显示后端计时；RTSP 若在录像前已经启用可继续运行，
但录像态 UI 不提供 RTSP 或其它媒体操作入口。

## Playback

```text
Local media file -> MPP demux/decode -> MPP video output -> DISP2 video layer -> LCD
camera-gui ------- control/status -------------------------------------------> UI
```

解码帧不传给 Qt；GUI 不使用 Qt Multimedia 或软件解码器代替产品 Playback。具体
Pipeline 见
[`camera-mpp-service/pipelines.md`](../components/camera-mpp-service/pipelines.md)，候选
门禁和未确认范围见
[`configuration.md`](../components/camera-mpp-service/configuration.md) 与
[`known-limitations.md`](../components/camera-mpp-service/known-limitations.md)。

相册把照片和视频分为两条显示路径：

```text
hello.storage_root -> GUI 有界目录扫描 -> JPEG -> Qt 相册预览框
                                      `-> MP4  -> load_media
                                                   |
                                                   v
                                      MPP DEMUX/VDEC/VO 子矩形
                                                   |
media_loaded(actual display rect) -> Qt 清该矩形 alpha -> DISP2 合成
```

照片选择会先 `leave_mode`，保证不存在被不透明 Qt 页面遮住但仍持有资源的媒体 Pipeline。
视频实际矩形由后端按源宽高比计算并通过 IPC 返回；Qt 不复制比例算法。手动上一项/
下一项重新走对应选择路径，不形成自动播放队列。

## UVC Out

```text
PC Host control
    PROBE/COMMIT --------------------------+
    STREAMON/STREAMOFF --------------------+--> UVC gadget event loop
                                                |
Sensor -> ISP/VI -> MJPEG/H.264 VENC -----------+--> bounded copy queue
              `-> NV21 -> CPU YUYV conversion -+          |
                                                           v
                                     V4L2 VIDEO_OUTPUT mmap buffers -> USB Host

camera-gui -> Qt framebuffer/UI layer -> DISP2 -> LCD（仅状态和控制，无 MPP VO）
```

进入 UVC mode 只打开并订阅预配置 gadget 节点；采集与编码由有效 COMMIT 后的 Host
STREAMON（bulk 模式为 COMMIT）按需创建。VENC stream 或 VI frame 在复制/转换后立即
Release，Host 慢时丢弃应用队列中的旧帧。STREAMOFF、DISCONNECT、模式切换和 shutdown
都先停止/join 数据线程，再释放 VI/VENC 与 gadget mmap buffer。服务不创建 configfs
gadget，也不在本阶段处理 ADB composite 兼容。

## YOLO 模式

```text
Home OpenCV action -> shutdown MPP -> QApplication/linuxfb 析构
                            |
                            v
Sensor/V4L2 -> camera-yolo-worker -> detection overlay -> framebuffer -> LCD
                            |
                            `-- exit/release lock --> 重建 Qt UI 和 MPP
```

YOLO 与 MPP 不共享实时 camera frame，也不并发打开采集设备。YOLO 会话没有 Qt UI
进程，worker 是唯一 framebuffer writer；它退出后才恢复 Qt，因此当前不需要增加推理
结果 IPC。若未来要求 YOLO 上方保留 Qt 控件，必须重新评审显示与推理结果传递架构。

## 变更规则

新增跨进程 frame 传输、音频链路、新后端或新的顶层媒体模式，会改变本页和资源 owner，
需要先更新 requirement，并判断是否需要新增或修订 ADR。单个组件内部替换类、线程或
MPI 封装而不改变上述边界时，只更新组件文档。
