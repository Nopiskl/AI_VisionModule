# camera-mpp-service 配置

- 文档角色：解释配置 key、覆盖关系、校验和 capability 投影
- 机器可读默认值：[`../../../configs/mpp-service.conf`](../../../configs/mpp-service.conf)
- 解析/校验实现：
  [`ServiceConfig.cpp`](../../../services/camera-mpp-service/src/ServiceConfig.cpp)

本页只描述配置语义，不定义 MPI Create/Bind/Start/Stop 顺序，不保存板测结果。MPP
链路见 [`pipelines.md`](pipelines.md)，当前成熟度见
[`../../STATUS.md`](../../STATUS.md)。

## 1. 事实来源和优先级

| 内容 | Owner |
| --- | --- |
| 产品必须满足的行为 | [`../../requirements.md`](../../requirements.md) |
| 机器可读默认值 | [`../../../configs/mpp-service.conf`](../../../configs/mpp-service.conf) |
| 解析、fallback 和跨字段校验 | `ServiceConfig.hpp/.cpp` |
| 运行时有效值 | 服务加载并校验后的 `ServiceConfig` |
| 对 GUI 暴露的能力 | `hello/get_capabilities` 实现与 [`IPC v1`](../../interfaces/ipc-v1.md) |
| 板端是否支持 | 主维护者认可的实际结果与 [`STATUS.md`](../../STATUS.md) |

配置模板是默认值的唯一机器 owner。本文引用 key 并解释关系，不再复制完整默认值表。

## 2. 配置组

| 前缀 | 消费者 | 职责 |
| --- | --- | --- |
| `ipc.*` | `IpcServer` / `BackendLock`；GUI 只读 layer/lock 门禁 | socket 和跨进程 owner 锁路径 |
| `storage.*` | `FileCommit`、Record/Snapshot | 输出根、媒体根和真实挂载要求 |
| `mpp.*` | `MppRuntime`、各 Pipeline | 公共对齐等 MPP 基础参数 |
| `display.*` | `DisplayOutput`；GUI 只读 layer 门禁 | VO device、video/UI handle、interface/sync、缩放策略和 480×800 物理显示画布 |
| `camera.*` | `CameraPipeline` | ISP/VIPP/VI、capture、帧率和 buffer |
| `snapshot.*` | `SnapshotOutput` | JPEG VENC、输出尺寸/质量和 timeout |
| `record.*` | `RecordOutput` | VI/VENC/MUX、编码、存储监测和 repair metadata |
| `rtsp.*` | `RtspOutput` | 可选 VI/VENC、单帧编码 payload/header 上限、网卡、端口和 stream name |
| `playback.*` | `PlaybackPipeline` | DEMUX/VDEC/CLOCK、候选门禁、保守兼容开关和相册视频子矩形 |
| `uvc.*` | `UvcPipeline` | gadget 节点/bulk 策略、ISP/VI/VENC、固定缓冲池、timeout、码率和单帧上限 |

`display.*` 虽涉及平台显示，但主要由 MPP 服务的 `DisplayOutput` 消费，因此 key 说明
留在这里；GUI 只读取 video/UI handle 和 backend lock 做启动门禁，不复制默认值。
Qt/DISP2 的跨 owner 规则由
[`linuxfb-disp2.md`](../../platform/display/linuxfb-disp2.md) 定义。

`rtsp.enabled=0` 时 capability 为关闭状态，RTSP 的 VI/VENC ID 不参与活动资源冲突和
非负校验，也不会创建网络或 MPP 资源；重新启用前，完整 `rtsp.*` profile 必须通过尺寸、
帧率、码率、buffer、端口、网卡/name token 和 channel 冲突校验。

`uvc.enabled=0` 时 capability 关闭且 GUI 不允许进入模式。启用时 `uvc.video_device`
必须指向 `/dev/video*` 下预配置的 UVC gadget `VIDEO_OUTPUT` 节点；它不是 sensor capture
节点。`bulk_mode` 决定 COMMIT 后立即启动还是等待 STREAMON。`gadget_buffer_count` 和
`frame_queue_depth` 分别限制驱动 mmap buffer 与应用复制队列；`max_frame_bytes` 必须至少
容纳 sample 最大 descriptor 的 1920×1080×2 上限。`venc_vbv_*` 是 MJPEG/H.264 VENC
候选池参数，不与应用队列混为一项预算。

### 2.1 当前显示 profile

当前 V853 物理 LCD/VO 为 480×800，Qt rotation=90 后逻辑界面为横屏 800×480：

```text
display.x/y/width/height = 0/0/480/800
display.scale_mode = stretch
display.video_layer = 0
display.ui_outside_layer = 4
playback.display_x/y/width/height = 52/144/388/644
```

当前 Camera 预览使用 VIPP0/ISP0 的 480×800 采集，与物理窗口相同，避免额外的 VO
宽高比变换。Qt 仍按 rotation=90 绘制横屏 UI。采集 fourcc 使用 sample 的 NV21，
不使用语义不同的 NV21M。

