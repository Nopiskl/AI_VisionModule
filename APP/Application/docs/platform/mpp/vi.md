# VI MPI：VIPP、VirChn 与手动取帧

VI（Video Input）负责从 sensor/CSI 经过 ISP、VIPP 取得视频帧。对应用而言，最重要的
不是把 `VI_DEV` 简单等同于 `/dev/videoN`，而是区分 VIPP 物理处理通路、VIPP 上的
VirChn 输出端点以及关联的 ISP。

对应头文件：`mpi_vi.h`、`mm_comm_vi.h`；需要控制 ISP 时再使用 `mpi_isp.h`。

## 1. 对象与数据路径

```text
Sensor -> CSI/MIPI -> ISP -> VIPP (VI_DEV) -> VirChn (VI_CHN)
                                                |       |
                                                |       +-> non-tunnel GetFrame
                                                +----------> tunnel Bind 到 VO/VENC
```

| 对象 | 作用 | 生命周期 owner |
| --- | --- | --- |
| ISP device | 3A、去噪和图像处理 | 与使用它的 camera pipeline 协同 Run/Stop |
| VIPP / `VI_DEV` | 配置采集格式、尺寸、fps、buffer、crop 等 | `CreateVipp` 到 `DestroyVipp` |
| VirChn / `VI_CHN` | 可绑定或手动取帧的 VI 输出端点 | `CreateVirChn` 到 `DestroyVirChn` |
| `VIDEO_FRAME_INFO_S` | 一次 Get 得到的借用帧 | `GetFrame` 成功到 `ReleaseFrame` |

多个 VIPP、ISP、VirChn 的数量和映射随 SoC/BSP 变化。V821 文档中的 timeslot、VIPP
编号和 online 能力不能用于证明 V851S；实际可用组合必须查目标 sysroot、DTS/驱动和
板测结果。

## 2. `VI_ATTR_S` 的主要字段

sun8iw21 的 `VI_ATTR_S` 直接包含 V4L2 类型，常用字段如下：

| 字段 | 含义 | 配置时必须核对 |
| --- | --- | --- |
| `type` | V4L2 buffer type | sample 常用 multi-planar capture；以 driver 支持为准 |
| `memtype` | V4L2 memory mode | MMAP/其他模式必须与驱动一致 |
| `format.pixelformat` | 内核像素格式 | 需由 MPP `PIXEL_FORMAT_E` 正确映射，不能混写枚举 |
| `format.width/height` | VIPP 输出尺寸 | 与 sensor/ISP window、下游输入能力和对齐要求一致 |
| `format.field` | 帧/场方式 | 逐行 camera 通常使用 frame/none，仍以输入为准 |
| `format.colorspace` | 色彩空间 | 需要传递给 VENC/VO，避免量程和矩阵不一致 |
| `fps` | VI 源帧率 | 是候选配置，不自动保证 sensor 或 VE 可达 |
| `nbufs` / `nplanes` | buffer 数量和平面数 | 过小易饥饿，过大占 CMA；由目标板测量决定 |
| `capturemode` | 捕获模式 | sample 常用视频模式，不要硬编码未知模式号 |
| `use_current_win` | 是否复用当前 ISP window | 多通路共享 ISP 时尤其需要理解 |
| `wdr_mode` | WDR 模式 | 必须与 sensor、ISP tuning 和编码配置一致 |
| `drop_frame_num` | 启动后丢弃帧数 | 用于等待图像稳定，不应复制 sample 常量 |
| `mOnlineEnable` | VI→VE online 候选开关 | 必须与 VENC 对应字段成对，并经 SoC 约束核对 |
| `mOnlineShareBufNum` | online 共享 buffer 数 | 仅 online 有意义，取值能力依平台而定 |
| `mCropCfg` | VIPP crop | 坐标和尺寸必须在源图像内并满足格式对齐 |
| `mbEncppEnable` | VI 侧 Encpp 协作开关 | 与 VENC Encpp 配置一致；不可只开一端 |

