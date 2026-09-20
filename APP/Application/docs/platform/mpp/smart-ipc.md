# SmartIPC 参考流程：采集、编码、预览、录像与 RTSP

这里的 SmartIPC 指“智能网络摄像机（IP Camera）场景”，不是
[`IPC v1`](../../interfaces/ipc-v1.md) 所说的进程间通信协议。它不是一个 MPP 模块或
一组 `AW_MPI_SMARTIPC_*` API，而是把 VI、ISP、VENC、可选 VO/AI、存储和 RTSP 组织
起来的一种应用架构。

本页分开描述：

1. sun8iw21 `sample_smartIPC_demo` 实际做了什么；
2. 从多个 sample 提炼出的可复用 SmartIPC 模型；
3. 与本项目 `camera-mpp-service` 的差异。

UVC Out 不是本页所说的网络 IPC/RTSP 分支：它是 Host 驱动、与 Camera/Playback 互斥的
独立顶层 Pipeline，经 Linux UVC gadget 节点输出。其协议和 buffer 生命周期见
[`uvc.md`](uvc.md)。

## 1. 模块边界

```text
MPI 数据面                    用户态业务面
---------------------------  ---------------------------------
VI / ISP -> VENC             StreamDispatcher
VI / ISP -> VO（可选预览）      ├-> RtspSink -> RTSP/RTP -> client
VI -> GetFrame（可选 AI）       └-> RecorderSink -> raw/MUX -> storage

DEMUX -> VDEC -> VO          本地文件 Playback；不是实时 camera 编码链的一环
```

RTSP server 不是 MPP 标准模块。`rtsp_open/start/sendData/stop/close` 来自 sample 配套的
私有库，能否链接、SDP 如何生成、RTP 如何分包、客户端如何重连都必须单独核对。

VDEC 也不属于实时 camera→RTSP 主链：camera 输入已经是原始图像，直接送 VENC；只有
播放文件/网络压缩流、转码或 loopback 测试才需要 VDEC。产品不能为了“把模块都串上”
而增加一次无意义的编码再解码。

## 2. sun8iw21 sample 的真实数据流

`sample_smartIPC_demo` 的基础结构是两条可选编码流和一条可选 AI 通路：

```text
                    +-> Main VIPP/VirChn --Bind--> Main VENC --+
Sensor/ISP ---------+                                         |
                    +-> Sub  VIPP/VirChn --Bind--> Sub  VENC --+-> 各自 GetStream worker
                    |                                         |       |
                    +-> NNA/AI input --------------------------+       +-> raw record
                                                                      +-> RTSP server
```

sample 的 main/sub 并不是“固定主码流=某分辨率、子码流=某分辨率”的规范。它们只是两个
配置驱动的 `VI -> VENC` 实例；VIPP、ISP、codec、尺寸、fps、bitrate 和 channel ID 均
来自 sample 配置。

该 sample：

- 使用 `AW_MPI_SYS_Bind(VI,VENC)` 传递输入帧；
- 在每个 VENC 上启动一个 `GetStream` 线程；
- 把编码 pack 复制到连续临时 buffer；
- 识别 H.264/H.265 I/P 帧，在 I 帧前追加参数集；
- 把同一份连续编码帧同步交给 raw recorder 和 RTSP；
- 可选启动 AI service 和 write-back YUV 线程；
- **没有创建 VO，也没有使用 VDEC**；
- sample 自带 `record.c` 写的是分段裸码流 `.raw`，不是 MP4 MUX pipeline。

`sample_smartIPC_demo` 与 `sample_rtsp` 配套的 `rtsp_server.cpp` 实际相同：两者都把
“参数集 + I 帧”整体以 `FRAME_DATA_TYPE_I` 交给 `MediaStream`，没有调用
`FRAME_DATA_TYPE_HEADER`，也没有注册新客户端 callback。因此 sample 能说明编码数据
如何送入私有 RTSP 库，却不能直接证明 SDP 参数集和重连首帧已经正确。

## 3. sample 的启动流程

### 阶段 A：配置和全局环境

```text
解析命令行/config
-> 校验 main/sub/AI 是否启用及其资源 ID
-> MPP_SYS_CONF_S.nAlignWidth
-> AW_MPI_SYS_SetConf
-> AW_MPI_SYS_Init
-> 初始化 sample recorder/storage monitor
```

