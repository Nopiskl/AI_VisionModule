# camera-mpp-service 已知限制

本页保存当前组件内部已经确认的实现边界和仍未知的集成风险。它不是测试计划，也不规定
验证命令；具体检查范围和通过条件由主维护者在相应任务中指定。项目当前能力和 blocker
仍以 [`STATUS.md`](../../STATUS.md) 为准。

## 当前检查边界

当前基线为用户提供的 v851s-camera-handoff-20260915 源码包及当前 v853-100ask SDK，
不再使用旧 Yuzukilizard 预编译 bundle。MPP service、Qt GUI、YOLO worker 已使用当前
GCC 6.4.1 ARM/musl 工具链完成编译、最终链接和本地 staging 安装。
MPP 来源/符号审核、sensor 和公共结构 ABI 对比、UVC 内核 ABI 断言均通过。
Qt 5.12.9、OpenCV 4.1.0 VIN/ISP 与 AWIspApi 也已交叉构建；构建事实之外已有有限板测，具体硬件、配置与结果以 STATUS 为准。

源码生命周期曾按以下范围进行静态核对：

| Owner | 已确认的源码结构 | 不能据此推断 |
| --- | --- | --- |
| `MppRuntime` / `BackendLock` | SYS Init/Exit 配对；MPP 和 YOLO 复用 common flock 格式；Qt 启动的服务设置 parent-death signal | BSP 初始化和异常退出后的设备状态 |
| `DisplayOutput` | 只对 UI layer Add/RemoveOutside；video layer/channel 逆序销毁 | framebuffer 映射、alpha、z-order 和真实合成 |
| `CameraPipeline` | VI/ISP/VO 分阶段持有，失败进入统一 cleanup，stop 幂等 | sensor/ISP 参数和 Preview 连续性 |
| `SnapshotOutput` | VI frame、VENC stream 的 Get/Release 路径成对；释放/错误事件只唤醒，ISP2VE 参数同步回填；录像中请求由服务端拒绝 | JPEG timeout、退出等待和 Preview 连续性 |
| `RecordOutput` | VI/VENC/MUX 的 Bind/Start/Stop/UnBind/Destroy 有逆序路径 | MP4 文件系统行为、拔卡、满盘和断电恢复 |
| `RtspOutput` | SPS/PPS header 缓存、新客户端 IDR 门禁、三片段 bounded copy、先 Release 后 sink、取流错误事件和幂等回收 | 纯视频 SDP、慢客户端、重连和析构行为 |
| `PlaybackPipeline` | DEMUX/VDEC/VO/CLOCK 的绑定、停止和逆序释放完整；从相册最大框计算 fitted rectangle 并通过 IPC 回传 | codec/container 支持、实际子矩形合成及 Pause 后 Start 的可靠性 |
| `UvcPipeline` | PROBE/COMMIT/STREAMON/OFF 状态、固定格式表、gadget mmap 表、有界复制池、每次 Get/Release 配对、断连/停止逆序回收 | gadget kernel 实际行为、Host 枚举、逐格式画面、bulk/isochronous、CPU YUYV 成本和反复重连 |

上表只描述源码结构，不是板端能力声明。实际 Pipeline 顺序仍以
[`pipelines.md`](pipelines.md) 为准。

## 构建、ABI 与发布

- 当前解压目录没有独立 Git 基线；已完成 ARM/musl 构建和本地安装，已部署 UDISK 隔离板测目录，尚未完成正式 rootfs/package 集成；
- standalone MPP bundle 来自当前 SDK staging，并已验证最终链接；必须保持头文件、库、kernel UAPI、工具链和目标固件同源；
- 上游 `Software/sunxi-mpp` 没有覆盖重新发布的明确软件许可，VIPLite 头文件含专有声明；
  授权和最终 rootfs 来源未确认前不能提交或发布生成 bundle；
- Unix socket 的最终用户/组、启动系统权限及 SELinux/AppArmor（若存在）仍需在部署
  环境确定。

外部 bundle 的生成方式、ABI 和许可边界见
[`mpp-reference-tools`](../mpp-reference-tools/README.md) 与
[`reference-sources.md`](../../platform/reference-sources.md)。

## Camera 与 Snapshot

