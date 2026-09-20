# Camera backend IPC protocol v1

本协议是 `camera-gui`、后续 CLI 与 `camera-mpp-service` 之间唯一的产品控制接口。
它不传输视频帧，也不把 stderr/log 当作业务状态。当前实现和已确认事实统一查看
[`../STATUS.md`](../STATUS.md)；本页只维护 v1 的线格式和语义。

## 1. 传输和帧格式

- 传输：Unix-domain `SOCK_STREAM`；默认路径
  `/run/v851s-camera/mpp.sock`；
- 编码：UTF-8 字节串；每条消息以 `\n` 结束；
- 单条消息（含换行）最大 8192 bytes；服务端每个客户端的输入和输出缓存均有界；
- 字段以 TAB 分隔，格式为 `key=value`；值使用 `%HH` 转义 TAB、换行、`%` 等
  非安全字节；重复 key、非法转义和缺少必需字段均被拒绝；
- 当前版本固定为 `v=1`。

请求示例（文档中的 `<TAB>` 表示一个真实 TAB 字节）：

```text
v=1<TAB>type=request<TAB>request_id=7<TAB>name=enter_mode<TAB>mode=camera\n
```

响应保留原 `request_id` 和命令 `name`：

```text
v=1<TAB>type=response<TAB>request_id=7<TAB>name=enter_mode<TAB>ok=1<TAB>mode=camera<TAB>state=preview\n
```

事件没有必需的 `request_id`：

```text
v=1<TAB>type=event<TAB>name=record_started<TAB>path=/mnt/extsd/v851s-camera/record-...mp4\n
```

同一个同步命令产生的 response 会先发给请求者，之后才广播该命令产生的 event。
MPP callback 事件通过有界 mailbox 异步进入控制线程，因此相对于其他客户端请求
仍是异步的。客户端必须用 `request_id` 匹配 response，并独立处理 event。

## 2. 公共响应字段

| 字段 | 含义 |
| --- | --- |
| `ok` | `1` 成功，`0` 失败 |
| `code` | 失败时稳定的机器可读错误码 |
| `detail` | 面向诊断的说明，不应用作状态机输入 |
| `mode` | `none`、`camera`、`playback` 或 `uvc` |
| `state` | 当前 Pipeline 状态，例如 `idle`、`preview`、`recording`、`ready`、`playing`、`paused`、`ended`、`uvc_waiting_host`、`uvc_connected`、`uvc_committed`、`uvc_streaming`、`uvc_error` |
| `mailbox_dropped` | MPP callback mailbox 因有界策略累计丢弃/淘汰的事件数 |
| `snapshot_busy` | `1` 表示唯一 Snapshot 任务仍在执行，`0` 表示可接收新请求 |
| `record_elapsed_ms` | 录像时表示从 Record Pipeline 成功启动起由后端单调时钟计算的毫秒数；未录像时为 `0`。GUI 只能展示该字段，不从点击时刻推算 |
| `rtsp_active` | `1` 表示 Camera 的 RTSP output 正在运行；它不改变顶层 Camera `state` |
| `rtsp_url` | 仅 RTSP active 时出现的实际播放地址；GUI 必须展示该值，不自行拼接 IP、端口或 stream name |
| `uvc_host_connected` | `1` 表示 UVC Host 已连接；不等于已经 COMMIT 或开始传输 |
| `uvc_streaming` | `1` 表示 UVC gadget 和采集路径正在传输 |
| `uvc_device` | 配置的 gadget `VIDEO_OUTPUT` 节点 |
| `uvc_format` | 当前已 COMMIT 的 `mjpeg`、`yuyv`、`h264`；未协商时为 `none` |
| `uvc_width` / `uvc_height` / `uvc_frame_rate` | 当前 UVC profile；未协商时为 `0` |
| `uvc_dropped_frames` | 有界应用帧池因 Host 背压累计淘汰的旧帧数 |

以上状态字段由 `addStatusFields` 附加到所有 response 和异步 event；客户端不需要等待
下一次 `get_status` 才能观察 UVC 子状态。