产品实现还应在 SYS Init 前完成：唯一 camera owner/跨进程锁、配置交叉校验、目标
storage 的安全检查、队列容量计算和 rollback 账本初始化。

### 阶段 B：为每条编码流创建 MPI 资源

```text
构造 VI_ATTR_S 和 VENC_CHN_ATTR_S
-> CreateVipp
-> SetVippAttr
-> ISP_Run
-> EnableVipp
-> CreateVirChn
-> Create VENC
-> SetRcParam / SetFrameRate / 可选高级参数
-> Register VENC callback
-> Bind(VI,VENC)
```

sample 对 main 和 sub 重复上述流程。可复用封装应抽成参数化对象，不复制两份几乎相同
的初始化代码；共享同一 ISP 时还要由上层统一 ISP Run/Stop 所有权。

### 阶段 C：启动数据面和 RTSP

```text
EnableVirChn
-> VENC_StartRecvPic
-> rtsp_open（如果该流启用 RTSP）
-> 创建 GetStream worker
-> worker 内取得 SPS/PPS 或 VPS/SPS/PPS
-> worker 调用 rtsp_start
-> 循环 GetStream -> dispatch -> ReleaseStream
```

`rtsp_open` 与 `rtsp_start` 的分工是私有 RTSP API 的约定，不属于 MPI。更换 RTSP
实现时，应由 `RtspSink` 适配，不修改 VI/VENC owner。

### 阶段 D：可选业务

sample 随后启动 AI service 和 write-back YUV worker。它们是旁路消费者，不应阻塞
VENC GetStream 主循环。AI 输入应来自独立、低分辨率且未绑定的 VI 输出，或由目标
平台明确支持的分流接口；每个 GetFrame 必须及时 Release。

## 4. 一帧编码数据如何走到 RTSP

每个 VENC worker 的核心循环可以抽象为：

```text
GetStream(timeout)
-> 遍历有效 pack 和 Addr0/1/2 片段
-> 根据 codec-specific mDataType 判断帧类型
-> 若为 IDR/I 帧，按下游需要附加参数集
-> 复制/聚合为有明确容量的自有 EncodedFrame
-> 同步或有界扇出给 RecorderSink
-> 同步或有界扇出给 RtspSink
-> ReleaseStream
```

### 4.1 为什么 sample 要复制

`VENC_PACK_S.mpAddr*` 只在 `ReleaseStream` 前有效。RTSP/MUX/磁盘如果异步保存这些指针，
Release 后就是悬空引用。sample 把三个片段复制到 `stream_buf` 后再调用 recorder/RTSP，
体现了“先取得自有数据，再归还 MPP stream”的原则。

产品可采用以下任一明确模型：

| 模型 | 优点 | 风险/要求 |
| --- | --- | --- |
| worker 内同步发送后 Release | 简单、少队列 | 网络/磁盘慢会阻塞 GetStream，导致 VENC buffer 满 |
| bounded copy 后先 Release，再由同一 worker 同步发送 | 不让网络阻塞延长 MPP 借用期；实现仍简单 | sink 阻塞仍会暂停下一次 GetStream；本项目当前 RTSP 路径采用此模型 |
| 复制到有界 frame pool 后异步扇出 | sink 解耦、可独立丢帧 | 有内存复制成本；必须有容量和引用计数 |
| VENC 输出 tunnel 到 MUX | 录像 buffer 由 MPP 管理 | 同一输出能否再 GetStream 给 RTSP 需 ABI 证明 |
| 为录像和 RTSP 建独立 VENC | owner 清晰、参数可独立 | 占用额外 VI/VENC/CMA/带宽，必须板测 |

无论选择哪种模型，都不能使用无界 `std::queue` 或让慢客户端无限积压编码帧。

### 4.2 pack 聚合

sample 只准备一个 `VENC_PACK_S`，再拼接其中的 `mLen0/1/2`。通用实现需要处理：

- `VENC_STREAM_S.mPackCount` 大于一；
- 单个 pack 多个非连续片段；
- 片段总长超过预分配 buffer；
- H.264/H.265/JPEG 使用不同 `mDataType`；
- PTS 回退、重复或不连续；
- 停止请求发生在 Get 成功、dispatch 失败、Release 尚未执行之间。

