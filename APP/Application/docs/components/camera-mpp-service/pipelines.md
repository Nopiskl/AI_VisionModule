# camera-mpp-service 实际 Pipeline

- 文档角色：MPP 服务实际实例化链路、所有权和失败回滚的规范设计
- 当前实现：[`src/`](../../../services/camera-mpp-service/src/) 与
  [`include/`](../../../services/camera-mpp-service/include/)
- 通用 MPP/MPI 手册：[`../../platform/mpp/`](../../platform/mpp/)
- 当前限制：[`known-limitations.md`](known-limitations.md)

本页只回答“本服务选择了哪些 VI/VO/VENC/VDEC/MUX/DEMUX/CLOCK 实例，以及怎样启动、
停止和回滚”。SYS/Bind、各模块 API、buffer 所有权和 SmartIPC 的通用解释由平台 MPP
手册维护，不在这里复制。当前源码曾做过静态生命周期核对，结论和未确认边界见
`known-limitations.md`；静态核对不能替代构建或目标板结果。参数值和 channel ID 只看
[`configuration.md`](configuration.md) 与运行配置。

| 知识 | Owner |
| --- | --- |
| `MPP_CHN_S`、Bind、callback、Get/Release、MMZ | 平台 [`core.md`](../../platform/mpp/core.md) |
| VI、VO、VENC、VDEC 的通用接口和状态 | 平台对应模块页 |
| VI→VENC→RTSP/raw/MUX 的可复用组织方式 | 平台 [`smart-ipc.md`](../../platform/mpp/smart-ipc.md) |
| 本服务的 Preview/Snapshot/Record/RTSP/Playback/UVC 实例 | 本页 |
| 本服务的具体参数、ID 和校验 | [`configuration.md`](configuration.md) 与运行配置 |

## 1. 顶层所有权

`camera-mpp-service` 是唯一 MPP owner，服务内任一时刻只有一个顶层 Pipeline：

```text
Idle -> CameraPipeline -> Idle
Idle -> PlaybackPipeline -> Idle
Idle -> UvcPipeline -> Idle
```

Snapshot、Record 和 RTSP 是 Camera 内的按需 Output，不是新的 camera owner。
Camera/Playback/UVC 切换必须先让旧 Pipeline 完整回到 Idle。UVC 只在 Host 取流期间创建
采集/编码模块，并且从不创建 VO。

## 2. 本服务的顶层事务生命周期

这是 `MppService` 的实现约束，不是所有 MPP 应用必须照抄的唯一模块顺序。

创建：

```text
校验配置和 capability
-> 获取 backend lock
-> 初始化 MPP_SYS
-> 按 Source 到 Sink 创建设备/channel
-> 注册 callback
-> Bind
-> Enable/Start
-> 上报 Ready
```

销毁：

```text
拒绝新请求
-> 通知并 join worker
-> Stop/Disable Source 与 Sink
-> 归还全部 frame/stream
-> Unbind
-> 逆序 Destroy channel/device
-> MPP_SYS Exit
-> 释放 backend lock
-> 上报 Stopped
```

每次 Create/Enable/Bind 成功后才设置 owned 状态；中途失败复用同一逆序 cleanup。
`stop()` 必须幂等。callback 和 signal handler 只写有界 mailbox/self-pipe，不直接销毁
Pipeline。

## 3. Camera Preview

实例关系：Preview 使用配置指定的 Preview VI VirChn 和 `DisplayOutput` 的 VO channel；
它不复用 Record/RTSP VirChn，也不把 Qt outside layer 当成 MPP 自有 layer。

```text
AW_MPI_VI_CreateVipp
-> AW_MPI_VI_SetVippAttr / RegisterCallback
-> AW_MPI_ISP_Run
-> AW_MPI_VI_CreateVirChn(preview)
-> AW_MPI_VI_EnableVipp
-> DisplayOutput::create
-> AW_MPI_SYS_Bind(VI, VO)
-> AW_MPI_VI_EnableVirChn(preview)
-> AW_MPI_VO_StartChn
```

停止时先 Disable Preview VI，随后 Stop VO 排空，再 Unbind/Destroy；媒体 Output 停止后
禁用媒体 VIPP 和预览 VIPP，停止共享 ISP，最后销毁各 VIPP。拍照、开始/停止录像和 RTSP
不得重建 Preview。

## 4. SnapshotOutput