## 3. v1 命令

| 命令 | 输入字段 | 合法状态 | 行为 |
| --- | --- | --- | --- |
| `hello` / `get_capabilities` | 无 | 任意已启动状态 | 返回协议、显示 profile/layer、录像参数、Playback 门禁和 capability；UVC/RTSP 按配置为 `0/1`，audio/seek 为 `0`。另返回 `storage_root`、`playback_media_root`、Playback 最大显示矩形、`uvc_device` 和固定 `uvc_formats` |
| `get_status` | 无 | 任意 | 返回公共 mode/state/UVC 字段；Playback 另含进度、媒体路径及已加载时的实际显示矩形，Recording 另含路径和后端已录时长，RTSP active 时另含 `rtsp_url` |
| `enter_mode` | `mode=camera\|playback\|uvc` | 任意 | 完整停止旧 Pipeline，再进入新模式；UVC capability=0 时返回 `capability_disabled` |
| `leave_mode` | 无 | 任意 | 幂等停止当前 Pipeline，回到 `none/idle` |
| `take_snapshot` | 可选 `filename` | Camera Preview，且未录像 | 启动唯一 Snapshot 任务并立即确认，完成结果随后通过 event 上报 |
| `start_record` | 可选 `filename` | Camera Preview | 启动录像，Preview 保持运行 |
| `stop_record` | 无 | Camera Recording | 停止并提交 MP4，Preview 保持运行 |
| `start_rtsp` | 无 | Camera Preview/Recording，且 RTSP capability=1 | 启动配置的 RTSP 输出；不重启 Preview |
| `stop_rtsp` | 无 | Camera 且 RTSP active | 停止 RTSP 输出；Preview/Record 保持 |
| `load_media` | `path` | 任意 | 验证路径位于 media root，切换到 Playback 并加载媒体 |
| `play` | 无 | Playback Ready | 启动视频播放 |
| `pause` | 无 | Playback Playing | 暂停播放 |
| `resume` | 无 | Playback Paused | 恢复播放 |
| `stop_playback` | 无 | Playback 非 Idle | 停止播放，保留 Playback mode/Idle |
| `shutdown` | 无 | 任意 | 停止 Pipeline；响应发送后服务退出；GUI 用它开始 MPP→YOLO 会话交接 |
| `seek` | 无 | 任意 | v1 固定返回 `capability_deferred` |

输出文件只允许位于 `storage.root`，其 canonical path 仍必须位于 canonical
`playback.media_root` 内；可选 `filename` 最长 128 bytes，只接受字母、数字、点、
横线和下划线。已有目标和 `.part` 文件都不会被覆盖。未指定名称时，服务使用
时间戳加 `request_id` 生成唯一候选名。Playback 输入拒绝内嵌 NUL，并在打开前通过
`realpath` 和普通文件检查，不得越过 `playback.media_root`。

## 4. v1 事件