- 当前预览 VIPP0 480×800，媒体采集改为独立 VIPP4 1280×720；目标固件必须启用 vinc@4。
  当前读到的运行 DT 中该节点 disabled，/dev/video4 不存在。应用已编译并验证缺节点
  请求被拒绝、预览保留；VIPP4 出帧、JPEG/MP4/RTSP 正常路径待用户配置固件后验证。
  早期 JPEG/Record/RTSP 成功来自单 VIPP0 1280×720，不能外推为新双路配置已通过。

- Snapshot 当前采用单任务线程，并在未录像时临时使用第二 VI channel；1280×720 JPEG 已完成一次板端读取验证；
  JPEG timeout、退出最大等待和长期 Preview 连续性尚未覆盖；
- 产品已决定录像期间不允许 Snapshot；服务端返回
  `snapshot_during_record_unsupported`，GUI 进入录像态后不提供拍照入口；
- 当前物理 480×800、Qt rotation=90；预览采集亦为 480×800。VIN 会按该比例裁剪 sensor，
  预览与未来独立 1280×720 媒体流的视野不相同；显示画布和录像编码尺寸不得混用。

## Record 与存储

- `RENAME_NOREPLACE` 的兼容 fallback 存在短暂零长度占位窗口；目标文件系统的原子性、
  目录同步和断电行为不能由源码检查保证；
- `.part` 会执行 fsync，提交后尝试同步父目录，但具体可移动文件系统是否支持仍未知；
- repair metadata 已进入 MUX 配置，但 bundle 没有 `libfilerepair`；当前没有启动扫描、
  自动修复或已确认的突然断电恢复能力；
- 挂载点设备号检查针对独立 SD mount；bind mount 或同文件系统拓扑没有被证明；
- 满盘、拔卡、慢存储和真实 I/O 故障的受控停录只有设计及源码路径，没有目标板结果。

## RTSP

- 网卡不存在或没有配置 IPv4 时启动失败，部署必须提供明确网络配置；
- 启动会先对配置的 IPv4/port 做临时 bind 预检，能把常见网卡、权限和端口占用问题作为
  `start_rtsp` 错误返回；预检关闭到 TinyServer 真正监听之间仍有竞态；
- 已在 loopback/单客户端完成三轮启动取流停止与录像并发；TinyServer 内部队列、
  `appendVideoData` 最坏阻塞、慢/多客户端与长时间启停仍未确认；产品已在调用 TinyServer 前复制并 Release VENC stream，但
  vendor sink 阻塞仍可能拖慢 GetStream worker 和 stop 收敛；
- 固定 TinyServer ABI 没有 audio-disabled 值，单播 SDP 可能声明没有数据的 AAC track，
  即使产品没有创建 AENC、也不追加音频；
- SPS/PPS 已通过 `FRAME_DATA_TYPE_HEADER` 显式写入 TinyServer 参数集缓存，客户端 PLAY
  callback 也会触发 `RequestIDR` 并等待实际 I 帧；SDP/SETUP/PLAY/TEARDOWN 及实际 SPS/PPS/IDR 已在 loopback 抓包确认；
  真实网络首屏、并发/慢客户端和长循环仍需验证；
- vendor `TinyServer` 在 RTSPServer 创建失败时可能直接 `exit(1)`；address/port 预检降低
  了常见触发概率，但不能消除检查与创建之间的竞态，仍需替换为可返回错误的实现；
- 产品绕开了旧 `streamURL()` 的可疑释放路径，并由校验后的 IPv4/port/name 构造 URL；
- Record + RTSP 需要两个 H.264 VENC；Snapshot 不与 Record 并发。当前 V853 已完成该组合短时测试；持续运行的 VE/CMA/带宽余量和其它型号仍未确认。

## Playback

- 当前 MPP 头文件没有独立 DEMUX/CLOCK Resume API，Pause 后使用 Start 恢复的已在当前 H.264 录像上完成有限循环；其它媒体的实际行为仍需确认；
- 当前 SDK 未启用 H.265 解码，服务即使收到 allow_h265=1 也会拒绝该 codec；
- 当前应用生成的 H.264 MP4 已板测；其它 H.264 文件、MJPEG、MOV、MPEG-TS、MPEG-PS/MPG 仍为候选，
  不能从 parser/archive 名称推断为目标板支持；