推荐让 `EncodedFrame` 保存 codec、frame kind、PTS、是否含参数集和完整 payload，sink
不再直接解释 MPP pack 指针。

## 5. 参数集、关键帧和客户端首屏

RTSP 客户端要从可解码边界开始：

```text
VENC codec config
-> GetH264SpsPpsInfo / GetH265SpsPpsInfo
-> RtspSink 生成/配置 SDP
-> client PLAY 或新录像切片请求可解码起点
-> 必要时 RequestIDR
-> 参数集 + IDR
-> 后续 P/B frame
```

应明确以下策略：

- H.264 使用 SPS/PPS；H.265 通常还需要 VPS；
- 参数集放入 SDP、关键帧前，还是两者都放，由 RTSP server 和客户端兼容性决定；
- 新客户端不能直接从任意 P 帧开始；
- `RequestIDR` 是请求，不应假设下一次 GetStream 必然立即返回 IDR；
- 编码器输出是 Annex-B 还是 length-prefixed 必须抓包/检查，不由函数名推断；
- SPS/PPS buffer 的所有权和有效期以当前 API 实现为准，长期保存时复制。

固定版本 TinyServer 的 `MediaStream` 对 frame type 有额外语义：

- `FRAME_DATA_TYPE_HEADER` 会解析 Annex-B header，将 SPS/PPS（H.265 时还包括 VPS）保存
  到 video source，后续创建 RTP sink/SDP 时读取这些参数集；
- `FRAME_DATA_TYPE_I/P` 只把 NAL 送入数据队列，不能替代上面的参数集缓存步骤；
- `setNewClientCallback` 对应客户端 PLAY 边界，可用来通知编码 worker 请求 IDR；callback
  自身仍只应设置原子标志或投递事件，不直接销毁资源。

因此产品适配不能只照抄 sample 的“把 header 拼在 I 帧前”做法。至少应在 server 接受
客户端前用 `FRAME_DATA_TYPE_HEADER` 初始化 SDP 参数集；每次客户端开始 PLAY 后请求新
IDR，并在真正观察到 I 帧前抑制 P 帧。为了兼容正在观看的客户端，I 帧前仍可再次提交
header，但 header 和 frame 应以各自正确的 frame type 交付。

## 6. RTSP 层实际负责什么

sample 暴露的最小接口是：

| API | 业务含义 |
| --- | --- |
| `rtsp_open(id,&attr)` | 创建指定 codec/network/stream 类型的 server/session |
| `rtsp_start(id)` | 开始监听或发送 |
| `rtsp_sendData(id,&frame)` | 交付完整编码帧、帧类型和 PTS |
| `rtsp_stop(id)` | 停止会话/发送 |
| `rtsp_close(id)` | 释放 server 实例 |

MPI 文档只能保证交给 RTSP 的编码帧生命周期正确，不能替 RTSP 实现保证：

- URL 和端口；
- SDP/`sprop-parameter-sets`；
- RTP timestamp 换算和 sequence number；
- MTU 分片、FU-A/FU、聚合包；
- RTCP、超时、鉴权、多客户端和慢客户端隔离；
- TCP interleaved/UDP/multicast；
- H.265 与不同播放器的兼容性。

这些都需要按主维护者指定方式检查 RTSP 库、抓包和客户端行为。

### 6.1 当前 sun8iw21/TinyServer ABI 的已知边界

当前固定参考为 Yuzukilizard commit
`94bb93ad67fd862c5f6fe6c29fbc0f54950e7107`。定向核对
`sample_smartIPC_demo`、`sample_rtsp`、`VideoEnc_Component.c`、RTSP public header 和
`libTinyServer.a` 后，可确认以下源码/ABI 事实，但它们不是目标板结果：

