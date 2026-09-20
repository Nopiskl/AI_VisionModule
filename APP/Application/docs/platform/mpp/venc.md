# VENC MPI：编码通道与码流所有权

VENC（Video Encoder）把 YUV/LBC 视频帧编码为 H.264、H.265、MJPEG/JPEG 等码流。
它的输入端和输出端可以分别选择 tunnel 或 non-tunnel，因此“VI 已绑定 VENC”并不妨碍
应用从 VENC 输出端调用 `GetStream`；真正需要避免的是同一个端口同时采用两套 owner。

对应头文件：`mpi_venc.h`、`mm_comm_venc.h`、`mm_comm_rc.h`；输入 frame 和公共枚举
还依赖 `mm_common.h`、`mm_comm_video.h`。

## 1. 四种常见连接方式

```text
1. VI --Bind--> VENC --Bind--> MUX
2. VI --Bind--> VENC --GetStream--> RTSP/裸流文件
3. app --SendFrame--> VENC --GetStream--> JPEG/离线编码
4. app --SendFrame--> VENC --Bind--> MUX（少见，需核对具体 ABI）
```

| 输入端 | 输出端 | 应用负责的 buffer |
| --- | --- | --- |
| VI tunnel | MUX tunnel | 不直接持有 VI frame 或 VENC stream |
| VI tunnel | non-tunnel | 每次成功 GetStream 后必须 ReleaseStream |
| non-tunnel | non-tunnel | 管理输入 frame 生命周期，并成对 Get/Release stream |

不要未经证明就在同一个 VENC 输出端同时 Bind MUX 又 GetStream。需要录像和 RTSP 时，
可以选择“一个 GetStream worker 复制后扇出”，或创建独立 VENC 输出；具体选择取决于
目标 ABI、码率/内存预算和产品架构。

## 2. 创建属性的组成

sun8iw21 的 `VENC_CHN_ATTR_S` 由五组属性组成：

```text
VENC_CHN_ATTR_S
├── VeAttr       codec、输入/输出尺寸、格式、旋转、online
├── RcAttr       RC 模式及 CBR/VBR/FIXQP/ABR/QPMAP 对应 union
├── GopAttr      GOP 模式与 GOP size
├── GdcAttr      几何校正
└── EncppAttr    Encpp/sharp 协作
```

### `VeAttr` 必查字段

| 字段 | 含义 | 约束 |
| --- | --- | --- |
| `Type` | `PT_H264`、`PT_H265`、`PT_MJPEG`、`PT_JPEG` 等 | 决定 codec union、RC union 和码流类型解释 |
| `SrcPicWidth/Height` | 输入帧尺寸 | 必须匹配 VI/SendFrame 的有效图像和 stride |
| codec union 中的 `PicWidth/Height` | 编码输出尺寸 | 字段名称在 H.264/H.265/JPEG union 中不同 |
| codec union 中的 `BufSize/mBufSize` | VBV/输出 buffer 大小 | 不复制 sample 公式；目标码率、GOP、峰值帧需实测 |
| codec union 中的 `bByFrame/mbByFrame` | 按帧/切片取流 | RTSP/录像通常更容易按帧管理 |
| `MaxKeyInterval` | 关键帧间隔候选 | 与 GOP、客户端首屏和码率波动共同设计 |
| `PixelFormat` | 输入像素格式 | 与 VI 输出或手动 frame 一致 |
| `mColorSpace` | 输入色彩空间 | 与 VI/显示链路保持一致 |
| `Rotate` | 编码旋转 | online/LBC 能力随 SoC 变化 |
| `mOnlineEnable` / `mOnlineShareBufNum` | VI→VE online | VI 和 VENC 两端配置必须一致，且受硬件拓扑限制 |
| `mVeRefFrameLbcMode` | 编码参考帧压缩 | 属于内存/质量权衡，需目标板验证 |
| `EncppAttr` | 编码锐化协作 | VI `mbEncppEnable`、ISP 联动策略需一致 |

