# ADR-0005：Qt linuxfb 与 MPP 视频使用 DISP2 分层合成

- 状态：Accepted
- 日期：2026-09-11
- 修订：2026-09-12，显示画布配置化并明确初始 layer 方案
- 修订：2026-09-13，将 handle、画布和适配实现下放到平台/配置文档
- 修订：2026-09-14，限定分层到 Camera/Playback，并接受 YOLO 会话的 framebuffer 交接
- 修订：2026-09-14，恢复 UVC 后明确 Qt 保留 UI layer、MPP 不创建 UVC VO layer

## 背景

产品需要在 Camera Preview 上持续显示 Qt 的拍照、录像按钮和状态信息。Qt 与
MPP 如果同时写同一个 framebuffer，会产生资源竞争、画面撕裂和不可控的关闭
顺序。V851S/sun8iw21 的 DISP2 支持多个 channel/layer、z-order 和 alpha，MPP
VO 也通过显示适配层使用 DISP2。

## 决策

- Qt 第一阶段使用 `linuxfb` QPA；
- Camera/Playback 会话中 Qt UI 子进程是 framebuffer 的唯一 writer，并拥有与 MPP
  video 不同的 UI layer；
- `camera-mpp-service` 只拥有独立且较低 z-order 的 MPP VO video layer；
- Camera Preview 与 Playback 都复用由 MPP Service 管理的 video layer；
- UVC 会话继续运行 Qt UI 子进程，但不创建 MPP VO/video layer；UI 仅显示 Host 与传输
  状态，媒体数据输出到 USB gadget；
- LCD/Qt UI 与 MPP VO 输出读取同一产品显示 profile，但输入媒体尺寸不等于显示画布；
- DISP2 在硬件中合成 UI 与视频，两个进程不得 mmap 或写入同一个 framebuffer；
- Qt 的拍照和录像按钮只发送 IPC，不直接调用 MPP；
- 切到 YOLO 时先停止 MPP，再销毁整个 Qt UI 子进程；只有 `QApplication` 和 linuxfb
  都析构后，YOLO worker 才能直接写同一 framebuffer；
- YOLO 退出后先关闭其 camera/framebuffer 并释放 backend lock，再重建 Qt UI；
- Qt 不链接 MPP。UI layer 的必要配置由小型 DISP2 adapter 或经验证的 BSP 接口
  完成，MPP 继续通过自己的媒体显示接口管理视频层；
- MPP 可以在自身资源分配器中登记/排除 Qt layer，但该登记不转移所有权；不得关闭、
  重配、改优先级、enable 或 disable Qt 正在使用的实际 layer；
- GUI adapter 使用 framebuffer DISP2 ioctl 查询实际 UI handle；只有它与配置的 outside
  handle 一致、且不同于 MPP video handle 时才允许启动 MPP。像素格式、alpha 和
  z-order 的实际效果仍由 V851S 板测确定。

## 原因

- 保持 Qt 和 MPP 的进程、依赖及故障隔离；
- Camera/Playback 中 Qt 可持续显示按钮和 MPP 错误；UVC 中可持续显示 Host/传输状态；
  YOLO 会话按产品决定全屏显示推理结果，不保留 Qt overlay；
- 不需要把视频帧拷贝到 Qt，也不需要跨进程传递 Preview 图像；
- DISP2 负责合成，避免 CPU 做逐帧 RGB/YUV 混合；
- framebuffer 只有一个 writer，所有权可检查、可恢复。

## 实现约束

- Qt layer 必须验证 ARGB/per-pixel-alpha 或等价透明方案；全屏不透明 UI 会遮住
  下方视频层；
- MPP 不能复制 vendor sample 中关闭非自有 UI layer 的行为；sample 仅用于理解
  video output 配置顺序；
- Qt 或 MPP 重启时只清理自己拥有的 layer，不进行全局 layer reset。

平台 API、sample 适用边界、部署 profile 和板测步骤见
[`../../platform/display/linuxfb-disp2.md`](../../platform/display/linuxfb-disp2.md)。

## 否决方案

### Qt 与 MPP 同写 `/dev/fb0`

无法定义独占 owner，任一进程重绘或退出都可能破坏另一方画面。

同理，Qt 与 YOLO 同时写 framebuffer 仍被禁止；本次接受的是先退出 Qt、再启动 YOLO
的时分复用，不是共享写入。

### 把 MPP Preview 拷贝到 Qt 控件

会引入 YUV/RGB 转换、内存带宽和跨进程传输，V851S 当前没有这种需求。

### Qt 直接链接 MPP 来配置 UI layer

会破坏 ADR-0001 的依赖边界，并把 MPP 初始化和失败风险带入 GUI。

## 后果

- GUI 提供小型 DISP2 layer 门禁并从 MPP 配置读取 handle contract；
- 需要对 Qt framebuffer 的透明格式、rotation、stride 和 alpha 做板端测试；
- MPP Service 的显示模块必须显式避开 UI layer；
- Playback 由 VO 在配置画布内保持宽高比缩放，不由 Qt 接收、转换或绘制解码帧；
- UVC 媒体数据不送 LCD，Qt layer 下方没有由 UVC 创建的 MPP video layer；
- YOLO worker 只能在 Qt UI 子进程和 MPP 均退出后直接写 framebuffer；
- Qt overlay 的静态校验不等于该合成方案已经板端成立。

## 重新评审条件

只有板测证明 V851S 当前 framebuffer/disp 驱动无法提供透明 UI layer，或显示
硬件只能可靠支持单 layer 时，才重新评审。重新评审应比较局部 UI framebuffer、
color key、MPP RGN/overlay 和由单一 compositor 合成等备选方案，并提供带宽与
稳定性证据。
