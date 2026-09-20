# 组件设计文档

本目录集中保存各产品运行时组件的内部工程设计。正式组件源码目录只保留源码、头文件、
构建文件和运行所需资源，不再放置 Agent 指导或长篇设计文档；迁移期 reference/sample
的原始说明和许可文件不作为产品设计事实源。

## 组件入口

| 组件 | 文档 | 源码 | 负责的内部事实 |
| --- | --- | --- | --- |
| `camera-gui` | [`camera-gui/`](camera-gui/) | [`../../apps/camera-gui/`](../../apps/camera-gui/) | Qt 页面、后端监督、GUI 内部显示边界 |
| `camera-mpp-service` | [`camera-mpp-service/`](camera-mpp-service/) | [`../../services/camera-mpp-service/`](../../services/camera-mpp-service/) | Service 模块、配置语义、实际 Pipeline 和已知限制 |
| `camera-yolo-worker` | [`camera-yolo-worker/`](camera-yolo-worker/) | [`../../src/component/YOLOv8/`](../../src/component/YOLOv8/) | YOLO worker 依赖和内部迁移边界 |
| MPP reference tools | [`mpp-reference-tools/`](mpp-reference-tools/) | [`../../src/component/MPP/`](../../src/component/MPP/) | sample 派生目标的用途和非产品边界 |

## 放置边界

组件文档回答“一个二进制内部怎样实现”。以下内容不在这里重复：

- 跨进程系统关系和稳定资源 owner：[`../architecture/`](../architecture/)；
- IPC 字段与兼容性：[`../interfaces/`](../interfaces/)；
- MPP/MPI、DISP2 等可复用 BSP 知识：[`../platform/`](../platform/)；
- 当前实现、已确认事实和能力：[`../STATUS.md`](../STATUS.md)；
- 产品行为与未来工作：[`../requirements.md`](../requirements.md) 和
  [`../roadmap.md`](../roadmap.md)；
- Agent 执行规则见 [`../../agent.md`](../../agent.md)，外部来源登记见
  [`../platform/reference-sources.md`](../platform/reference-sources.md)。

一个事实若只由一个可执行程序实现和维护，放在对应组件目录；若它约束两个或更多
组件，则提升到 architecture、interfaces 或 platform 中的唯一 owner，组件页只链接。