| 事件 | 关键字段 | 含义 |
| --- | --- | --- |
| `state_changed` | `mode/state` | 成功命令后的后端确认状态 |
| `snapshot_started` | `path` | 唯一 Snapshot 任务已被后端接受 |
| `snapshot_completed` | `path` | JPEG 已 fsync 并完成无覆盖提交；原子性取决于下述文件系统路径 |
| `record_started` | `path/width/height/record_elapsed_ms` | 录像分支已经启动；公共计时字段从 `0` 开始 |
| `record_stopped` | `path` | 录像已停止，临时文件已提交为最终路径 |
| `record_file_done` | `path` | 后端报告当前文件结束；首版不做自动分段 |
| `record_interrupted` | `path/code/detail/committed` | 存储异常中断录像；`committed=0` 时 `.part` 可能被保留 |
| `rtsp_started` | `url/width/height/frame_rate` | RTSP URL 已可供客户端使用 |
| `rtsp_stopped` | `url` | RTSP 已停止，Camera Preview 保持 |
| `rtsp_error` | `url/code/detail` | RTSP 运行错误并停止输出 |
| `uvc_connected` | `device` | USB Host 已连接；服务仍等待协商或取流 |
| `uvc_disconnected` | `device` | Host 已断开；传输、gadget buffer 和本次采集/编码资源已收敛，UVC mode 保持等待重连 |
| `uvc_committed` | `profile` | Host 已完成有效 COMMIT；`profile` 形如 `mjpeg 1280x720@30` |
| `uvc_streaming_started` | `profile` | Gadget output、VI 和可选 VENC 已开始传输 |
| `uvc_streaming_stopped` | `device` | Host STREAMOFF 已停止本次传输；UVC mode 保持可再次取流 |
| `uvc_error` | `code/detail` | gadget/control/capture/output 发生不可忽略错误；客户端应展示错误，并通过模式切换重试 |
| `media_loaded` | `path/duration_ms/source_width/source_height/codec_id/codec/frame_rate_milli_fps/average_bit_rate/maximum_bit_rate/display_x/display_y/display_width/display_height` | 媒体已通过后端校验并加载；`display_*` 是保持源宽高比后实际写入 VO layer 的物理屏幕矩形 |
| `rendering_started` | `source` | 后端开始渲染视频 |
| `playback_eof` | `path` | 播放到达 EOF，状态变为 Ended |
| `backend_error` | `code/detail` | 后端运行错误 |
| `protocol_error` | `code/detail` | 单条客户端消息无法解析或类型错误 |

GUI 的拍照 busy、录像中、已录时长、loaded/playing/paused/EOF，以及 UVC 等待/连接/
协商/传输/错误状态必须由 response/event 确认；点击本身不构成状态变化。录像状态下
GUI 只保留停止录像操作，服务端同时拒绝 `take_snapshot`。UVC 格式和开始/停止取流由
Host 控制，GUI 只有进入/离开 UVC mode 的产品控制权。

### 4.1 GUI 使用的 capability/profile 字段

`hello/get_capabilities` 中与本轮 800×480 UI 集成直接相关的字段如下；它们是新增字段，
不改变 v1 已有命令语义：

| 字段 | 当前语义 |
| --- | --- |
| `display_width` / `display_height` | MPP 物理显示画布；当前 V853 profile 为 `480/800`，不等于旋转后的 Qt 屏幕尺寸 |
| `video_layer` / `ui_layer` | MPP VO 与 Qt/linuxfb 的 layer handle；两者必须不同 |
| `storage_root` | Snapshot/Record 的产品输出目录；GUI 相册只扫描该目录 |
| `playback_media_root` | 服务端允许 `load_media` 的安全根；GUI 不用它扩大相册扫描范围 |
| `playback_display_x/y/width/height` | 相册视频预览的最大物理矩形，当前 V853 profile 为 `52/144/388/644` |

最大矩形不是每个视频的实际透明区域。后端按照源宽高比计算 VO fitted rectangle，并在
`media_loaded` 以及已加载 Playback 的 `get_status` 中用 `display_*` 返回。Qt 只能对这个
实际矩形清 alpha；停止/关闭 Playback、切换照片或离开 Playback 后必须恢复不透明相册
背景。照片不经过 IPC 和 MPP，由 GUI 从 `storage_root` 读取并绘制。

## 5. 协议演进与实现边界

- v1 的字段、命令或事件发生不兼容变化时必须升级协议版本；
- 新增可选能力时先扩展 `get_capabilities`，GUI 不得根据按钮或配置名称猜测支持；
- request/response 兼容性必须同时核对共享 codec、服务端和所有客户端；
- stderr、日志文本、进程存在或本地按钮状态都不能替代协议状态；
- YOLO 不复用本协议：切换发生在 MPP IPC shutdown、Qt UI 进程退出和外层进程监督层，
  YOLO 会话当前没有与 Qt 并存的 UI client；
- 本协议不定义 MPP 模块、线程、文件提交、显示 layer 或板端验证实现。

后端 Pipeline、配置和技术限制见
[`camera-mpp-service 组件文档`](../components/camera-mpp-service/)。具体兼容性和
故障检查由主维护者在任务中指定，当前状态见
[`STATUS.md`](../STATUS.md)。
