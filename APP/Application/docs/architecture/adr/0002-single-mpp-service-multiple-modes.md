# ADR-0002：单一 MPP 服务和多模式 Pipeline

- 状态：Accepted
- 日期：2026-09-11
- 修订：2026-09-11，UVC Out 延期；当前范围收敛为 Camera/Playback
- 修订：2026-09-12，确认 RTSP 为 Camera 的可选单路输出
- 修订：2026-09-13，移除易变参数和实现类，保持原决策不变
- 修订：2026-09-14，按产品决定恢复 UVC Out；加入第三个互斥顶层 Pipeline

## 背景

MPP 当前需要提供本地 Camera Preview、Qt 按钮触发的拍照/录像、视频 Playback 和
UVC Out，并需要继续支持 RTSP。vendor sample 为每种链路提供独立 `main()`，产品需要
统一设备所有权、模式切换和错误恢复。

## 决策

只创建一个产品级 `camera-mpp-service`。其顶层模式为：

```text
CameraPipeline
PlaybackPipeline
UvcPipeline
```

三个顶层 Pipeline 严格互斥。UvcPipeline 在同一服务中打开预配置的 UVC gadget video
节点，按 Host 事件按需创建 ISP/VI/VENC，不以独立 sample 进程运行。
Camera 内的 Preview、Snapshot、Record 和 RTSP 是输出或瞬时操作，
不产生新的产品进程。

UVC 模式不创建 VO：Qt UI 继续持有 framebuffer layer，只显示控制和 Host 状态。USB
configfs descriptor、UDC 绑定和 ADB composite function 不由媒体服务管理；它们是部署
前置条件，ADB 共存不在当前决策范围。

RTSP 作为 Camera 会话中的可选输出，由同一 Camera owner 管理；它不得通过独立产品
进程或第二个 sensor owner 绕过本决策。具体 VI/VENC 组合、网络 sink、参数和并发
能力由组件设计、配置及 V851S 验证证据决定。

## 模式资源

| 模式 | 主要资源 |
| --- | --- |
| Camera | MPP_SYS、采集、显示，可选 Snapshot/Record/RTSP 输出 |
| Playback | MPP_SYS、解封装、解码、时钟和显示 |
| UVC Out | UVC gadget fd/mmap buffer；Host STREAMON 期间按协商格式持有 ISP/VI，可选 VENC 和有界复制队列；不持有 VO |

模式切换采用完整停止、逆序释放、资源归零和重新创建。实现必须记录实际获得的资源，
并提供失败回滚和幂等 stop；具体模块和调用顺序见
[`../runtime-model.md`](../runtime-model.md) 与
[`../../components/camera-mpp-service/pipelines.md`](../../components/camera-mpp-service/pipelines.md)。

## 原因

- Sensor、ISP、VI、VO、VE 等硬件并非独立 sample 可随意并发使用；
- Camera Preview 与 Record 是共享采集会话上的输出，不应重复创建 owner；
- Camera 与 Playback 共享显示资源，必须完整切换；
- 一个状态机可以给 GUI 提供一致的 ready、error、stopped 和进度事件。

## 后果

- 正式代码必须从 sample 提取模块，而不是拼接多个 `main()`；
- UVC 需要独立的 Gadget 平台适配，但媒体业务留在同一 MPP 服务；
- Playback 会给 standalone bundle 增加 DEMUX/VDEC/CLOCK 依赖；
- Qt 只提供 Playback 控制，不增加 Qt Multimedia 软件解码后端；
- Camera/Playback/UVC 同时运行不属于当前阶段范围。

媒体参数、channel ID 和 capability 不是本 ADR 的一部分，分别由运行配置、组件文档和
验证证据维护。

## 重新评审条件

只有当板端测试证明某些模式必须常驻并行，且资源、display layer、USB 和 VE
限制均有明确证据时，才允许增加并发模式。不得仅因 sample 可以单独运行就推断
组合也受支持。
