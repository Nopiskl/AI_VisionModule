# VDEC MPI：码流输入、解码帧与 EOF

VDEC（Video Decoder）把 H.264、H.265、MJPEG/JPEG 等压缩码流转换为 YUV 帧。它既可
作为 `DEMUX -> VDEC -> VO` tunnel 播放链中的 filter，也可以由应用手动 SendStream、
GetImage 和 ReleaseImage。

对应头文件：`mpi_vdec.h`、`mm_comm_vdec.h`；输出 frame、pixel format 和 callback
类型来自 `mm_common.h`、`mm_comm_video.h`。

## 1. 两种使用方式

```text
Tunnel playback:
DEMUX --Bind--> VDEC --Bind--> VO
应用管理状态和 EOF，MPP 管理数据 buffer

Non-tunnel decode:
应用 --SendStream--> VDEC --GetImage--> 应用处理 --ReleaseImage
```

同一个 VDEC 输入端或输出端只选择一种 owner。VDEC 输入绑定 DEMUX 后不再手动
`SendStream`；VDEC 输出绑定 VO 后不再手动 `GetImage`。

## 2. `VDEC_CHN_ATTR_S` 关键字段

| 字段 | 含义 | 配置注意 |
| --- | --- | --- |
| `mType` | 待解码 payload | 必须来自可靠探测/DEMUX media info，不按扩展名猜测 |
| `mBufSize` | 压缩码流输入 buffer | 过小会频繁满，过大占连续内存；需按流峰值验证 |
| `mPriority` | 通道优先级 | 多通道能力未经板测时不要依赖 |
| `mPicWidth/Height` | 允许的最大图像尺寸 | 不应小于流实际尺寸；不是强制输出缩放尺寸 |
| `mInitRotation` | 初始顺时针旋转 | 能力与格式/尺寸受硬件限制 |
| `mOutputPixelFormat` | 主输出 YUV 格式 | 必须与 VO/应用接收方匹配 |
| `mSubPicEnable` 和子图字段 | MJPEG 等候选双图输出 | 使用 `GetDoubleImage`，能力需核对 |
| codec union 的 mode | `VIDEO_MODE_STREAM` 或 `VIDEO_MODE_FRAME` | 决定输入包边界的解释 |
| `mSupportBFrame` | 视频 B 帧支持候选 | 需要码流探测、资源预算和板测 |
| `mRefFrameNum` | 参考帧数 | 与 codec、level 和内存占用相关 |
| `mnFrameBufferNum` / `mExtraFrameNum` | 解码帧 buffer | 字段适用范围随 codec/版本不同 |

`VIDEO_MODE_FRAME` 要求应用可靠提供完整 access unit，并正确设置 `mbEndOfFrame`；
`VIDEO_MODE_STREAM` 允许按字节流投喂，但仍需保留 codec 参数集和顺序。不能因为文件
读取一次返回一块数据，就假设它天然是一帧。

## 3. 基础 API 速查

### Channel 与状态

| API | 作用 | 状态/配对 |
| --- | --- | --- |
| `AW_MPI_VDEC_CreateChn(chn,&attr)` | 创建独立解码通道 | 配对 Destroy；静态属性在 Start 前确定 |
| `AW_MPI_VDEC_DestroyChn(chn)` | 销毁通道 | Stop、解绑、Release 全部 image 后 |
| `AW_MPI_VDEC_GetChnAttr` | 读取通道属性 | 用于诊断实际 codec/尺寸/格式 |
| `AW_MPI_VDEC_RegisterCallback` | 注册 EOF/尺寸/错误事件 | callback 只投递事件 |
| `AW_MPI_VDEC_StartRecvStream` | 进入接收和解码状态 | Create、配置、绑定完成后 |
| `AW_MPI_VDEC_StartRecvStreamEx` | 启动并限制解码帧数 | 有限帧任务使用 |
| `AW_MPI_VDEC_StopRecvStream` | 停止接收 | Pause/Executing 收敛到可销毁状态 |
| `AW_MPI_VDEC_Pause` / `Resume` | 暂停/恢复解码 | 与 DEMUX/CLOCK/VO 协同使用 |
| `AW_MPI_VDEC_Seek` | 清理并准备跳播后的解码状态 | 不会替你 seek 文件或重置其他组件 |
| `AW_MPI_VDEC_ResetChn` | 清空内部缓存，保留配置资源 | 必须先 Stop；不代替 Destroy |
| `AW_MPI_VDEC_Query` | 查询积压、接收和错误计数 | 用于背压与诊断 |

### 参数和数据

| API | 作用 | 关键点 |
| --- | --- | --- |
| `AW_MPI_VDEC_SetChnParam` / `GetChnParam` | 错误阈值、解码帧类型、输出顺序等 | 静态字段通常在 Stop/Idle 设置 |
| `AW_MPI_VDEC_SetVideoStreamInfo` | 提供更具体的码流信息 | Start 前调用；结构与 ABI 绑定 |
| `AW_MPI_VDEC_ForceFramePackage` | 声明每个输入 packet 是否完整帧 | 只有上游确实提供帧边界时才能开启 |
| `AW_MPI_VDEC_SendStream` | non-tunnel 发送压缩数据 | 成功只说明进入输入 buffer，不等于已显示 |
| `AW_MPI_VDEC_GetImage` | non-tunnel 取得主解码帧 | 成功后必须 `ReleaseImage` |
| `AW_MPI_VDEC_ReleaseImage` | 归还主解码帧 | 使用同一 chn 和原 frame 描述 |
| `AW_MPI_VDEC_GetDoubleImage` | 取得主/子图 | 成功后两者一起 `ReleaseDoubleImage` |
| `AW_MPI_VDEC_SetStreamEof` | 设置/清除输入 EOF | tunnel 播放常由 DEMUX EOF callback 触发 |
| `AW_MPI_VDEC_SetRotate` / `GetRotate` | 设置/读取旋转 | 参考状态要求在解码开始前设置 |
| `AW_MPI_VDEC_ReopenVideoEngine` | 重开解码引擎 | JPEG 分辨率变化等特殊场景；不是通用错误重试 |
| `AW_MPI_VDEC_SetVEFreq` | 设置 VE 频率 | 平台级性能调参，没有测量依据时不调用 |