### RC 与 GOP

`RcAttr.mRcMode` 决定 union 中哪个成员有效。sun8iw21 头文件分别定义了 H.264/H.265/
MJPEG/MPEG4 的 CBR、VBR、ABR、FIXQP 等模式；至少配置对应模式真正使用的 bitrate、
frame rate、GOP 或 QP 字段，不能设置 `mRcMode` 后继续填写另一个 union 成员。

`GopAttr.enGopMode` 可选择 NormalP、DualP、SmartP、BipredB 等候选模式。模式能力、B 帧、
参考帧数和下游 Playback/RTSP 兼容性必须共同验证。本项目未板测前不把 sample 的 QP、
GOP、VBV 公式或产品模式写成默认值。

`AW_MPI_VENC_SetRcParam` 是创建后的高级码控参数接口。当前 SDK 的产品模式使用
`VENC_CHN_ATTR_S.RcAttr.mProductMode`；旧 sample 中 `product_mode/sensor_type` 字段
不能直接照搬。CBR 的源/目标帧率与 GOP 同样必须按当前 `mm_comm_rc.h` 填写。

## 3. 基础 API 速查

### Channel 生命周期

| API | 作用 | 状态/配对 |
| --- | --- | --- |
| `AW_MPI_VENC_CreateChn(chn,&attr)` | 创建编码通道并进入可配置状态 | 配对 `DestroyChn`；静态属性在这里确定 |
| `AW_MPI_VENC_DestroyChn(chn)` | 销毁通道 | 先 Stop、解绑并释放全部 stream |
| `AW_MPI_VENC_ResetChn(chn)` | 清空编码状态/缓存 | 只在停止接收后使用；不代替 Destroy |
| `AW_MPI_VENC_RegisterCallback` | 注册事件 | 通知事件投递到控制路径；ISP2VE 参数请求须同步回填 |
| `AW_MPI_VENC_SetChnAttr` / `GetChnAttr` | 设置动态属性/读取属性 | 只有当前 ABI 标为动态的字段才允许运行中修改 |
| `AW_MPI_VENC_StartRecvPic` | 开始持续编码 | Create、输入准备和必要 Bind 后 |
| `AW_MPI_VENC_StartRecvPicEx` | 开始并限制接收帧数 | 用于有限帧任务；配合 Query/事件判断完成 |
| `AW_MPI_VENC_StopRecvPic` | 停止接收/编码 | worker 退出前后顺序需保证 GetStream 可收敛 |
| `AW_MPI_VENC_Query` | 查询剩余输入/输出和 pack 状态 | 用于诊断积压，不应忙轮询 |

### Non-tunnel 数据面

| API | 作用 | 所有权 |
| --- | --- | --- |
| `AW_MPI_VENC_SendFrame(chn,&frame,timeout)` | 手动输入一个待编码 frame | 调用返回与 buffer 可复用关系需核对 callback/实现 |
| `AW_MPI_VENC_GetStream(chn,&stream,timeout)` | 获取编码输出 | 成功后应用临时借用 pack 数据 |
| `AW_MPI_VENC_ReleaseStream(chn,&stream)` | 归还编码输出 | 所有同步消费/复制完成后立即调用 |
| `AW_MPI_VENC_GetHandle(chn)` | 获取可等待句柄 | 可与 SYS handle select 配合，但仍需停止唤醒 |

### 常用编码控制