| 边界 | 当前事实 | 产品适配要求 |
| --- | --- | --- |
| VENC pack | sun8iw21 VideoEnc 明确写明 by-frame 输出只支持每帧一个 pack，并只填充 `mpPack[0]`；该 pack 最多有 Addr0/1/2 三段 | 本版本可分配一个 pack，但仍须检查三段地址/长度、总容量并完整复制；不能把“一 pack”外推到其他 SoC/ABI |
| SDP 参数集 | `MediaStream` 仅在 `FRAME_DATA_TYPE_HEADER` 路径缓存 SPS/PPS/VPS | server 开始服务前显式提交 header；I 帧到达时按策略再次提交 |
| 新客户端 | public ABI 提供 `setNewClientCallback` 和 `AW_MPI_VENC_RequestIDR` | callback 只唤醒/置位；worker 请求 IDR 并等待实际 I 帧，不把请求成功等同于已得到 I 帧 |
| 音频关闭 | `MediaStreamAttr::AudioType` 只有 AAC；固定单播实现还会创建 audio subsession | 首版虽不创建 AENC/不送音频，仍不能承诺 SDP 是纯视频；需要替换/修订 server ABI 或板端确认客户端行为 |
| 创建失败 | `TinyServer` 构造路径在底层 `RTSPServer::createNew` 失败时会 `exit(1)` | 调用前预检 interface/address/port 可覆盖常见错误，但关闭预检 socket 到真正创建之间仍有竞态，最终仍需可返回错误的 server 实现 |
| 慢客户端 | `appendVideoData` 背后的队列策略不由 Application 配置或观测 | 先复制并 Release VENC stream，再调用 RTSP sink；阻塞上限、断连和 stop 收敛仍需目标板检查 |

上述结论解释了为什么产品 `RtspOutput` 直接适配 public ABI，而不复用 sample 的全局
`gpRtspContext[]`/`gpRtspStream[]` wrapper。更换 RTSP 实现时，VI/VENC owner、bounded
copy 和 Get/Release 关系保持不变。

## 7. 录像不是“把 RTSP 数据 fwrite”这么简单

sun8iw21 SmartIPC sample 的 recorder：

- 监测 SD 卡；
- 根据 PTS 切分文件；
- 新文件等待 I 帧，必要时调用 `RequestIDR`；
- 写入已聚合的裸 H.264/H.265 数据；
- 使用 `.raw` 文件名。

它不创建 MUX，也不提供 MP4 索引、原子提交或断电一致性。因此只能用于理解“同一编码
流扇出”和“切片从关键帧开始”，不能作为产品录像模块。

容器录像通常使用：

```text
VI --Bind--> VENC --Bind--> MUX -> file descriptor/container
```

并向 MUX 提供 codec header、track 属性、文件切换策略和错误 callback。sun8iw21 使用
MUX group/channel 模型，而 V821 较新指南已经出现移除 MUX group 的接口演进；实现前
必须以当前 sysroot `mpi_mux.h` 为准。

本项目 `camera-mpp-service` 选择独立 RecordOutput 的 VI→VENC→MUX pipeline，不采用
SmartIPC sample 的 raw recorder，详见
[`pipelines.md`](../../components/camera-mpp-service/pipelines.md)。

## 8. 如何加入本地 VO 预览

SmartIPC sample 自身没有 VO。若产品需要边推流边本地预览，应把预览设计为 camera
source 的另一个明确输出：

```text
Camera/ISP
├── preview VirChn --Bind--> VO video layer
└── encode  VirChn --Bind--> VENC --GetStream--> RTSP
```

是否可以由同一个 VIPP 的多个 VirChn 同时承担 Preview/Encode，或必须使用不同 VIPP，
取决于 sun8iw21 驱动、格式和资源预算。不能用 V821/sun252iw1 的多通道数量推断。

VO 只持有 MPP video layer；Qt UI framebuffer/layer 继续由 Qt 持有。不要复制 sample 中
关闭 `HLAY(2,0)` UI layer 的行为。

## 9. VDEC/Playback 与 SmartIPC 的关系

VDEC 用于：

- 播放本地录制文件；
- 接收压缩网络流后解码显示；
- 编解码 loopback 诊断。

它不应出现在普通实时采集推流数据面。产品如果需要“停止预览后回放录像”，状态机是：

```text
Camera + RTSP/Record outputs
-> 全部停止并释放 camera/VENC/VO 绑定
-> Idle
-> DEMUX -> VDEC -> VO Playback
```

