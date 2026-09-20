# 800×480 GUI 集成与后续开发约束

- 文档角色：新 Qt GUI 的页面行为、后端接线、相册显示和后续修改边界
- 产品行为 owner：[`../../requirements.md`](../../requirements.md)
- IPC owner：[`../../interfaces/ipc-v1.md`](../../interfaces/ipc-v1.md)
- 显示 owner：[`../../platform/display/linuxfb-disp2.md`](../../platform/display/linuxfb-disp2.md)
- 当前成熟度：[`../../STATUS.md`](../../STATUS.md)

本页指导后续 agent 修改 `camera-gui`，不把未执行的 Qt 目标构建或实机显示描述为已确认
能力。UI 视觉基线来自用户提供的
`TMP/NewUI/AI_Vision_Module_UI_v1.0.0_20260915.zip`；产品只迁入其中的可维护 Qt 绘制
源码，没有复制 Windows Qt runtime、DLL、构建目录、独立 `main()` 或部署脚本。

## 1. 当前固定决定

| 项目 | 当前决定 |
| --- | --- |
| 逻辑/目标分辨率 | 800×480 横屏；设计坐标为 1600×960，由 `Dashboard` 等比缩放 |
| Qt 基线 | 继续使用产品 Qt 5.12 Widgets + linuxfb；使用当前 SDK GCC 6.4.1 可完整支持的 C++14 |
| 首页 | Camera、UVC、OpenCV 三张卡片；首页是导航页，不是新的 backend mode |
| 退出程序 | 主页右上角按钮；请求 MPP shutdown，等待后端和 Qt 结束，再释放 UI framebuffer layer |
| 返回主页 | 非录像态只切换 Qt 页面，不发送 `leave_mode`，并提示仍活动的 backend；Recording 期间隐藏该入口 |
| Camera | 页面透明，MPP Preview 在低层；拍照、录像、RTSP 接既有 IPC |
| Camera 录像态 | 后端确认 Recording 后锁定 Camera 页面；只保留 iOS 风格停止录像按钮，顶部显示后端已录时长 |
| Camera 检测 | 可见但禁用；不能启动与 MPP 并发的 YOLO |
| OpenCV | 不进入 Qt 占位页；触发既有 Qt→YOLO→Qt 进程级互斥会话 |
| UVC | 首页只进入 UVC 功能页；页内按钮通过 IPC 启用/停止，画面和文字以后端状态为准 |
| 相册照片 | Qt 从服务声明的 `storage_root` 读取 JPEG，在预览框内等比例显示 |
| 相册视频 | `load_media` 后由 MPP Playback 在 VO 子矩形显示；Qt 只对后端报告的实际矩形清 alpha |
| 媒体切换 | 支持过滤、选择、手动上一项/下一项；不实现自动播放列表、队列、seek 或音频 |

任何要改变以上产品语义的任务必须先更新 requirement；涉及新的 owner、并发模式或跨进程
frame 传递时，还要评审现有 ADR。

## 2. 源码分层

```text
apps/camera-gui/
├── main.cpp                    外层 session loop；Qt/YOLO 子进程切换
├── backend/
│   ├── BackendSupervisor.*     camera-mpp-service 进程生命周期
│   └── IpcClient.*             IPC v1 Unix socket 客户端
├── platform/                   framebuffer 门禁、会话启用和退出清理
└── ui/
    ├── MainWindow.*            产品控制器、IPC 状态、媒体目录模型
    └── design/
        ├── dashboard.*         页面容器、纯 UI signal/state setter
        ├── pages/              Home/Camera/Album/UVC/OpenCV 绘制与控件
        └── ui/                 自绘按钮、图标、主题、缩放度量
```

`Dashboard` 不调用 IPC、MPP、V4L2 或进程管理；它只发出用户意图 signal，并接受
`MainWindow` 回写的状态。`MainWindow` 不直接操作 VO/DISP2；它只把 IPC 状态翻译成 UI。
不要把后端命令重新接进 `pages/*.cpp`，也不要把视觉坐标散落进 MPP service。

## 3. 页面与动作映射

