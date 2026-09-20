# camera-mpp-service

`camera-mpp-service` 是产品唯一的 Allwinner MPP owner。它管理三个
互斥顶层 Pipeline 和按需 Output，不是多个 sample `main()` 的集合：

```text
MppService
├── CameraPipeline
│   ├── DisplayOutput       VI Preview -> VO
│   ├── SnapshotOutput      temporary VI -> JPEG VENC -> file
│   ├── RecordOutput        VI -> H.264 VENC -> MUX -> file
│   └── RtspOutput          independent VI -> H.264 VENC -> server
├── PlaybackPipeline        DEMUX -> VDEC -> VO, CLOCK -> DEMUX/VO
└── UvcPipeline             Host events -> on-demand VI/VENC -> gadget
```

稳定的顶层模式、媒体流和资源 owner 分别见
[`runtime-model.md`](../../architecture/runtime-model.md)、
[`media-flows.md`](../../architecture/media-flows.md) 和
[`resource-ownership.md`](../../architecture/resource-ownership.md)；当前实现和能力边界见
[`../../STATUS.md`](../../STATUS.md)。组件源码和构建入口位于
[`../../../services/camera-mpp-service/`](../../../services/camera-mpp-service/)。

## 实现一条 MPP 路径时怎样选文档

service 开发不从某个 sample 名称出发，而是先画出自己的 source、MPP module、sink 和
buffer owner，再按下面顺序收敛到代码：

```text
产品 requirement / architecture
-> platform/mpp 通用对象、API、状态和所有权
-> service configuration / 组件 pipelines
-> 对应 C++ owner 与幂等 rollback
-> 已知限制 / 当前状态 / 用户指定检查
```

其中平台 MPP 文档回答“这个 MPI 模块怎样使用”，service 组件文档回答“本产品选了
哪些实例、ID、参数和组合”。两者发生冲突时，先核对当前 sysroot 头文件和源码；不能
通过复制 sample 常量消除冲突。

| 要实现或修改的 service owner | 数据路径 | 实现前必须阅读 |
| --- | --- | --- |
| [`MppRuntime`](../../../services/camera-mpp-service/src/MppRuntime.cpp) | 进程级 MPP SYS | MPP [`core.md`](../../platform/mpp/core.md) 的 SYS、状态骨架和资源账本；[`pipelines.md`](pipelines.md) 顶层生命周期 |
| [`CameraPipeline`](../../../services/camera-mpp-service/src/CameraPipeline.cpp) | ISP/VIPP/Preview VI→VO | MPP [`core.md`](../../platform/mpp/core.md)、[`vi.md`](../../platform/mpp/vi.md)、[`vo.md`](../../platform/mpp/vo.md)；本组件 Pipeline 的 Camera Preview |
| [`DisplayOutput`](../../../services/camera-mpp-service/src/DisplayOutput.cpp) | VO device/layer/channel→DISP2 | MPP [`vo.md`](../../platform/mpp/vo.md)、平台 [`linuxfb-disp2.md`](../../platform/display/linuxfb-disp2.md)；本组件 Pipeline 的 DisplayOutput |
| [`SnapshotOutput`](../../../services/camera-mpp-service/src/SnapshotOutput.cpp) | VI GetFrame→JPEG VENC→文件 | MPP [`core.md`](../../platform/mpp/core.md) 的 Get/Release、[`vi.md`](../../platform/mpp/vi.md) 的 non-tunnel、[`venc.md`](../../platform/mpp/venc.md) 的 JPEG；本组件 Pipeline 的 SnapshotOutput |
| [`RecordOutput`](../../../services/camera-mpp-service/src/RecordOutput.cpp) | VI→VENC→MUX→MP4 | MPP [`core.md`](../../platform/mpp/core.md)、[`vi.md`](../../platform/mpp/vi.md)、[`venc.md`](../../platform/mpp/venc.md)、[`smart-ipc.md`](../../platform/mpp/smart-ipc.md) 的录像边界；本组件 Pipeline 的 RecordOutput 和 [`configuration.md`](configuration.md) |
| [`RtspOutput`](../../../services/camera-mpp-service/src/RtspOutput.cpp) | VI→VENC→GetStream→RTSP | MPP [`core.md`](../../platform/mpp/core.md)、[`vi.md`](../../platform/mpp/vi.md)、[`venc.md`](../../platform/mpp/venc.md)、[`smart-ipc.md`](../../platform/mpp/smart-ipc.md) 的 stream 扇出/参数集/线程；本组件 Pipeline 的 RtspOutput 和 [`known-limitations.md`](known-limitations.md) |
| [`PlaybackPipeline`](../../../services/camera-mpp-service/src/PlaybackPipeline.cpp) | DEMUX→VDEC→VO，CLOCK→DEMUX/VO | MPP [`core.md`](../../platform/mpp/core.md)、[`vdec.md`](../../platform/mpp/vdec.md)、[`vo.md`](../../platform/mpp/vo.md)；本组件 Pipeline 的 PlaybackPipeline 和 [`known-limitations.md`](known-limitations.md) |
| [`UvcPipeline`](../../../services/camera-mpp-service/src/UvcPipeline.cpp) | UVC Host event→按需 VI→MJPEG/H.264 VENC 或 NV21→YUYV→gadget | 平台 [`uvc.md`](../../platform/mpp/uvc.md)、MPP [`core.md`](../../platform/mpp/core.md)、[`vi.md`](../../platform/mpp/vi.md)、[`venc.md`](../../platform/mpp/venc.md)；本组件 Pipeline 的 UVC 章节和 [`known-limitations.md`](known-limitations.md) |