`camera.media_vipp_device=4`、`camera.media_capture_width/height=1280/720`
指定 Snapshot/Record/RTSP 的独立媒体采集。第一次请求媒体输出时才创建/配置/启用 VIPP4，
共享已经运行的 ISP0（use_current_win=1），直到离开 Camera 时释放。
用户确认应从 VIPP0/4/8 选择独立出帧节点；当前使用 0/4，8 保留。
运行固件必须启用对应 vinc@4 并创建 /dev/video4；缺少节点返回
`camera_media_unavailable`，不回退到低分辨率预览，也不关闭 VIPP0 预览。
VIPP4 初始化由互斥锁保护，避免 Snapshot worker 与 RTSP 启动同时创建资源。

`display.*` 是 Camera Preview 的画布，也是 Playback 子矩形的边界。Playback 创建 VO
前复制同一个 `DisplayConfig`（因此 device、channel、interface、video layer 和 UI outside
layer 都不变），只用 `playback.display_*` 覆盖候选画布，再由 `fitWithin()` 按 scale_mode 得到实际矩形：stretch 直接返回配置窗口，
与 sample_virvi2vo 一致；contain 保持源宽高比并收敛到 4:2:0 偶数尺寸/起点。实际值通过 IPC `display_*`
返回 GUI。配置没有新增第二个 video layer；
Qt layer 仍必须与这个唯一 MPP layer 不同。

`playback.display_*` 必须是正尺寸并完整位于 `display.*` 内。修改物理画布、Qt rotation 或相册预览框
时必须同时核对 GUI 设计坐标、framebuffer 几何、配置模板、IPC 和 DISP2 合成，不能只改
其中一处。当前 UI 设计空间为 1600×960，默认物理子矩形经逆旋转对应逻辑 (144,40,644,388)，再对应设计坐标
`(288,80,1288,776)`。

## 3. 参数成熟度

### 冻结约束

只有 requirement 冻结产品行为，例如 Record 编码尺寸、单文件策略、视频-only
Playback、模式互斥和显示 owner。改变这些内容必须先修改 requirement，必要时修改
ADR，不能只改配置。

### 已选默认值

帧率、码率、codec/container、RTSP profile、显示画布等可以是当前产品选择，但在
目标 V851S 板完成验证前仍为 `Candidate`。选择一个默认值不等于证明硬件支持。

### Bring-up 候选

VIPP/VI/VENC/MUX/DEMUX/VDEC/CLOCK/VO channel、buffer 数量、VBV 和 VE frequency 等
可以根据 BSP/板测调整，只要不破坏唯一 owner、互斥、输出安全和显示 layer 边界。

### 安全策略

存储挂载要求、容量保留、轮询周期、repair metadata、Snapshot timeout 等 key 控制
安全策略。具体阈值属于配置；“必须安全停录、不自动格式化、不把 repair tag 当自动
修复”等行为属于 requirement。

## 4. 必须校验的关系

`ServiceConfig` 应在创建任何 MPP 资源前拒绝以下配置：

- Record 编码尺寸违反冻结的 1280×720 产品要求；
- Preview 宽高必须为合法偶数，媒体采集/Record 固定为 1280×720；媒体 VIPP 必须与预览不同；
- video layer 与 UI outside layer 相同，或不满足已接受的显示设计约束；
- Playback 子矩形非正、起点越过显示画布或右/下边界超出 `display.*`；
- 输出根不在 canonical media root 内，或真实挂载要求不成立；
- 同一活动组合中的 VI/VENC/MUX 等 channel 发生 owner 冲突；
- codec/container、buffer、timeout、端口、尺寸或码率超出解析器允许范围；
- 禁用的 RTSP/Playback 能力仍通过 capability 声明为可用。
- UVC 节点不是 `/dev/video*` 绝对路径，布尔值/数量/timeout/码率/VBV/最大帧值不合法，
  或最大帧上限无法覆盖冻结格式矩阵。

校验失败必须在初始化 MPP 前返回稳定错误，不得带着部分默认值继续创建资源。

### 当前 SDK 限制

`record.mux_group` 仅为配置兼容保留，必须为 0；实际 MUX 对象由
`record.mux_channel` 指定。非零 group 在创建任何资源前拒绝。

`display.ui_outside_layer=4` 对应 HLAY(1,0)，GUI 仍须核对实际 framebuffer 地址。

## 5. Playback 门禁

`playback.*` 是创建 VDEC 前的保守拒绝规则，不是产品支持列表：

- codec 开关与 SDK_FEATURES.cmake 取交集；本 SDK 关闭 H.265 解码，\n  即使配置打开也拒绝创建该 VDEC，IPC 不宣称支持；
- source width/height、frame rate、bit rate 是元数据门禁；
- `support_b_frames`、`force_frame_package` 是兼容性策略；
- `display_x/y/width/height` 只定义相册视频的最大显示框，不改变解码尺寸或 layer owner；
- parser/archive 出现在 bundle 中不能证明对应容器可在 V851S 播放。

尚未确认的媒体组合和状态边界见 [`known-limitations.md`](known-limitations.md)。只有
主维护者认可的目标板结果覆盖到具体组合时，才能把它作为板端能力。

## 6. 配置变更规则

修改配置时至少核对：

1. 模板、`ServiceConfig` fallback/校验和 capability 返回是否一致；
2. 是否改变 Accepted requirement 或 Accepted ADR；
3. 是否需要更新目标板部署 profile；
4. 是否需要按主维护者指定范围重新检查构建、目标板行为或兼容性；
5. 是否只更新真正的 owner，而不是在 README、roadmap 和历史记录复制新默认值。