| UI 动作 | Qt 页面变化 | 后端动作 | 确认来源 |
| --- | --- | --- | --- |
| 首页 Camera | 立即显示 Camera overlay | 非 Camera 时 `enter_mode camera` | response/event `mode/state` |
| Camera → 相册 | 显示相册并刷新媒体目录 | 无选中视频时进入 Playback Idle；选中视频时 `load_media` | response/event |
| 相册 → Camera | 立即显示 Camera overlay | `enter_mode camera`，完整停止 Playback | response/event |
| 首页 UVC | 只显示 UVC 页面 | 无 | 当前 backend 状态仍持续 |
| UVC“启用” | 页面保持 | `enter_mode uvc` | UVC mode/state/events |
| UVC“停止” | 页面保持 | `leave_mode` | `none/idle` |
| 首页 OpenCV | 等待交接 | `shutdown` MPP，Qt UI 以 42 退出 | service exit + 外层 session loop |
| 主页“退出程序” | 等待后退出 Qt UI | shutdown，异常时由 supervisor 回收 | service exit + FramebufferSession 释放 |
| 非录像功能页“返回主页” | 显示 Home | 无 | 首页摘要显示仍活动的 mode |

选择另一个真正的功能时，由 `camera-mpp-service::enterMode()` 先完整释放旧 Pipeline，再
创建新 Pipeline。不要因为首页是“不透明页面”就推断后台已停止。

## 4. Camera 控件状态

- 拍照仅在 `camera/preview` 且无 snapshot/record/mode transition 时启用；录像期间既不
  显示也不能通过其它 GUI 路径请求，服务端仍以
  `snapshot_during_record_unsupported` 做最终门禁。
- 后端 `state=recording` 后，页面锁定在 Camera 并隐藏 Camera/Album 导航、返回主页、
  拍照、RTSP、检测和普通状态提示，只保留白色圆环包围红色圆角方块的 iOS 风格停止
  按钮。停止请求 pending 时按钮禁用但保持可见，直到后端确认状态离开 Recording。
- 顶部录像计时只读取公共状态字段 `record_elapsed_ms`，格式为“已录制 MM:SS”；计时从
  后端录像 Pipeline 成功启动后使用单调时钟计算，不从点击时刻或本地 wall clock 猜测。
- 停止成功、存储异常中断、服务断开或其它后端状态离开 Recording 后恢复普通 Camera
  控件；已在录像前启用的 RTSP 可以继续，但录像态不提供 RTSP 操作入口。
- RTSP toggle 只在 Camera 且 capability=1 时启用。`rtsp_started.url` 或 active
  `get_status.rtsp_url` 是顶部地址的唯一来源；GUI 不读取配置拼接 URL。
- 页面上方 RTSP 文本按“不可用 / 未启用 / 处理中 / 实际 URL / 错误”收敛。断线、停止或
  `rtsp_active=0` 时必须清除旧 URL。
- “实时检测”保持关闭且 disabled；它只是对未来产品决定的视觉占位。

## 5. 相册模型与 Playback 透明区域

### 5.1 媒体目录

`hello.storage_root` 是相册目录 owner，当前默认 `/mnt/extsd/v851s-camera`。GUI 只枚举该
目录第一层的可读普通文件，接受大小写无关的 `.jpg/.jpeg/.mp4`，按修改时间从新到旧，
并限制最多 512 项。这样不会把更宽的 `playback_media_root` 当成相册，也不会形成无界 UI
列表。服务仍对每次 `load_media` 做 canonical path 和普通文件检查。

当前刷新触发点是进入相册、收到 `snapshot_completed`、`record_stopped` 或
`record_interrupted`。若以后加入文件系统 watcher，仍须保持有界、不能在 watcher callback
中发起 MPP 生命周期操作。

### 5.2 照片

选择照片时，GUI 发送幂等 `leave_mode` 释放可能存在的 Camera/Playback/UVC 路径，再用
`QPixmap` 读取文件并在相册预览框内保持比例绘制。JPEG imageformat plugin 和中文字体是
目标 rootfs 的运行依赖；当前主题首选 `Noto Sans CJK SC`，缺失时 Qt 使用平台 fallback，
但 fallback 是否包含中文字形仍需部署检查。

### 5.3 视频

当前 Qt linuxfb rotation=90 时，物理/逻辑/设计坐标关系：

| 含义 | 480×800 物理坐标 | 800×480 逻辑坐标 | 1600×960 设计坐标 |
| --- | --- | --- | --- |
| 相册最大预览框 | `(52,144,388,644)` | `(144,40,644,388)` | `(288,80,1288,776)` |
| Camera 画布 | `(0,0,480,800)` | `(0,0,800,480)` | `(0,0,1600,960)` |

加载视频的顺序是：

```text
选择 MP4
-> GUI: load_media(path)
-> service: 切换到 Playback、DEMUX 读实际媒体信息
-> service: 按 display.scale_mode 在 playback.display_* 内计算实际窗口
-> service: contain 收敛为偶数边界，stretch 直用配置；SetVideoLayerAttr(actual rect)
-> media_loaded(display_x/y/width/height)
-> GUI: 按 linuxfb rotation 逆变换物理 rect，再缩放为设计 rect，仅清除该区域 alpha
-> 用户 play/pause/resume；get_status 持续更新进度
```