Snapshot 使用媒体 VIPP4 上未录像时空闲的 VI channel，不从已经 Bind VO 的 VIPP0
Preview channel 直接拉帧。Snapshot/Record/RTSP 首次启动均先 ensureMediaCapture；
按需创建 VIPP4、设置 1280×720 / NV21 / use_current_win=1、EnableVipp，不重复 ISP_Run。
缺节点或初始化失败只返回媒体错误；VIPP0 预览继续，媒体初始化失败按已持有状态回滚：

```text
Create/Enable temporary VI
-> AW_MPI_VENC_CreateChn(PT_JPEG)
-> RegisterCallback / SetJpegParam
-> AW_MPI_VENC_StartRecvPic
-> AW_MPI_VI_GetFrame
-> AW_MPI_VENC_SendFrame
-> 等待 release-buffer event
-> AW_MPI_VI_ReleaseFrame
-> AW_MPI_VENC_GetStream
-> FileCommit 写入、fsync、无覆盖提交
-> AW_MPI_VENC_ReleaseStream
-> StopRecvPic / Destroy VENC
-> Disable/Destroy temporary VI
```

所有 timeout/error 路径仍需 Release 已获得的 frame/stream。单次只允许一个 Snapshot
任务，退出和模式切换必须等待任务线程收敛。录像期间 `take_snapshot` 是确定的产品拒绝
路径，不创建临时 VI 或 JPEG VENC。

## 5. RecordOutput

这是本产品选择的 VI→VENC→MUX 容器录像，不是 SmartIPC sample 的裸码流 recorder。

```text
验证 storage root/真实挂载/容量
-> Create Record VI
-> Create H.264 VENC
-> Create 当前 SDK 的 MUX channel，设置 repair metadata
-> Bind(VI, VENC) / Bind(VENC, MUX) / 设置 SPS/PPS
-> Enable VI / StartRecvPic / Start MUX
-> 启动有界 storage monitor
```

停止：

```text
停止并 join storage monitor
-> Stop VENC / Stop MUX
-> Unbind(VENC, MUX) / Unbind(VI, VENC)
-> Destroy MUX channel
-> Destroy VENC
-> Disable/Destroy Record VI
-> fsync 临时文件并无覆盖提交
```

存储 monitor 只通知控制线程请求停录，不在 worker 中销毁 MPP。异常文件可以保留为
`.part`；repair metadata 不等于自动修复。

## 6. RtspOutput

这是本产品独立的 VI→VENC→GetStream 输出；RTSP server 是用户态 sink，不是 MPP
module。通用参数集、关键帧和慢消费者处理见平台
[`smart-ipc.md`](../../platform/mpp/smart-ipc.md)。

```text
Create RTSP VI
-> Create H.264 VENC / 配置 frame rate
-> Bind(VI, VENC)
-> Enable VI / StartRecvPic
-> 复制 VENC SPS/PPS 到 service-owned header
-> 解析网卡 IPv4 / 预检监听 address:port
-> 创建 RTSP server/media stream
-> 以 FRAME_DATA_TYPE_HEADER 初始化 SDP 参数集
-> 注册新客户端 callback / 启动 server / 请求初始 IDR
-> 启动有界 GetStream worker
```

固定 sun8iw21 VideoEnc 的 by-frame 输出每帧只填一个 pack，但 pack 可含 Addr0/1/2 三段。
worker 对三段做地址和溢出检查，复制到 `rtsp.max_frame_bytes` 限制的自有 buffer，并在
进入可能阻塞的 TinyServer 前先 `AW_MPI_VENC_ReleaseStream`。只有
`ERR_VENC_BUF_EMPTY` 是正常轮询超时；其他 Get/Release 错误投递 `RtspFault`，由控制线程
停止 Output。

新客户端 callback 只设置原子 IDR 请求。worker 调用 `AW_MPI_VENC_RequestIDR` 后继续
检查实际 frame type，在观察到 I 帧以前丢弃 P 帧；I 帧按 `HEADER(SPS/PPS)`、`I payload`
两个正确类型依次送给 `MediaStream`。停止时先通知并 join worker，再停止 server、
VI/VENC、Unbind 并逆序销毁；200 ms GetStream timeout 为正常取流循环提供退出边界，
TinyServer 自身的慢客户端阻塞仍见 `known-limitations.md`。RTSP 不创建第二个 sensor
owner。

## 7. UvcPipeline

UVC 是与 Camera/Playback 互斥的顶层 Pipeline，参考 sun8iw21 `sample_uvcout`，但不复制
其 `main()`、全局 signal 状态或再次调用 `AW_MPI_SYS_Init/Exit`。MPP_SYS 仍由服务级
`MppRuntime` 唯一管理。

