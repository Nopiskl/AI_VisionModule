# camera-yolo-worker

YOLOv8 的权威候选源码位于
[`src/component/YOLOv8/src/`](../../../src/component/YOLOv8/src/)，CMake 目标为
`camera_yolov8_opencv`，输出 `camera-yolo-worker`。本页描述组件边界，不把候选
源码存在误写成板端能力。

依赖边界：

- OpenCV/V4L2：由独立交叉工具链 sysroot 提供；
- VIPLite：由本地 standalone sunxi-mpp bundle 提供库和头文件；
- Qt GUI：不直接链接 YOLOv8；外层会话监督器只负责进程启动、退出和恢复。

原 DVP/MIPI Tina package 中的两份相同源码暂时保留，避免影响旧固件构建；
后续应让 package 从本目录的唯一源码生成，停止手工同步副本。

## 运行路径

worker 保留原逻辑：使用 OpenCV/V4L2 读取可配置 camera index，使用 VIPLite 推理并把
检测框合成到 BGR 帧，再按实际 framebuffer 分辨率缩放、转换为 RGB565，并对
`/dev/fbN` 做一次共享 `mmap`，依据 `line_length`、`xoffset`、`yoffset` 逐行复制。它会
校验 16-bit RGB565 位域，不假设 framebuffer 一定是紧密连续的
`width*height*2`，也不会在每帧执行数百次 `pwrite`。

当前产品模型契约是单个 `1×3×320×320` INT8 输入，以及单个与类别数和
8/16/32 stride 对应的 INT16 dynamic-fixed-point 输出。worker 会在写输入和读输出前
核对 tensor 数量、格式和元素数；模型不匹配时直接退出，不再沿用参考实现中可能越界的
隐式假设。VIPLite 初始化、network/buffer 创建的任一失败路径都会进入统一回收，再释放
backend lock。

启动参数：

```text
camera-yolo-worker --model PATH --classes PATH
                   [--memory BYTES] [--camera INDEX]
                   [--framebuffer PATH] [--backend-lock PATH]
```

旧的三个位置参数仍兼容。worker 在初始化 VIPLite、camera 或 framebuffer 前先取得与
MPP 相同的非阻塞 `flock`；锁记录 owner/PID，并因 fd 关闭或进程死亡自动释放。锁忙时
返回 exit code 3，不会继续探测设备。

## 生命周期与显示所有权

架构已经冻结为 YOLO 与整个 MPP service 严格互斥。进入 YOLO 前，MPP 服务和 Qt UI
子进程都必须退出；此时 worker 是唯一 camera/framebuffer owner。收到 SIGINT/SIGTERM、
正常结束或异常退出时，C++ owner 按 framebuffer→camera→VIPLite→backend lock 的逆序
释放，外层监督器随后重建 Qt。当前不规划 MPP 向 YOLO 共享 frame。

`VideoObjectDetectionPipeline.cpp` 是迁移期未接入目标的旧多线程实验，当前产品 target
不编译它；实际运行入口只由 `src/main.cpp` 维护。

当前实现和未确认范围见 [`../../STATUS.md`](../../STATUS.md)，跨进程顺序见
[`runtime-model.md`](../../architecture/runtime-model.md)，显示交接见
[`linuxfb-disp2.md`](../../platform/display/linuxfb-disp2.md)。