而不是让 camera pipeline 和 playback pipeline 默认并行。当前 Application 已通过
ADR-0002 采用单一 MPP owner 和顶层 pipeline 互斥，service 组件文档继续定义其切换。

## 10. 推荐控制流和线程模型

```text
Control thread
├── 唯一执行 Create/Bind/Start/Stop/UnBind/Destroy
├── 维护资源账本和状态机
└── 接收 callback/worker mailbox

VencStreamWorker[chn]
├── 有限超时 GetStream
├── copy/normalize EncodedFrame
├── 送入有界 dispatcher
└── ReleaseStream

RtspSink worker
├── 每客户端/会话背压策略
└── 慢客户端不能阻塞 MPP owner

RecorderSink worker
├── 关键帧边界、MUX/文件提交
└── 磁盘错误只通知控制线程

AI worker（可选）
└── VI GetFrame -> inference -> ReleaseFrame
```

停止推荐顺序：

```text
RTSP 停止接受新会话/录像拒绝新任务
-> 设置全局 stop token，调用 sink 的非销毁式 wake/stop 并唤醒所有队列
-> join AI、RTSP、Recorder、GetStream worker
-> 停止 VENC 和 VO channel
-> Disable VI VirChn
-> 反向 UnBind
-> Destroy VENC/VO/VirChn
-> Disable VIPP / Stop ISP / Destroy VIPP
-> 销毁已停止的 RTSP、storage 和其他用户态 sink
-> AW_MPI_SYS_Exit
```

RTSP 的“停止接受/唤醒”与“销毁实例”可能是同一个 API，也可能必须拆开；它在 join
前后的确切位置取决于库能否安全唤醒阻塞发送，wrapper 必须定义并测试。不能在 signal
handler 或 MPI callback 中直接执行上述停止序列。

## 11. 不应照搬的 sample 行为

| sample 行为 | 产品化处理 |
| --- | --- |
| main/sub 大段重复初始化 | 参数化 `ViSource`/`VencEncoder`，统一 rollback |
| 固定一个 VENC pack | 先核对当前 ABI；sun8iw21 可明确使用一 pack/三片段，其他 ABI 按 `mPackCount` 分配并遍历 |
| `width * height * N` 推算 RTSP buffer | 依据实际编码帧长度做有界增长并设置硬上限 |
| 持有 VENC stream 时同步阻塞网络/磁盘 | 至少先 bounded copy 并 Release；需要进一步隔离时再使用有界 frame pool、独立 sink 和丢帧/断开策略 |
| raw recorder 当录像 | 使用明确的 MUX/container 与原子文件提交 |
| 自动 mount/umount/修复 SD | 交给系统存储策略；应用只验证真实挂载和错误 |
| 销毁前没有显式 UnBind | 保存绑定账本并反向 UnBind |
| 中途 `return -1` | 统一进入幂等 cleanup |
| 固定 VIPP/ISP/VENC/RTSP ID | 从产品配置读取并全局冲突检查 |
| 默认双流/AI 并发 | 只在 V851S 资源和稳定性得到实际确认后启用 |

## 12. 与本项目 service 的最终分工

| 文档 | 负责内容 |
| --- | --- |
| 本页 | 可复用 SmartIPC 数据流、stream 扇出、RTSP 边界和 sample 陷阱 |
| [`core.md`](core.md)、[`vi.md`](vi.md)、[`vo.md`](vo.md)、[`venc.md`](venc.md)、[`vdec.md`](vdec.md) | 各 MPI 模块接口语义和生命周期 |
| [`uvc.md`](uvc.md) | UVC gadget control/event、V4L2 output buffer 和 Host 驱动数据流 |
| service [`configuration.md`](../../components/camera-mpp-service/configuration.md) | 本产品使用的配置 key 和校验 |
| service [`pipelines.md`](../../components/camera-mpp-service/pipelines.md) | 本产品实际创建哪些 channel、怎样 Bind/Start/Stop/rollback |
| service [`known-limitations.md`](../../components/camera-mpp-service/known-limitations.md) | 当前实现边界和未确认风险 |

因此，新增通用 MPI 知识时更新本目录；改变 `camera-mpp-service` 的实际 pipeline 时只
更新 service 组件文档和源码，二者不再相互复制完整调用教程。
