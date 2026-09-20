# ADR-0003：OpenCV YOLOv8 与 MPP 互斥

- 状态：Accepted
- 日期：2026-09-11
- 修订：2026-09-14，明确 Qt UI 进程级退出、framebuffer 交接和恢复

## 背景

YOLOv8 worker 使用 OpenCV/V4L2 获取摄像头画面并使用 VIPLite 推理；MPP 服务
通过 ISP/VIPP/VI 获取同一 sensor。两套 backend 同时运行会产生 camera、显示
和内存资源争抢。

## 决策

`camera-yolo-worker` 与 `camera-mpp-service` 严格互斥：

- YOLO 运行时停止 MPP 服务或使其进入未初始化 MPP 的深度 Idle；
- MPP 运行时 YOLO worker 不存在；
- 使用跨进程 backend lock 记录 owner 和 PID；
- Qt 必须等待前一个 backend 报告 stopped 并释放锁后才能启动另一个；
- `camera-gui` 使用不创建 `QApplication` 的外层会话监督器；进入 YOLO 时先停止 MPP，
  再退出 Qt UI 子进程，使 linuxfb 彻底关闭 framebuffer；
- YOLO worker 保留 OpenCV/V4L2 捕获、VIPLite 推理和直接 framebuffer 输出，并与 MPP
  使用同一个 backend lock；
- YOLO 正常或异常退出后，外层监督器重新创建 Qt UI 子进程，再由 Qt 启动 MPP；
- 当前不设计 MPP frame export、YOLO/MPP 并行或共享显示写入。

## 原因

- 需求已明确两种工作方式互斥；
- 避免为了当前非需求引入跨进程 YUV/DMA-BUF 传输；
- 减少 V851S 的 buffer、带宽和生命周期复杂度；
- 独立进程保留 OpenCV/VIPLite 与 MPP 的依赖隔离。

## 后果

- 模式切换有明确停止/启动延迟；
- GUI 需要处理启动失败、停止超时和锁恢复；
- YOLO 仍拥有自己的 OpenCV capture 和 framebuffer 输出路径；
- YOLO 运行期间没有 Qt 控件；外部可终止 worker，或向外层监督器发送 `SIGUSR1` 返回
  Qt，第二次 `SIGUSR1` 可把未收敛 worker 升级为强制终止；
- worker 或 Qt UI 子进程死亡时，kernel flock/parent-death signal 负责把 owner 收敛到
  零，再由监督器恢复单一会话。

## 重新评审条件

只有产品新增“YOLO 与 Preview/Record/UVC 同时运行”的明确需求时重新评审。
届时必须将 MPP 设为唯一 camera owner，并单独设计有界共享内存或 DMA-BUF frame
export；不得直接同时打开 `/dev/video*`。
