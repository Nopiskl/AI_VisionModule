# Application 系统架构

- 文档角色：Accepted 架构入口，维护跨进程、跨组件且长期稳定的系统关系
- 最近复核：2026-09-14

本目录回答“系统由哪些边界组成、运行时怎样协作、媒体怎样跨组件流动、资源由谁
拥有”。单个程序内部的类和 Pipeline、平台 API、IPC 字段、当前成熟度与未来计划均由
其他分区维护。

## 一屏系统视图

```text
User
  |
  v
camera-gui 外层会话监督器
    |
    +-- Qt UI 子进程 -- IPC --> camera-mpp-service --> MPP/BSP
    |       |                         |
    |       | Qt UI layer             `--> MPP video layer
    |       `-----------------------> DISP2/LCD <-----------+
    |
    `--（Qt/MPP 已退出）--> camera-yolo-worker
                              | OpenCV/V4L2 + VIPLite
                              `--> framebuffer --> DISP2/LCD
```

统一工程不等于单体进程。GUI、MPP 服务和 YOLO worker 保持依赖、故障域和设备 owner
隔离；Application 是权威业务源码，SDK/BSP 提供工具链、驱动、rootfs 和薄打包适配。

## 核心不变量

1. `camera-gui` 不链接 MPP、OpenCV 或 VIPLite，只通过进程监督和版本化 IPC 控制后端。
2. `camera-mpp-service` 是唯一产品级 MPP owner，Camera、Playback 与 UVC 顶层模式互斥。
3. `camera-yolo-worker` 与整个 MPP 服务互斥，不与 MPP 并发持有 sensor/camera 资源。
4. Camera/Playback 中 Qt UI 与 MPP video 使用不同 layer；UVC 不创建 MPP VO，Qt 继续
   持有 UI layer；YOLO 中 Qt/MPP 均退出，worker 临时独占 framebuffer，任一时刻最多
   一个 framebuffer writer。
5. 后端切换必须完成停止、资源释放和 owner 交接后，才能启动下一个后端。
6. 跨组件不变量发生变化时必须获得明确决策，并新增或修订 ADR。

## 架构视图

| 文档 | 回答的问题 | 不负责的内容 |
| --- | --- | --- |
| [`system-context.md`](system-context.md) | 系统内外有哪些参与者、进程和依赖边界 | 类树、构建进度 |
| [`runtime-model.md`](runtime-model.md) | 谁监督谁、顶层模式怎样切换、失败怎样收敛 | MPI 创建/销毁调用序列 |
| [`media-flows.md`](media-flows.md) | 控制和媒体数据怎样跨组件流动 | channel ID、编码参数 |
| [`resource-ownership.md`](resource-ownership.md) | sensor、MPP、显示、跨进程锁及资源容量边界 | DISP2 ioctl 和平台部署值 |
| [`adr/`](adr/) | 为什么选择当前长期结构及其权衡 | 当前成熟度和临时实现细节 |

## 与其他文档的边界

| 内容 | 唯一 owner |
| --- | --- |
| 单个程序的内部结构、配置语义和实际 Pipeline | [`../components/`](../components/) |
| GUI/Service 命令、字段和兼容性 | [`../interfaces/`](../interfaces/) |
| MPP/MPI、SmartIPC 和 Qt/DISP2 平台知识 | [`../platform/`](../platform/) |
| 产品必须满足的行为、非目标和待决问题 | [`../requirements.md`](../requirements.md) |
| 当前实现、已确认事实和阻塞项 | [`../STATUS.md`](../STATUS.md) |
| 后续 Workstream、依赖和出口 | [`../roadmap.md`](../roadmap.md) |

## 收录规则

一个事实只有在约束两个或更多组件、且不随某次实现轻易变化时才进入 architecture。
类名、源文件、配置默认值、channel/layer 编号、完整 MPI 顺序、TODO 和一次检查结果不得在
这里形成第二事实源。架构正文描述当前生效结构；决策理由和被否决方案进入 ADR。

## 决策记录

当前 Accepted 决策为 ADR-0001～0005，索引见 [`adr/README.md`](adr/README.md)。改变
统一构建/多进程、唯一 MPP owner、Camera/Playback/UVC 或 YOLO 互斥、树外开发、Qt/MPP
显示所有权时，必须同步评审对应 ADR。