`ViVirChnAttrS` 主要控制 idle 状态是否收帧、缓存帧数量以及保留旧帧/新帧策略。传
`NULL` 表示采用平台默认虚通道属性；需要缓存语义时应显式配置并给出队列上限。

## 3. 基础 API 速查

### VIPP 管理

| API | 作用 | 正确时机/配对 |
| --- | --- | --- |
| `AW_MPI_VI_CreateVipp(dev)` | 创建 VIPP | SYS Init 后；配对 `DestroyVipp` |
| `AW_MPI_VI_SetVippAttr(dev,&attr)` | 设置采集属性 | Create 后、Enable 前设置静态属性 |
| `AW_MPI_VI_GetVippAttr(dev,&attr)` | 读取当前属性 | 用于核对实际配置 |
| `AW_MPI_VI_EnableVipp(dev)` | 启动 VIPP 采集 | 属性、ISP 和必要 VirChn 已准备后 |
| `AW_MPI_VI_DisableVipp(dev)` | 停止 VIPP | 所有 VirChn 已停止，DestroyVipp 前 |
| `AW_MPI_VI_DestroyVipp(dev)` | 销毁 VIPP | Disable、解绑、销毁 VirChn 后 |

### VirChn 管理

| API | 作用 | 正确时机/配对 |
| --- | --- | --- |
| `AW_MPI_VI_CreateVirChn(dev,chn,attr)` | 创建 VI 输出端点 | VIPP 已创建；配对 `DestroyVirChn` |
| `AW_MPI_VI_SetVirChnAttr` / `GetVirChnAttr` | 设置/读取缓存策略 | 通常在 EnableVirChn 前配置 |
| `AW_MPI_VI_EnableVirChn(dev,chn)` | 允许该端点产出帧 | VIPP 已 Enable；绑定链路应已建立 |
| `AW_MPI_VI_DisableVirChn(dev,chn)` | 停止该端点 | worker 停止取帧后，解绑/销毁前 |
| `AW_MPI_VI_DestroyVirChn(dev,chn)` | 销毁输出端点 | Disable 且不存在绑定/借用帧后 |

### 帧、callback 与辅助配置

| API | 作用 | 注意 |
| --- | --- | --- |
| `AW_MPI_VI_GetFrame(dev,chn,&frame,timeout)` | non-tunnel 获取 YUV/LBC 帧 | 只用于未绑定的 VirChn；成功后必须 Release |
| `AW_MPI_VI_ReleaseFrame(dev,chn,&frame)` | 归还 VI buffer | 使用原始的 dev/chn/frame 配对 |
| `AW_MPI_VI_RegisterCallback(dev,&cb)` | 注册 VI 事件 | callback 不销毁 pipeline；关注 VI timeout |
| `AW_MPI_VI_GetIspDev(dev,&isp)` | 查询 VIPP 对应 ISP | 比复制 sample 的 ISP ID 更可靠 |
| `AW_MPI_VI_SetCrop` / `GetCrop` | 设置/读取 VIPP crop | 静态/动态能力以当前 BSP 为准 |
| `AW_MPI_VI_SetVippMirror` / `AW_MPI_VI_SetVippFlip` | VIPP/sensor 镜像翻转 | 与下游 VENC rotate/mirror 不重复叠加 |
| `AW_MPI_VI_SetShutterTime` | 设置曝光时间策略 | 属于成像参数，需结合 ISP/sensor 调优 |
| `AW_MPI_VI_SetVIFreq` | 设置 VI 频率 | 高风险平台调参；无测量依据时不调用 |

sun8iw21 还声明了 `AW_MPI_VI_SetVippShutterTime`，较新 V821 指南已把它标为即将废弃并
推荐 `AW_MPI_VI_SetShutterTime`。这是跨版本演进提示；实际替换前仍需核对当前库。

## 4. ISP 的最小生命周期

sun8iw21 sample 的 camera 流程通常使用：

```text
AW_MPI_VI_CreateVipp
-> AW_MPI_VI_SetVippAttr
-> AW_MPI_ISP_Run(isp)
-> ...开始 VI 数据流...

...全部 VI 数据流停止...
-> AW_MPI_ISP_Stop(isp)
-> AW_MPI_VI_DestroyVipp
```