| API | 用途 | 关键点 |
| --- | --- | --- |
| `AW_MPI_VENC_SetFrameRate` / `GetFrameRate` | 源/目标帧率 | `SrcFrmRate` 和 `DstFrmRate` 不等同于实际达到值 |
| `AW_MPI_VENC_SetRcParam` / `GetRcParam` | 高级码控参数 | 结构字段版本差异大，严格按当前头文件填写 |
| `AW_MPI_VENC_RequestIDR(chn,instant)` | 请求关键帧 | 新 RTSP 客户端、切片起录或恢复后常用 |
| `AW_MPI_VENC_GetH264SpsPpsInfo` | 取得 H.264 SPS/PPS | SDP、MUX 初始化或 IDR 前置 header 使用 |
| `AW_MPI_VENC_GetH265SpsPpsInfo` | 取得 H.265 VPS/SPS/PPS 类 header | 不能只按函数名假设没有 VPS |
| `AW_MPI_VENC_SetJpegParam` / `GetJpegParam` | JPEG quality 等 | `PT_JPEG` 通道使用 |
| `AW_MPI_VENC_SetJpegExifInfo` / `GetJpegExifInfo` | JPEG EXIF | 输入字符串和 buffer 生命周期需明确 |
| `AW_MPI_VENC_GetJpegThumbBuffer` | 获取 JPEG thumbnail | 在编码完成后的有效窗口内读取 |
| `AW_MPI_VENC_SetCrop` / `GetCrop` | 编码前裁剪 | online/LBC 支持随 SoC 变化 |
| `AW_MPI_VENC_SetRoiCfg` / `GetRoiCfg` | ROI 编码 | 坐标映射到编码输出空间，区域数/对齐有限制 |
| `AW_MPI_VENC_SetSuperFrameCfg` | 超大帧处理 | 丢弃/重编码会影响时延与质量 |
| `AW_MPI_VENC_SetIntraRefresh` | P 帧帧内刷新 | 与 GOP/客户端解码兼容性共同验证 |

2D/3D filter、SmartP、motion search、write-back YUV、VE 频率和 ISP↔VE 联动属于高级接口。
头文件存在不等于当前产品应启用；需要单独的效果、资源和状态验证。

## 4. `VENC_STREAM_S` 不能只看一个指针

```text
VENC_STREAM_S
├── mpPack[0 .. mPackCount-1]
│    ├── mpAddr0/mLen0
│    ├── mpAddr1/mLen1
│    ├── mpAddr2/mLen2
│    ├── mPTS
│    ├── mbFrameEnd
│    ├── mDataType
│    └── mPackInfo[]
└── codec-specific stream info
```

处理一帧编码流时必须：

1. 先通过 `AW_MPI_VENC_Query` 读取 `VENC_CHN_STAT_S.mCurPacks`，或按 wrapper 规定的硬
   上限准备 `VENC_PACK_S` 数组，并把容量写入 `mPackCount`；
2. 遍历有效 pack，而不是永远假设只有 `mpPack[0]`；
3. 对每个 pack 处理所有非空的 `Addr0/1/2 + Len0/1/2`；
4. 使用对应 codec 的 `mDataType` 判断 NAL/关键帧，不能把 H.264 enum 解释为 H.265；
5. 保留原始 PTS；多 sink 扇出时不各自重造时间戳；
6. 在 `ReleaseStream` 前完成同步使用或复制，之后不保留任何 pack 地址。

`GetStream` 返回后应把 `mPackCount` 当作实际 pack 数再次校验，不能超过调用方提供的
数组容量。sun8iw21 `sample_smartIPC_demo` 固定准备一个 pack，并把三个片段复制到临时连续 buffer。
这是该 sample 的简化假设，不应提升为通用上限。实现 wrapper 时应显式验证
`mPackCount`、片段总长和目标 buffer 容量，并对溢出采取丢帧/断开等确定策略。

## 5. VI→VENC 绑定编码

```text
Create/configure VI VIPP + VirChn
-> CreateChn(VENC) + SetRcParam/SetFrameRate + RegisterCallback
-> Bind(VI, VENC)
-> EnableVipp + EnableVirChn
-> StartRecvPic
-> GetStream/ReleaseStream 循环，或让 VENC 输出 Bind 到 MUX
```

停止：

```text
停止接收新业务请求
-> 唤醒并 join GetStream worker
-> StopRecvPic
-> DisableVirChn
-> UnBind(VI,VENC)
-> DestroyChn(VENC)
-> Destroy VI 端点
```

