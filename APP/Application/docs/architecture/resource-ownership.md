# 资源所有权

- 文档角色：定义运行模式下跨进程硬件、显示和锁的唯一 owner
- 决策依据：ADR-0002、ADR-0003、ADR-0005

资源 owner 表示“负责创建、使用、停止和释放的唯一运行时主体”。获得资源失败不能
把它标记为 owned；释放完成前不能把所有权交给下一主体。

## Owner 矩阵

| 资源 | Camera 模式 | Playback 模式 | UVC 模式 | YOLO 模式 | 稳定规则 |
| --- | --- | --- | --- | --- | --- |
| Sensor/ISP/V4L2 capture | `camera-mpp-service` | 无 | Host STREAMON 期间为 `camera-mpp-service` | `camera-yolo-worker` | 任一时刻只有一个采集 owner |
| MPP_SYS 与 MPP channel | `camera-mpp-service` | `camera-mpp-service` | `camera-mpp-service` | 无 | 只有 MPP Service 创建产品级 MPP 资源 |
| 编码器、封装与媒体输出 | `camera-mpp-service` | 无 | MJPEG/H.264 时由 `camera-mpp-service` 持有 VENC；gadget fd/buffer 始终属于 UVC Pipeline | 无或另行决策 | 输出不能成为第二个 camera owner |
| 解封装、解码与播放时钟 | 无 | `camera-mpp-service` | 无 | 无 | 只存在于 Playback 生命周期内 |
| MPP VO video layer | `camera-mpp-service` | `camera-mpp-service` | 无 | 无或另行决策 | UVC 不创建视频层；Service 只管理自己的 video layer |
| Framebuffer-backed UI layer | `camera-gui` Qt 子进程 | `camera-gui` Qt 子进程 | `camera-gui` Qt 子进程 | `camera-yolo-worker` | YOLO 前先销毁 Qt；任一时刻一个 writer |
| Backend owner/lock | 活动 MPP 后端 | 活动 MPP 后端 | 活动 MPP 后端 | 活动 YOLO 后端 | 交接完成前不得启动下一后端 |
| 输出文件与临时文件 | 对应 MPP 输出 | Playback 只读输入 | 无文件输出 | 按后续设计 | producer 负责提交、关闭和异常清理 |

具体 VI/VO/VENC/VDEC/MUX/DEMUX/CLOCK 的对象所有权见组件
[`pipelines.md`](../components/camera-mpp-service/pipelines.md)；平台 frame/stream
Get/Release 规则见 [`../platform/mpp/`](../platform/mpp/)。

## 后端交接协议

```text
Active owner
-> 拒绝新业务
-> 停止并 join 数据线程
-> 归还借出的 frame/stream
-> 解除连接并销毁自有资源
-> 释放显示/video owner
-> 释放 backend lock
-> MPP 报告 Stopped/退出；需要 framebuffer 交接时销毁 Qt UI
-> Next owner acquire
```

- GUI 负责监督交接，不因停止命令已发出而提前启动下一后端。
- stale PID、异常退出或超时必须先确认设备和锁状态；不能通过同时启动另一个后端
  “探测”资源是否已经释放。
- 后端切换失败时保持零个或一个 owner，绝不能降级为两个 owner 并发。

## 显示所有权

```text
Camera/Playback:
DISP2 / LCD
├── Qt UI layer       owner = camera-gui Qt 子进程
└── MPP video layer   owner = camera-mpp-service

YOLO:
camera-yolo-worker -> framebuffer-backed layer -> DISP2 / LCD
（Qt UI 子进程和 MPP 服务均不存在）

UVC:
Qt UI layer -> DISP2 / LCD
camera-mpp-service -> UVC gadget video node -> USB Host
（没有 MPP VO video layer）
```

- MPP 不 mmap 或写 Qt framebuffer；Qt 不操作 MPP video layer。
- MPP 对 Qt layer 只能进行不改变其实际状态的必要登记，不能关闭、启停、重配或修改
  优先级。
- 任一进程退出时只清理自己拥有的 layer，不执行全局 display reset。
- YOLO 直接写 framebuffer 只在 Qt UI 和 MPP 均退出后合法；worker 退出时先关闭
  camera/framebuffer，再释放共享 backend lock，外层监督器随后重建 Qt。
- framebuffer 映射、格式、alpha、rotation、z-order 和具体 handle 是平台部署事实，
  由 [`../platform/display/linuxfb-disp2.md`](../platform/display/linuxfb-disp2.md) 与
  运行配置维护，不在架构中固定数值。

## 生命周期不变量

- 创建失败按依赖逆序回滚，stop 幂等。
- 每次成功取得的 frame、stream、buffer、fd、channel 和线程都有唯一释放者。
- callback 和 signal handler 不转移资源 owner，只投递事件或唤醒控制路径。
- 队列必须有界；慢消费者不得通过无界积压长期占有媒体资源。
- 未经目标板验证，不根据 sample 或其他 SoC 推断可并发资源组合。

## 资源容量边界

资源数量属于系统架构约束，具体 channel ID、buffer 数和码率仍由组件配置管理。
当前最大 Camera 候选组合为：

```text
Preview VI -> VO
Record VI  -> H.264 VENC -> MUX
RTSP VI    -> H.264 VENC -> network sink
```

Snapshot 只允许在未录像的 Camera Preview 中按需创建 JPEG VENC；录像期间由服务端
拒绝，不计入 `Preview + Record + RTSP` 峰值。允许 `Record + RTSP` 之前仍必须确认
V851S 的 VI/VENC 数量、VE 调度、CMA/ION 和内存带宽。源码能创建这些分支不代表硬件
能够稳定并发。Playback 与 Camera 顶层互斥，UVC 也与二者顶层互斥；YOLO 与整个 MPP
Service 互斥，不能把这些模式计入同一正常运行预算。UVC 的独立候选峰值还包括 gadget
mmap buffers、固定深度应用复制队列，以及 MJPEG/H.264 时的 VENC VBV；YUYV 路径不
创建 VENC，但需要 CPU 完成 NV21→YUYV 复制转换。

未压缩 YUV420 buffer 可以先按下式估算下界：

```text
frame_bytes = aligned_width * aligned_height * 1.5
pool_bytes  = frame_bytes * buffer_count
```

该估算不包含 stride/height 对齐、LBC、metadata、VBV、decoder reference frame、MUX/
DEMUX cache、Qt framebuffer、allocator 和驱动开销。每个 VI path、编码/解码池、AI 输入
池和手工队列都必须单独计入，不能因分辨率相同就假定共享 buffer。

作出并发或性能承诺前，至少需要取得目标运行组合下的可用 RAM、CMA/ION 峰值、实际
buffer 数、CPU、连续 FPS/码率、温度和反复启停后的资源回归事实。具体采样命令、时长、
循环次数和通过阈值由主维护者在对应任务中指定。

若需要两个进程共享 camera frame、同时运行 YOLO 与 MPP，或改变 Qt/MPP layer owner，
必须先修改 requirement 并重新评审 ADR-0003/0005。