每条路径采用同一实现检查法：

1. 在组件 `pipelines.md` 中确定端点、绑定图、启动顺序、停止顺序和每个资源的 owner；
2. 从 `configuration.md` 读取 ID、格式和参数，不把平台手册或 sample 的示例值变成默认值；
3. 每个成功的 Create/Enable/Start/Bind/Get 立即写入 owned 状态，并提供对应的
   Destroy/Disable/Stop/UnBind/Release；
4. callback、signal handler 和数据 worker 只投递事件，控制线程负责状态切换和销毁；
5. 先用实际 sysroot 核对结构字段和可链接符号；确有缺口时使用对应 MPP 模块页列出的
   sample，再按 [`reference-sources.md`](../../platform/reference-sources.md) 核对来源和版本边界；
6. 同步 [`known-limitations.md`](known-limitations.md) 中受影响的边界，并按主维护者
   指定范围执行检查；分辨率、并发、性能和兼容性没有实际结果时不得作能力承诺。

当前通用手册还没有独立 MUX、DEMUX、CLOCK 页面。修改 `RecordOutput` 或
`PlaybackPipeline` 的这些模块时，以实际 sysroot 的 `mpi_mux.h`、`mpi_demux.h`、
`mpi_clock.h` 为 ABI owner，并只定向参考对应 sample；完成通用归纳后再为平台手册
增加模块页。

## 构建和运行

从工作区根目录使用顶层 CMake：

```sh
cmake -S Application -B build/v851s \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/Application/cmake/toolchains/v851s-musl.cmake" \
  -DV851S_C_COMPILER=/opt/v851s/bin/arm-openwrt-linux-gcc \
  -DV851S_CXX_COMPILER=/opt/v851s/bin/arm-openwrt-linux-g++ \
  -DV851S_SYSROOT=/opt/v851s/sysroot \
  -DAPPLICATION_BUILD_MPP=ON

cmake --build build/v851s --target camera_mpp_service
```

运行：

```sh
/usr/bin/camera-mpp-service \
  --config /etc/v851s-camera/mpp-service.conf
```

由 Qt UI 子进程启动时会额外传入 `--exit-with-parent`，使 Linux 在 UI 异常死亡时向服务
发送 SIGTERM，避免孤儿 MPP 进程继续持有 backend lock、sensor 或 video layer。独立诊断
运行不传该参数。

这些命令只有实际执行成功后才表示对应工具链能够构建。机器可读默认值在
[`../../../configs/mpp-service.conf`](../../../configs/mpp-service.conf)，参数语义在
[`configuration.md`](configuration.md)，命令和事件在
[`../../interfaces/ipc-v1.md`](../../interfaces/ipc-v1.md)。

## 运行边界

- `MppRuntime` 在服务生命周期内管理 MPP_SYS；`BackendLock` 保证 camera owner 唯一；
- Camera、Playback 与 UVC 切换先完整停止旧 Pipeline，再创建新 Pipeline；
- callback 只写入有界 `EventMailbox`，signal 只写 self-pipe，控制线程统一处理状态和销毁；
- 每个 IPC 连接和 mailbox 都有上限，消费者变慢不会导致无界内存增长；
- 每个创建失败都逆序回滚，`stop` 幂等，工作线程先通知并 join；
- `DisplayOutput` 只管理 MPP video layer；对 Qt UI layer 只允许 Add/RemoveOutside 登记；
- 输出文件先写 `.part` 并同步，再无覆盖提交；存储监测只请求控制线程停录，不自动
  mount、umount、格式化或删除文件；
- UVC 和 RTSP capability 分别按配置启用或禁用；音频和 Seek 当前关闭。
- UVC 假设部署已创建并绑定 gadget 节点；服务不操作 configfs，也不处理本阶段明确排除的
  ADB composite 兼容。

通用 MPP/MPI 对象、VI/VO/VENC/VDEC 接口和 SmartIPC 参考流程见
[`../../platform/mpp/`](../../platform/mpp/)；本服务实际实例化的资源与回滚见
[`pipelines.md`](pipelines.md)，当前静态检查边界和未确认风险见
[`known-limitations.md`](known-limitations.md)。

组件配置、Pipeline 和已知限制分别见 [`configuration.md`](configuration.md)、
[`pipelines.md`](pipelines.md) 和 [`known-limitations.md`](known-limitations.md)。

## 状态边界

源码、配置和组件 README 不能自行提升产品成熟度。当前状态只看
[`../../STATUS.md`](../../STATUS.md)，具体技术原因和未确认范围只在
[`known-limitations.md`](known-limitations.md) 维护。