部分 sample 没有显式 UnBind 就 Destroy。产品封装仍应保存绑定账本并显式反向 UnBind，
不能依赖 Destroy 的隐式副作用。

## 6. 手动输入：JPEG 抓拍

```text
Create JPEG VENC
-> RegisterCallback
-> SetJpegParam / 可选 SetJpegExifInfo
-> StartRecvPic
-> VI_GetFrame
-> VENC_SendFrame
-> VENC_GetStream
-> 复制/写入全部 pack 片段
-> VENC_ReleaseStream
-> VI_ReleaseFrame
-> StopRecvPic -> DestroyChn
```

`sample_takePicture` 的 release-buffer callback 表明输入 frame 的消费可能包含异步阶段。
若当前实现不能证明 `SendFrame` 返回即可释放输入，必须等待对应 release 事件或使用
明确的同步接口。停止路径既要处理“已取到 VI frame 但未送入”，也要处理“已取到
VENC stream 但文件写入失败”。

## 7. SPS/PPS、IDR 与 RTSP

H.264/H.265 RTSP 至少需要：

- 将 codec 和参数集提供给 SDP/RTSP server；
- 新客户端开始播放或录像切片开始时从可解码边界起步；
- 必要时 `RequestIDR`，并在关键帧前发送相应 SPS/PPS 或 VPS/SPS/PPS；
- 保持 Annex-B/AVCC/HVCC 等格式与 RTSP/MUX 接收方预期一致。

`GetH264SpsPpsInfo` 返回的 header 和 `VENC_PACK_S` 是否已包含参数集需要以当前编码器
设置与抓包为准。不能盲目每帧重复 header，也不能假设关键帧一定自带完整参数集。
完整扇出流程见 [`smart-ipc.md`](smart-ipc.md)。

## 8. Callback 和异常

常见 VENC 事件包括：

- `MPP_EVENT_RELEASE_VIDEO_BUFFER`：手动输入帧可归还；
- `MPP_EVENT_VENC_TIMEOUT`：编码超时；
- `MPP_EVENT_VENC_BUFFER_FULL` / `MPP_EVENT_ERROR_ENCBUFFER_OVERFLOW`：输出拥塞；
- ISP→VE 参数请求：当前 SDK 要求在回调返回前同步填写 eventData；不可把这个临时指针投递到队列。

callback 不写文件、不发送 RTSP、不 Stop/Destroy。发生 buffer full 时首先检查
GetStream worker 是否及时 Release、sink 是否阻塞以及队列是否无界，再考虑扩大 buffer。

## 9. 版本和能力边界

- `PT_*`、RC union 字段、product mode、Encpp、online 和高级接口在 SoC 间变化明显；
- V821 指南中的“在线编码限制”仅作风险清单，不是 V851S 能力表；
- sun252iw1 的 `AWVideoInput_*` 是更高层 rt_media 包装，不是本页的 MPI ABI；
- 任何分辨率、fps、bitrate、B 帧、并发 VENC 数和 VBV 大小都需要 V851S 板测。

产品实际使用哪些 VENC 属性以及如何回滚，仍以 service 组件 pipeline/configuration 为准。

## 当前 SDK 的 Encpp 参数交换

当前 SDK 的 vencoder 在每帧编码前以清零的 VencIsp2VeParam 调用上层回调，
随后立即使用回填的锐化配置与 AE 状态。因此只设置 mbEncppEnable 而不处理
MPP_EVENT_LINKAGE_ISP2VE_PARAM 会让编码器接收到零参数。服务用 VencIspLink
查询实际 VIPP 对应 ISP，复制当前锐化/AE 状态，并同步 Encpp 开关；这属于同步
参数交换，不在回调内停止或销毁组件。错误只通知控制路径执行回收。
没有启用 VE→ISP 反向调参，避免 Record/RTSP 多编码通道共同修改传感器策略。
