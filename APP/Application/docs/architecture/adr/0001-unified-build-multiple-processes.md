# ADR-0001：统一构建，多个产品进程

- 状态：Accepted
- 日期：2026-09-11

## 背景

Application 同时使用 Qt、Allwinner MPP、OpenCV 和 VIPLite。它们拥有不同的
依赖、线程模型、设备资源和失败方式。可选方案包括每个功能一个 sample 进程、
所有功能链接进 Qt 单体程序，以及统一工程生成少量职责进程。

## 决策

使用一个权威仓库和一套顶层 CMake，至少生成：

```text
camera-gui
camera-mpp-service
camera-yolo-worker
```

允许增加不参与产品资源所有权的 `camera-cli` 和测试工具。GUI 不链接 MPP、
OpenCV 或 VIPLite；它通过进程监督和版本化 IPC 控制后端。

## 原因

- 保留 vendor/AI 故障隔离，媒体崩溃不必直接破坏 UI；
- 避免 Qt UI 线程承担 MPP/VENC/YOLO 工作；
- 允许独立构建、部署、重启和裁剪 worker；
- 统一 CMake 避免每个 Tina package 或 sample 维护不同源文件和链接参数；
- 相比每个功能一个进程，减少重复初始化和组合数量爆炸。

## 后果

- 需要定义 Unix-domain-socket 协议和 backend 状态机；
- 需要可靠的进程停止、超时、锁和错误传播；
- 若以后跨进程传递图像，必须使用有界共享内存或经验证的 DMA-BUF 方案；
- 部署包含多个二进制，但由同一个 Application package 管理。

## 否决方案

### 每个功能一个产品二进制

会重复 MPP_SYS/ISP/VI 初始化，无法自然表达 Preview + Record 等组合，并造成
设备争抢。现有 sample 仅作为迁移和诊断工具保留。

### Qt 单体程序

会把 Qt、MPP、OpenCV、VIPLite 和 RTSP 放入同一地址空间，使依赖、线程、异常
恢复和可选构建高度耦合。

## 重新评审条件

只有当目标系统明确不允许多进程，或经过测量证明 IPC 是不可接受的性能瓶颈，
才重新评审本决策。重新评审必须提供测量数据和迁移计划。