如果多个 VIPP 共享同一个 ISP，不能让每个局部对象独立 Stop ISP。应由更高一层记录
ISP 引用/所有权，最后一个使用者退出后再 Stop。曝光、白平衡、降噪等 ISP 参数与 sensor
和 tuning 文件绑定，不属于通用 VI 默认值。

## 5. Tunnel：VI 绑定到 VO 或 VENC

推荐部分顺序：

```text
CreateVipp -> SetVippAttr -> Run ISP
-> CreateVirChn
-> 创建并配置下游 VO/VENC
-> Bind(VI, downstream)
-> EnableVipp
-> EnableVirChn
-> Start downstream channel
```

不同 sample 在 `CreateVirChn` 与 `EnableVipp` 的先后上有差异，因此封装代码应遵守真正
依赖关系，而不是依赖单一 sample 的偶然顺序：VIPP 必须先 Create/SetAttr；VirChn 必须
先 Create 再 Enable；Bind 两端必须存在；开始产出前 ISP/VIPP 必须就绪。

停止的安全原则：

```text
停止下游消费或使 worker 退出
-> DisableVirChn
-> UnBind
-> DestroyVirChn
-> DisableVipp
-> Stop ISP
-> DestroyVipp
```

具体 VO/VENC Stop 时机见对应模块文档。每一步只在其正向操作成功后执行。

## 6. Non-tunnel：手动获取 VI 帧

```text
Create/Set/Run/CreateVirChn/EnableVipp/EnableVirChn
-> GetFrame(timeout)
-> 在当前线程同步处理，或复制到自有有界 buffer
-> ReleaseFrame
-> 循环
```

典型用途：

- JPEG 抓拍：`VI GetFrame -> VENC SendFrame -> VENC GetStream`；
- AI 输入：取低分辨率独立 VirChn 的帧，完成推理后立即 Release；
- 调试：有限帧数保存 YUV，用于格式和 stride 核对。

不要把 `VIDEO_FRAME_INFO_S` 结构体的浅拷贝当成像素数据副本；其中地址仍指向 VI 管理
的 buffer。跨线程排队若只复制结构体，会在 Release 后形成悬空引用。

## 7. 格式和 stride 检查

VI 到下游至少要保持以下关系：

```text
V4L2 pixelformat <-> PIXEL_FORMAT_E
有效 width/height <= buffer/canvas width/height
plane count/stride 与格式一致
colorspace/range 在 VI、VENC、VO 间一致
crop 起点与尺寸满足色度采样和硬件对齐
```

不要用 `width * height * 3 / 2` 直接推断所有帧的内存布局；多平面、stride、LBC、裁剪
和高度对齐都会改变真实地址跨度。读取 `VIDEO_FRAME_INFO_S` 的 plane 地址和 stride，
必要时以板端 dump 验证。

## 8. 失败回滚清单

VI wrapper 建议记录：

```text
vippCreated
callbackRegistered
ispRunning
virChnCreated[]
vippEnabled
virChnEnabled[]
borrowedFrames[]
bindings[]
```

先释放 borrowed frame、停止取帧线程和 VirChn，再处理 Bind、VIPP、ISP。不能因为
`CreateVirChn` 失败就直接 `DestroyVipp`，而忽略之前可能已创建的其他 VirChn。

## 9. 参考提炼

- `sample_virvi2vo`、`sample_smartPreview_demo`：VI→VO tunnel 和回滚顺序；
- `sample_virvi2venc`、`sample_smartIPC_demo`：VI→VENC tunnel、online/Encpp 配置耦合；
- `sample_takePicture`：VI Get/Release 与 JPEG VENC 手动输入；
- V821 指南：VIPP/VirChn 模型以及绑定与非绑定互斥说明，仅作跨 SoC 语义补充。

组合成 IPC/RTSP 场景时继续读 [`smart-ipc.md`](smart-ipc.md)；产品实际 channel 和配置
读取 service 组件文档，不从本页取默认值。