- codec、尺寸、FPS、码率、B-frame 和 frame-package 门禁只是创建 VDEC 前的保守拒绝
  规则；metadata 为 0 表示未知，不能解释为通过；
- 损坏/截断文件、无视频轨文件、EOF、Pause/Resume 和 Camera↔Playback 切换行为除当前 H.264 正常文件的 EOF/Pause/Resume 外，仍没有
  完整目标板结论；
- 当前相册最大物理框为 `(52,144,388,644)`，映射 Qt 逻辑 `(144,40,644,388)`；
  局部透明孔已有当前 H.264 截图，最终比例策略和更多媒体仍需确认；
- 音频、A/V sync、seek、自动连续播放/队列管理、倍速和字幕不在当前范围；GUI 的手动
  上一项/下一项只是重新加载所选媒体。

## UVC Out

- 当前源码只消费已有 gadget video 节点；不创建 configfs descriptor、不选择 UDC，也不
  启停或组合 ADB。部署若没有用相同 format/frame index 建立 descriptor，协商结果和
  `UvcPipeline` 固定表可能不一致；
- sample 矩阵中的 MJPEG/H.264 1080p、30 fps 和相应 VENC/CMA/USB 带宽尚无 V851S
  结果，源码能够选择 profile 不等于目标板能够稳定输出；
- YUYV 320×240 使用 CPU 做 stride-aware NV21→YUYV，避免引入 sample 的 `/dev/g2d`
  私有 UAPI；实际 CPU 占用、色序和 cache 可见性仍需用 Host 图像确认；
- H.264 路径按 sample 先排入受保护的 SPS/PPS、请求 IDR，并丢弃观察到 I/IDR slice 前的
  普通帧；Host 对 UVC H.264 payload 的兼容、参数集帧边界和重连首帧仍未确认；
- gadget `VIDIOC_S_FMT/REQBUFS/STREAMON/OFF` 的具体 kernel 行为、disconnect 后 ioctl
  返回、bulk COMMIT 即启流以及 mmap buffer 长度都必须在目标 BSP 上确认；
- 队列满时会淘汰最旧帧并累计 `uvc_dropped_frames`，这是安全收敛策略，不保证慢 Host
  下的连续帧率；
- 当前没有 ADB composite 兼容性声明；这是本轮明确的非目标，而不是已经验证不兼容。

## Display、YOLO 与综合资源

- GUI 已实现 `Disp2LayerAdapter`：启动 MPP 前读取配置、用
  `FBIOGET_FSCREENINFO` / `DISP_LAYER_GET_CONFIG` 只读核对 framebuffer 与 layer，并拒绝 UI/video 重叠、outside
  不匹配或 MPP/YOLO lock 路径不一致；当前内核未实现旧 FBIOGET_LAYER_HDL ioctl，默认候选为 channel=1/layer=0、MPP handle=4；板测确认实际 UI handle=4、video=0；
- framebuffer 格式、handle、当前 alpha/z-order 已有板端证据；最终显示比例、方向验收仍属于 OPEN-012；
- MPP 不能为解决透明叠加问题而写 Qt framebuffer 或重配 Qt UI layer；
- YOLO 产品 target 已接入 shared backend lock、显式 INT8 输入/INT16 DFP 输出 tensor
  门禁、VIPLite 失败回收、可配置 V4L2/fb、RGB565 stride/offset mmap 写入和
  Qt→YOLO→Qt 会话监督；Qt/OpenCV/worker 交叉构建已完成，进程切换和设备释放尚无板测结果；
- YOLO 会话刻意不保留 Qt overlay，返回 Qt 依赖 worker 退出、外部终止或外层监督器的
  `SIGUSR1`；这不是 UI 内返回按钮；
- Preview + Record + RTSP 是当前 Camera 最大候选资源组合，已做 loopback RTSP 与录像短时并发；Snapshot 只在未录像的
  Preview 中按需运行。资源数量、CMA/ION、CPU、带宽和温度明确前仍不能承诺该组合；
- UVC 与 Camera/Playback 顶层互斥，不会叠加到 Camera 最大候选组合；进入 YOLO 前同样
  通过服务 shutdown 停止 event/capture 线程、释放 gadget/MPP 资源后再交接 backend lock。

跨组件的资源计算原则和 owner 关系见
[`resource-ownership.md`](../../architecture/resource-ownership.md)。
