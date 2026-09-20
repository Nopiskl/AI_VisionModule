# VO MPI：Device、Layer 与 Channel

VO（Video Output）不是单一“显示通道”。应用需要分别管理输出设备、DISP2 video layer
和 layer 内的 VO channel；GUI 已经拥有的 outside layer 又是另一类资源。

对应头文件：`mpi_vo.h`、`mm_comm_vo.h`；公共 frame、pixel format 和 callback 类型
来自 `mm_common.h`。

## 1. 三层对象

```text
VO_DEV
  -> VO_LAYER / DISP2 layer handle
       -> VO_CHN / MPP 输入与播放状态
            -> LCD/HDMI/CVBS 等输出
```

| 对象 | 主要配置 | 生命周期 |
| --- | --- | --- |
| `VO_DEV` | interface type、sync/timing、background | `Enable` 到 `Disable` |
| `VO_LAYER` | screen rect、image size、pixel format、display fps | `EnableVideoLayer` 到 `DisableVideoLayer` |
| `VO_CHN` | buffer 数、frame rate、show/hide、pause/resume | `CreateChn` 到 `DestroyChn` |
| outside layer | 由 GUI/framebuffer 创建的已有 layer | MPP 只 `AddOutside`/`RemoveOutside` 登记 |

`VO_LAYER` 常由 `HLAY(display-channel, layer-id)` 一类宏生成，但编码方式和有效索引是
BSP 能力。固定 handle 不能从 sample 复制到产品；当前 Qt/MPP 所有权规则见
[`../display/linuxfb-disp2.md`](../display/linuxfb-disp2.md)。

## 2. 关键属性

### `VO_PUB_ATTR_S`

| 字段 | 含义 |
| --- | --- |
| `mBgColor` | 无有效视频覆盖时的背景色 |
| `enIntfType` | LCD、HDMI、CVBS 等接口类型 |
| `enIntfSync` | PAL/NTSC、标准分辨率时序或 user timing |
| `stSyncInfo` | 自定义输出时序，仅在对应模式下有意义 |

### `VO_VIDEO_LAYER_ATTR_S`

| 字段 | 含义 | 常见错误 |
| --- | --- | --- |
| `stDispRect` | 图层在屏幕上的目标矩形 | 越界、旋转后坐标仍按原屏幕计算 |
| `stImageSize` | 图层 canvas/输入图像尺寸 | 与输入帧尺寸混淆 |
| `mDispFrmRt` | 显示帧率 | 当成输入能力承诺 |
| `enPixFormat` | layer 接收的像素格式 | 与 VI/VDEC 输出格式不一致 |
| `bDoubleFrame` | 双帧相关策略 | 未确认平台语义即开启 |
| `bClusterMode` | 内存 cluster 模式 | 从其他 SoC 示例照搬 |

显示裁剪、源图大小和屏幕目标矩形是不同概念；需要缩放时还要确认该 DISP2 channel/layer
具备 scaler 能力。

## 3. 基础 API 速查

### Device

| API | 作用 | 配对/时机 |
| --- | --- | --- |
| `AW_MPI_VO_Enable(dev)` | 启用 VO device | SYS Init 后；配对 `Disable` |
| `AW_MPI_VO_GetPubAttr` | 读取当前输出属性 | 修改前建议先读，保留未负责字段 |
| `AW_MPI_VO_SetPubAttr` | 设置 interface/sync | device 已启用但业务 channel 未运行时配置 |
| `AW_MPI_VO_GetHdmiHwMode` | 查询 HDMI 模式 | 只对 HDMI 场景有意义 |
| `AW_MPI_VO_Disable(dev)` | 禁用输出设备 | 所有 channel/layer/outside 登记清理后 |

### Layer

| API | 作用 | 配对/时机 |
| --- | --- | --- |
| `AW_MPI_VO_EnableVideoLayer(layer)` | 申请并使能 MPP 管理的 video layer | 配对 `DisableVideoLayer` |
| `AW_MPI_VO_DisableVideoLayer(layer)` | 释放 MPP video layer | channel 已 Destroy、无输入绑定后 |
| `AW_MPI_VO_AddOutsideVideoLayer(layer)` | 登记外部已存在的 GUI layer | 不执行 DISP2 申请；配对 Remove |
| `AW_MPI_VO_RemoveOutsideVideoLayer(layer)` | 移除 outside 登记 | 不等于关闭或销毁 GUI layer |
| `AW_MPI_VO_OpenVideoLayer` / `CloseVideoLayer` | 改变 layer 实际开关状态 | 只能由该 layer 的真实 owner 使用 |
| `AW_MPI_VO_SetVideoLayerAttr` / `GetVideoLayerAttr` | 设置/读取矩形、尺寸、格式 | MPP 自有 video layer 上使用 |
| `AW_MPI_VO_SetVideoLayerPriority` / `GetVideoLayerPriority` | 设置/读取 z-order 相关优先级 | 语义和方向必须板端核对 |
| `AW_MPI_VO_SetVideoLayerAlpha` / `GetVideoLayerAlpha` | 设置/读取 alpha | 像素/全局模式需与格式和 DISP2 一致 |

本项目的稳定边界是：MPP 对 Qt UI outside layer 只允许 Add/Remove，不允许 Open/Close、
Enable/Disable、SetAttr、SetPriority 或 SetAlpha。

### Channel

