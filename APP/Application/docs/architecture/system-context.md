# 系统上下文

- 文档角色：定义 Application 与用户、设备、BSP 和运行时组件之间的稳定边界
- 决策依据：ADR-0001、ADR-0003、ADR-0004、ADR-0005

## 系统边界

Application 是一个统一构建、多个进程的 Camera 产品程序。它负责用户交互、媒体
控制、摄像头/编解码能力和 AI worker 集成；目标系统的内核、设备树、ISP tuning、
驱动、rootfs 和设备权限仍由 Tina/OpenWrt BSP 提供。

```text
                    +---------------- Application ----------------+
User -------------->| camera-gui 外层会话监督器                    |
                    |    |                                         |
                    |    +-- Qt UI 子进程 -- IPC -> MPP service     |
                    |    `-- 互斥启动 -----------> YOLO worker      |
                    +-------------------------|---------------------+
                                              |
                 +----------------------------+--------------------+
                 | Sensor / ISP / MPP / DISP2 / Storage / Network  |
                 | OpenCV / VIPLite / toolchain / rootfs            |
                 +-------------------------------------------------+
```

## 运行时组件

| 组件 | 稳定职责 | 明确不负责 |
| --- | --- | --- |
| `camera-gui` | 外层会话监督、Qt UI 子进程、用户操作、MPP 监督、IPC client；Camera/Playback/UVC 时拥有 UI layer | MPP 初始化、媒体编码/解码、AI 推理 |
| `camera-mpp-service` | 产品级 MPP owner、Camera/Playback/UVC 生命周期、媒体输出 | Qt 页面、OpenCV/VIPLite 推理、USB configfs/ADB 部署 |
| `camera-yolo-worker` | YOLO 模式下独占 capture、推理和 framebuffer 输出 | MPP 产品 Pipeline；与 Qt/MPP 并发运行或并发显示 |

组件内部实现见 [`../components/`](../components/)。组件名称可以随构建目标演进，但
GUI、MPP 与 AI 的进程隔离和依赖方向只有通过 ADR 才能改变。

## 外部参与者与依赖

| 外部对象 | Application 使用方式 | 边界 |
| --- | --- | --- |
| 用户 | 通过 Qt GUI 发起 Camera、Record、RTSP、Playback、UVC 或模式切换 | 不直接操作后端设备 |
| Sensor/ISP/V4L2 | 由当前活动后端独占 | MPP 与 YOLO 不并发打开同一采集资源 |
| MPP/BSP | 由 MPP Service 使用 | GUI 不链接或初始化 MPP |
| DISP2/LCD | Camera/Playback 合成 Qt UI 与 MPP video；UVC 只保留 Qt UI；YOLO 独占 framebuffer 输出 | layer 门禁或进程交接完成前不得启动下一 writer |
| 存储 | Snapshot、Record 和 Playback 文件 | 路径、安全提交和挂载策略由组件与 requirement 约束 |
| 网络客户端 | 消费可选 RTSP 输出 | 网络阻塞不得破坏媒体资源回收 |
| USB Host/UVC gadget | Host 协商格式并驱动 UVC STREAMON/OFF；系统提供预配置 gadget node | 服务不创建 configfs gadget，本阶段不处理与 ADB composite 的兼容 |
| Tina/OpenWrt | 工具链、BSP、rootfs 和 package adapter | 不维护第二份 Application 业务源码 |

## 交互边界

- GUI 与 MPP Service 只通过版本化 IPC 交换命令、响应和事件，不传输视频帧；协议见
  [`../interfaces/ipc-v1.md`](../interfaces/ipc-v1.md)。
- GUI 通过后端监督启动、停止和观察进程；“进程存在”不等于“后端 ready”。
- 外层会话监督器本身不创建 `QApplication`；Qt UI 子进程完全退出后，YOLO 才能接管
  framebuffer，YOLO 退出后再重建 Qt UI。
- MPP Service 和 YOLO worker 不作为彼此的库依赖，也不直接控制对方生命周期。
- 参考 sample 只用于理解平台 API，不属于产品运行时拓扑；参考边界见
  [`../platform/reference-sources.md`](../platform/reference-sources.md)。

## 源码与交付边界

`Application/` 是 GUI、MPP Service、YOLO worker 和共享模块的唯一业务源码。
开发使用 out-of-source build；SDK 中的适配仅调用同一套构建和安装入口。构建细节见
Application 根 README，决策理由见 ADR-0004。