进入模式只建立 gadget 控制面：

```text
open(uvc.video_device, O_RDWR|O_NONBLOCK|O_CLOEXEC)
-> VIDIOC_QUERYCAP(VIDEO_OUTPUT + STREAMING)
-> VIDIOC_SUBSCRIBE_EVENT(CONNECT/DISCONNECT/STREAMON/STREAMOFF/SETUP/DATA)
-> 启动 UVC event loop
-> WaitingHost
```

SETUP/DATA 路径处理 `UVC_VS_PROBE_CONTROL` 与 `UVC_VS_COMMIT_CONTROL`。格式/帧索引只
接受 requirement 冻结的 sample 矩阵，无效索引归一到默认 profile；COMMIT 随后执行
`VIDIOC_S_FMT` 并拒绝驱动静默修改宽高或 fourcc。isochronous 模式等待 STREAMON；bulk
模式遵循 sample，在 COMMIT 后启动，重复 STREAMON 幂等。

Host 开始取流时：

```text
VIDIOC_REQBUFS + QUERYBUF + mmap + QBUF（固定数量）
-> Create/Set VIPP -> ISP Run -> Create VI -> Enable VIPP
-> MJPEG/H.264: Create VENC -> Bind(VI,VENC)
-> Enable VI -> Start VENC
-> 建立固定深度应用复制池
-> H.264: 先缓存不可被背压淘汰的 SPS/PPS，随后请求 IDR
-> 启动 capture worker
-> VIDIOC_STREAMON
```

MJPEG/H.264 worker 使用一个 by-frame `VENC_PACK_S`，检查 Addr0/1/2 总长度后复制，并在
入应用队列前 `AW_MPI_VENC_ReleaseStream`。H.264 起流会在实际 I/IDR slice 出现前丢弃
普通编码帧，确保 Host 观察到 SPS/PPS→IDR 的首帧边界；SPS/PPS 优先块不会被慢 Host 的
最旧帧淘汰策略移除。YUYV 只支持 320×240：worker 拉取 NV21 VI
frame，按实际 stride 转换为 Y0/U/Y1/V 后立即 `AW_MPI_VI_ReleaseFrame`。队列满时复用并
淘汰最旧帧；gadget writable 时才 DQBUF、复制和 QBUF。任何已成功 Get 的媒体对象都由
同一迭代 Release，gadget buffer 则由唯一 mmap 表释放。

STREAMOFF/断连/切换的收敛顺序：

```text
可用时 VIDIOC_STREAMOFF
-> 请求并 join capture worker
-> Stop VENC / Disable VI
-> Unbind(VI,VENC)
-> Reset/Destroy VENC
-> Destroy VI / Disable VIPP / Stop ISP / Destroy VIPP
-> munmap 全部 gadget buffer / VIDIOC_REQBUFS(count=0)
-> Committed、WaitingHost 或最终 Stopped
```

event loop 是 UVC 子状态的串行控制路径；MPI callback 和 capture worker 只投递故障或
数据。`MppService::leaveMode()` 先请求 event loop 退出并 join，随后执行同一幂等 cleanup，
因此不会与下一顶层 Pipeline 并发创建 MPP。configfs/UDC/ADB 不属于本 Pipeline。

## 8. PlaybackPipeline

Playback 是与 Camera 互斥的顶层 Pipeline；VDEC 不插入实时 camera→RTSP 链路。

```text
检查 canonical media path
-> Create DEMUX / callback / GetMediaInfo
-> 按配置执行 codec/尺寸/FPS/码率/B-frame 门禁
-> Create VDEC(actual codec/size)
-> 从 display.* 复制 device/layer/interface，并以 playback.display_* 替换画布
-> DisplayOutput::fitWithin(相册子矩形, actual source size)
-> DisplayOutput::create(effective fitted rectangle)
-> Create CLOCK
-> Bind(DEMUX,VDEC)
-> Bind(VDEC,VO)
-> Bind(CLOCK,DEMUX)
-> Bind(CLOCK,VO)
-> Start CLOCK/VDEC/VO/DEMUX
```