Qt 不应根据文件名、固定 16:9 或最大预览框猜测透明区域。停止/关闭 Playback、选择照片
或切到其他 mode 后，透明孔必须消失。暂停时 VO 仍持有最后一帧，因此透明孔保持。

上一项/下一项只是对当前过滤结果重新执行“选择媒体”，视频切换仍走 `load_media` 的原子
关闭/加载路径；播放结束后再次点击播放会重新 load 再 play。当前不自动跳到下一项。

## 6. UVC 真值模型

`uvcToggleButton` 的 checked 状态等于 `mode == uvc`，不是点击动画状态。文字至少区分：

```text
未启用
uvc_waiting_host
uvc_connected
uvc_committed + negotiated profile
uvc_streaming + negotiated profile
uvc_error
```

Host CONNECT/COMMIT/STREAMON 不由 GUI 模拟；按钮也不直接访问 gadget node。UVC 不创建
VO，所以该页完全由 Qt UI layer 绘制。

## 7. OpenCV/YOLO 会话边界

OpenCV 卡片调用 `MainWindow::requestYoloSession()`，沿用以下已存在的安全顺序：

```text
IPC shutdown
-> 等待/必要时终止 camera-mpp-service
-> Qt UI session 以 exit code 42 退出
-> QApplication/linuxfb 析构
-> 外层进程启动 camera-yolo-worker
-> worker 退出并释放 camera/fb/shared lock
-> 外层进程重建 Qt UI session 和 MPP service
-> 新 UI 从 Home 开始，不自动进入 Camera
```

模型、类别或 worker 不可读时只禁用 OpenCV 卡片；不能退化为在 Qt 进程内链接 OpenCV。
Camera“检测”按钮也不能绕过该交接。

## 8. 后续 agent 修改检查表

修改 UI/功能接线时至少核对：

1. 产品行为先更新 `requirements.md`；源码状态再更新 `STATUS.md`，两者不能互相代替。
2. `Dashboard` 保持纯视图，`MainWindow` 保持 IPC 控制器，视频帧不进入 GUI。
3. 所有媒体按钮在点击后回滚本地 checked 假象，再由 response/event 设置最终状态。
4. 页面切换不能用 `applyBackendState()` 强制导航，否则返回主页会被轮询状态弹回功能页。
5. Playback 实际矩形来自 IPC；Camera/Playback 复用 video layer，Qt 只拥有 UI layer。
6. GUI target 保持 Qt 5.12 API 可用性；保持 C++14，不引入 C++17 inline 变量或 Qt 6-only API。
7. 媒体目录、条目数、IPC pending 和 timer 都必须有界；GUI 线程不做编解码或媒体写入。
8. 静态源码完成不等于 Qt 目标构建或 DISP2 实机确认；验证结论只写入 `STATUS.md`。


## 当前 BSP 输入与日志集成

当前 ft6336 驱动的事件为单点 MT 坐标、BTN_TOUCH、SYN_REPORT，没有可用的 MT slot
声明或 Type A 分隔事件；坐标值由驱动按 480×800 处理，EVIOCGABS 却来自 DTS 的
0x480/0x800（1152/2048）。Qt 本地补丁提供显式 single-touch 和 range 参数；当前
横屏测试使用 linuxfb rotation=90 与 event3:rotate=270:single-touch:range=480x800。
这只是该板的输入兼容参数，不修改驱动/DTS，不把它作为通用 ft6336 参数。
Qt 静态设备发现同时修正了把 MT touchscreen 注册成 mouse 的过滤条件。

后端 stdout/stderr 转发到 GUI 进程日志；界面状态和媒体错误使用 IPC 事件。
不再把厂商 ANSI 日志片段直接写入相机状态栏。显示启动、MPP 退出后的 LCD 恢复
见平台 linuxfb-disp2.md。横屏视频帧旋转与物理矩形映射仍需独立适配。

### 当前 V853 物理坐标适配

DisplayGeometry 对应 Qt overlay 的实际 QPainter 旋转；0/90/180/270 分别处理。
MPP hello 中的 display_width/height 是物理边界；窗口回报越界时不创建透明孔。
GUI 在 QApplication 之前核对 display.x/y=0 且尺寸等于 framebuffer，避免旧的
800×480 VO 配置再次产生越界条纹。此映射只处理矩形，不旋转摄像头或解码帧像素。
当前 profile 按用户最终选择使用 contain 保留视野和比例、允许黑边，录像保持 1280×720。