| API | 作用 | 配对/时机 |
| --- | --- | --- |
| `AW_MPI_VO_CreateChn(layer,chn)` | 创建 VO channel | layer 已使能；配对 Destroy |
| `AW_MPI_VO_RegisterCallback` | 注册渲染/释放等事件 | Create 后、Start 前 |
| `AW_MPI_VO_SetChnDispBufNum` / `GetChnDispBufNum` | 设置/读取显示 buffer 数 | Start 前设置；实际需求需测量 |
| `AW_MPI_VO_SetChnFrameRate` / `GetChnFrameRate` | 设置/读取 channel 帧率 | 不代替显示时序或 source fps |
| `AW_MPI_VO_StartChn` / `StopChn` | 启动/停止接收并显示数据 | Start/Stop 成对 |
| `AW_MPI_VO_PauseChn` / `ResumeChn` | 暂停/恢复处理 | Playback 控制使用 |
| `AW_MPI_VO_ShowChn` / `HideChn` | 改变 channel 可见性 | 不释放资源，也不代替 Stop |
| `AW_MPI_VO_SetStreamEof` | 标记输入流结束 | Playback EOF 传播使用 |
| `AW_MPI_VO_Seek` | 清理/准备跳播状态 | 与 DEMUX/VDEC/CLOCK 协同，不可单独调用完成 seek |
| `AW_MPI_VO_GetChnPts` | 读取当前播放 PTS | 单位在参考文档中为微秒，使用前核对 ABI |
| `AW_MPI_VO_GetDisplaySize` | 查询显示尺寸 | 用于诊断实际 layer/channel 配置 |
| `AW_MPI_VO_SetFrameDisplayRegion` | 调整帧显示区域 | 坐标系和缩放能力需验证 |

## 4. Tunnel 显示

### VI→VO 预览

```text
Enable VO device -> Get/SetPubAttr
-> EnableVideoLayer -> Get/SetVideoLayerAttr
-> CreateChn -> RegisterCallback -> SetChnDispBufNum
-> Create/configure VI
-> Bind(VI, VO)
-> Enable VI data flow
-> StartChn
```

`sample_virvi2vo` 和 `sample_smartPreview_demo` 使用这一模型。产品不应复制 sample 关闭
outside UI layer 的动作；GUI 与视频 layer 需要通过 DISP2 合成而不是互相接管。

### VDEC→VO 播放

```text
DEMUX -> VDEC -> VO
CLOCK ---------> DEMUX/VO
```

VDEC→VO 绑定后，解码帧由组件内部传递并归还，应用不再 `GetImage/ReleaseImage`。EOF、
Pause/Resume、Seek 必须同时协调 DEMUX、VDEC、CLOCK 和 VO；仅操作 VO 不构成完整播放
状态机。

## 5. Non-tunnel 手动送帧

`AW_MPI_VO_SendFrame(layer, chn, &frame, timeout)` 用于应用直接把
`VIDEO_FRAME_INFO_S` 送入未绑定 VO channel。适合测试图、应用自有帧或特定转换链路。

必须先明确 frame 的 owner：

- 如果 frame 来自 `VI_GetFrame` 或 `VDEC_GetImage`，在 VO 已完成使用前不能 Release；
- 可以通过 `MPP_EVENT_RELEASE_VIDEO_BUFFER` 判断手动送入的 buffer 何时可归还，但事件
  数据类型和触发条件必须对照当前实现；
- 不要在 callback 中直接 Release 其他线程仍可能访问的 frame，应通过所有权状态或
  mailbox 串行处理。

普通预览和播放优先使用 Bind，让 MPP 内部完成 buffer 归还。

## 6. Callback 与 EOF

常见 VO 事件：

| 事件 | 意义 | 应用动作 |
| --- | --- | --- |
| `MPP_EVENT_RENDERING_START` | 首帧开始渲染 | 投递“画面已开始”状态，不做销毁 |
| `MPP_EVENT_RELEASE_VIDEO_BUFFER` | 手动送入的帧可释放 | 投递 frame token 给 owner |
| `MPP_EVENT_SET_VIDEO_SIZE` | 输入尺寸变化 | 校验后在控制线程决定是否重配 |
| `MPP_EVENT_NOTIFY_EOF` | 播放输出结束 | 更新状态并触发受控停止/下一媒体 |

事件支持和 `pEventData` 类型需以当前 `mpi_vo.c`/头文件为准。

## 7. 停止与回滚

```text
停止 source/decoder 和手动 SendFrame
-> StopChn
-> 等待全部外部 frame release 事件
-> UnBind(source, VO)
-> DestroyChn
-> DisableVideoLayer
-> RemoveOutsideVideoLayer（若由本对象登记）
-> Disable VO device
```

资源账本至少记录 `deviceEnabled`、`outsideLayerAdded`、`videoLayerEnabled`、
`channelCreated`、`channelStarted`、`bindings` 和尚未归还的外部帧。

## 8. 跨 SoC 限制

V821 指南描述的 layer 数量、channel 类型、缩放能力和 RGB/YUV 分工只能作为 DISP2
模型参考；sun8iw21/V851S 的有效 handle、UI 映射、alpha 和 z-order 必须在当前 BSP
查询并按主维护者指定方式确认。MPP API 调用成功也不代表 Qt overlay 的最终合成关系正确。

产品显示初始化和 layer owner 继续由 service 组件 pipeline 与平台显示契约共同定义；
本页不提供任何产品 layer 默认值。