Pause/Resume、EOF 和 error callback 只改变状态或投递事件。Stop 依次停止 VO、VDEC、
DEMUX、CLOCK，再按 `CLOCK→VO`、`CLOCK→DEMUX`、`VDEC→VO`、`DEMUX→VDEC` 反向
Unbind；当前 `close()` 随后销毁 VO、VDEC、DEMUX、CLOCK。任何失败都应回到 Idle。
加载成功后，Pipeline 把 effective fitted rectangle 随 `media_loaded` 上报，并在已加载的
`get_status` 中重复返回；Qt 据此只清除实际视频像素区域。Camera 和 Playback 复用配置的
同一个 video layer，顶层模式互斥保证二者不会并存。

## 9. DisplayOutput

`DisplayOutput` 只完整管理配置指定的 MPP video layer：

```text
AW_MPI_VO_Enable
-> Get/SetPubAttr(interface/sync)
-> 可选 AddOutsideVideoLayer(UI handle，仅登记)
-> EnableVideoLayer(video handle)
-> Get/SetVideoLayerAttr(rect/pixel format)
-> CreateChn / RegisterCallback
-> StartChn
```

销毁按相反顺序执行。对 UI outside layer 只允许 Add/RemoveOutside，不得 Open/Close、
Enable/Disable、SetAttr 或 SetPriority。Qt/DISP2 平台规则见
[`linuxfb-disp2.md`](../../platform/display/linuxfb-disp2.md)。

当前 `GetVideoLayerAttr -> 修改 stDispRect/enPixFormat -> SetVideoLayerAttr` 顺序与提供的
指定区域显示说明一致；这里只使用当前 V851S bundle 已有的公开 VO 结构/API。参考文档
来自其他 sun8iw 系列只能证明调用语义，不能替代 V851S 实机能力证据。

## 10. 并发和事件

- `MppService` 控制线程是顶层模式变化的唯一执行者；UVC event loop 是其 Pipeline-local
  Host 子状态控制者，顶层离开必须先停止并 join 它；
- `EventMailbox`、IPC 连接缓存和 frame/stream buffer 必须有界；
- signal self-pipe、MPI callback、Snapshot/RTSP/Storage worker 和 UVC capture worker
  只投递消息或有界媒体块；
- worker 的退出条件必须可唤醒，销毁依赖资源前先 join；
- GUI 与 CLI 均通过 [`IPC v1`](../../interfaces/ipc-v1.md) 进入同一命令路径。

## 当前 SDK 接口适配（2026-09-15）

当前 v853-100ask SDK 的 MUX 是单层 channel API：录像元数据和文件属性合并在 MUX_CHN_ATTR_S；创建、启停和销毁均使用 record.mux_channel。record.mux_group 仅为旧配置兼容保留，必须为 0。先建立 VENC→MUX tunnel，再写入 SPS/PPS；正常停录使用 StopChn(FALSE) 排空缓存。DEMUX 使用 SOURCETYPE_FD，不再设置已移除的 mStreamType。H.265 解码以导入 SDK 的实际编译功能为门禁，当前 SDK 未启用。

## 当前 SDK 的 Playback 停止顺序

Playback 先 Stop DEMUX、Stop VDEC，再 Stop VO，最后 Stop CLOCK；所有 tunnel 保留到
VO 归还帧之后再 UnBind/Destroy。当前 VideoRender 在 Idle 状态仍接受输入帧，若先停 VO，
VDEC 可能继续交付最后一帧。UnBind 清空 hTunnel 后，Destroy 归还该帧会访问空 tunnel。
该约束针对当前 SDK 的实际组件实现，不能把简单的反向启动顺序视为充分条件。

EOF 还有独立约束：VideoRender 自动进入 Idle 时保留最后两帧，StopChn(Idle) 不归还帧，
Seek 也刻意保留 used 帧。因此停止生产者后，先 Stop VO，再清除 StreamEof，并执行一次
Start/Stop VO 的同步状态转换；CLOCK 和 VDEC tunnel 在此期间仍有效。这样在 UnBind
前完成帧归还，避免自然播放结束时 Destroy 访问已清空的 tunnel。此处理只作用于
Playback 的 video channel，不操作 Qt UI layer。

### Camera Preview 的 tunnel 排空

当前 SDK VideoRender 在 Idle 仍可能接收上游帧；UnBind 会清空 tunnel handle，
DestroyChn 又会返还残留帧，导致空 handle 访问。Camera stop 必须先 DisableVirChn
停止生产者，再 StopChn 排空 VO，最后 UnBind/Destroy。VI 的 FillThisBuffer 支持 Idle
状态返还帧；不要把 sample 中未显式 UnBind 的销毁片段直接拼接到本服务。