## 4. `VDEC_STREAM_S` 语义

```c
typedef struct VDEC_STREAM_S {
    unsigned char *pAddr;
    unsigned int mLen;
    uint64_t mPTS;
    BOOL mbEndOfFrame;
    BOOL mbEndOfStream;
} VDEC_STREAM_S;
```

| 字段 | 规则 |
| --- | --- |
| `pAddr/mLen` | 指向本次有效压缩数据；至少保持到 `SendStream` 返回 |
| `mPTS` | 保留上游时间基准；不能对 B 帧简单使用读取顺序代替显示 PTS |
| `mbEndOfFrame` | 只有本包确实结束一帧时设置；frame mode 尤其关键 |
| `mbEndOfStream` | 表示不再有后续输入；与 channel EOF 状态协调使用 |

`SendStream` 的 timeout 控制“等待输入 VBV 有空间”的时长：`0` 立即返回，正数有限等待，
负数阻塞。产品线程推荐有限等待并处理 buffer full，而不是无条件 retry 忙循环。

## 5. Non-tunnel 解码循环

```text
CreateChn -> RegisterCallback -> 可选 SetChnParam/StreamInfo/Rotate
-> StartRecvStream

producer: 解析可靠的 packet/access unit
       -> SendStream(timeout)

consumer: GetImage(timeout)
       -> 同步消费/复制
       -> ReleaseImage

输入结束 -> 标记 EOF -> 排空可用帧/等待 EOF 事件
-> StopRecvStream -> DestroyChn
```

生产和消费若在不同线程，必须用有界队列和共同 stop token。解码输出比输入延后，特别是
存在参考帧或 B 帧时，不能在读到文件末尾后立即 Destroy；要先发送/设置 EOF 并排空。

`sample_vdec` 展示了 H.264/H.265 裸流按外部长度文件投喂以及 JPEG 单图解码，但它的
紧循环 retry 和固定 buffer/最大尺寸只是测试代码，不应直接进入服务。

## 6. Tunnel 播放和 EOF 传播

典型绑定图：

```text
DEMUX --video--> VDEC --decoded frame--> VO
  ^                                      ^
  +---------------- CLOCK --------------+
```

启动前两端已 Create 并 Bind，然后协调 Start CLOCK、VDEC、VO、DEMUX。当 DEMUX 报告
视频 EOF 时，在控制路径对 VDEC 调用 `SetStreamEof(TRUE)`；VDEC 完成排空并报告 EOF 后，
再对 VO 设置 EOF。最终以 VO/整体状态机的完成事件判断播放结束，而不是以“文件读完”
判断已经显示完。

Seek 的概念流程：

```text
Pause/Stop 数据流
-> DEMUX seek
-> 清除 VDEC/VO EOF
-> VDEC Seek + VO Seek（按当前 ABI）
-> 同步 CLOCK 基准
-> Resume/Start
```

这只是跨模块参考顺序；产品实际顺序由 service 组件 PlaybackPipeline 定义并测试。

## 7. 输出帧所有权

`GetImage` 返回的 `VIDEO_FRAME_INFO_S` 只在 Release 前有效。处理 YUV 时必须使用结构中的
真实 plane 地址、stride、宽高、pixel format 和 crop；不能默认所有输出都是连续 NV12。

如果把帧交给 VO、G2D、AI 或其他异步消费者，需满足其一：

- 使用 MPP Bind 让组件内部管理归还；
- 等待所有消费者完成后再 Release；
- 复制到应用自有 buffer，并立即 Release 原 VDEC frame。

浅拷贝结构体不等于复制像素数据。

## 8. 错误与恢复

| 现象 | 可能原因 | 推荐动作 |
| --- | --- | --- |
| `NOBUF` / Send 超时 | 输入 VBV 满、消费/显示停滞 | 有界等待，检查下游和未 Release image |
| 长时间无输出 | codec/packet 边界错误、缺参数集、尺寸门禁错误 | Query 状态、错误计数和首个 SPS/PPS/VPS |
| 绿屏/花屏 | pixel format/stride、参考帧或丢包问题 | 核对输出格式和码流完整性 |
| EOF 不结束 | 未传播 EOF、B 帧/缓存未排空、VO 未收到 EOF | 按 DEMUX→VDEC→VO 顺序检查事件 |
| Seek 后旧画面 | 只 seek DEMUX，未清 VDEC/VO/CLOCK | 执行完整跨模块 seek 状态机 |

失败 cleanup 先停止 producer/consumer 并归还 image，再 Stop、UnBind、Destroy。callback
中的 EOF/错误只进入 mailbox，由控制线程决定恢复或停止。

## 9. 版本与产品边界

V821 文档和 sun252iw1 资料可以帮助理解 VDEC 状态和 rt_media 封装，但 codec 能力、
最大尺寸、缩放/旋转、B 帧、frame buffer 数和多通道并发均不能外推到 V851S。当前产品
Playback 的门禁、实际链路和未确认组合见 service 组件的 configuration、pipeline 与
known-limitations。
