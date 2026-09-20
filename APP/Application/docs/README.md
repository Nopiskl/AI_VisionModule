# Application 文档入口

Application 文档按事实职责平铺组织。完整 Agent 总指导位于
[`../agent.md`](../agent.md)；了解当前实现先读 [`STATUS.md`](STATUS.md)。

## 目录结构

```text
Application/
├── agent.md                 Agent 总指导唯一正文
├── AGENTS.md                自动发现兼容入口
└── docs/
    ├── README.md            总导航、事实 owner 和任务路由
    ├── requirements.md      产品范围、约束、Decision 和 Open
    ├── roadmap.md           Workstream、依赖和阶段出口
    ├── STATUS.md            当前实现、已确认事实和阻塞
    ├── deployment-bringup.md 实际环境构建、部署、首轮运行和证据回传
    ├── architecture/        跨组件架构视图与 ADR
    ├── interfaces/          跨进程协议
    ├── platform/            MPP、DISP2 和外部平台参考
    └── components/          产品组件内部设计与已知限制
```

`requirements.md`、`roadmap.md` 和 `STATUS.md` 是所有任务的高频入口；其余文档按事实
owner 分类。仓库不维护 task、changelog、历史归档或独立验证目录。

## 单一事实源

| 事实类型 | Owner |
| --- | --- |
| Agent 稳定工程边界、禁止项和交接规则 | [`../agent.md`](../agent.md) |
| 产品行为、非目标、Decision、Open 和旧 ID 去向 | [`requirements.md`](requirements.md) |
| Workstream Priority、依赖和阶段出口 | [`roadmap.md`](roadmap.md) |
| 当前实现、已经确认的事实、能力和 blocker | [`STATUS.md`](STATUS.md) |
| 实际环境交叉构建、部署、首次板测和证据回传 | [`deployment-bringup.md`](deployment-bringup.md) |
| 系统上下文、运行时模型、媒体流和资源 owner | [`architecture/`](architecture/) |
| 长期决策理由与后果 | [`architecture/adr/`](architecture/adr/) |
| GUI/MPP IPC v1 | [`interfaces/ipc-v1.md`](interfaces/ipc-v1.md) |
| MPP/MPI 通用接口和 SmartIPC | [`platform/mpp/`](platform/mpp/) |
| Qt/linuxfb/DISP2 契约 | [`platform/display/`](platform/display/) |
| 外部参考版本、用途、ABI 和许可边界 | [`platform/reference-sources.md`](platform/reference-sources.md) |
| 组件内部模块、配置、Pipeline 和已知限制 | [`components/`](components/) |

一个事实只在最具体的 owner 中定义，其他位置使用链接，不复制正文。

## 文档放置规则

- 改变产品“要做什么”：更新 `requirements.md`。
- 改变跨组件且长期有效的结构、owner 或高代价选择：更新架构视图，并经主维护者决定
  后新增或修订 ADR。
- 改变跨进程线格式或语义：更新 `interfaces/`，同步通信双方。
- 总结可跨组件复用的 vendor/BSP 知识：更新 `platform/`。
- 改变某一程序怎样实例化通用能力：更新 `components/<component>/`。
- 跨组件资源约束进入 `architecture/resource-ownership.md`；组件私有的不确定性进入
  对应 `known-limitations.md`。
- 当前实现、已经确认的事实、能力或 blocker 变化：更新 `STATUS.md`。
- 未来顺序、依赖或阶段出口变化：更新 `roadmap.md`。

具体验证由当前用户任务指定，不为测试计划、模板或一次运行单独创建固定目录。实际结果
需要长期影响能力声明时，只在 `STATUS.md` 摘要记录基线、结论和仍未确认的范围；原始
日志按用户指定位置保存。

## 冲突处理

1. **规范目标**：当前用户明确要求 > Accepted requirement/ADR > 其他设计说明。若
   requirement 与 ADR 冲突，请主维护者决定，不自行选择。
2. **当前行为**：源码、CMake 和运行配置 > README/设计描述。实现偏离规范时记录为缺陷
   或组件限制，不修改规范把偏差合理化。
3. **能力结论**：目标板能力只来自与明确基线绑定的实际结果；计划、sample、配置、
   静态符号或源码存在不能替代真实结果。
4. **历史追溯**：普通历史由 Git 管理；当前事实不从旧提交反向覆盖。

## 按任务阅读

所有任务先读 `agent.md`、`STATUS.md`、相关组件页和待修改源码/配置，再按范围追加：

| 任务 | 追加阅读 |
| --- | --- |
| 产品范围或 Open Decision | requirements、相关 architecture/ADR |
| Workstream、阶段出口或当前阻塞 | roadmap、STATUS、相关组件限制 |
| GUI/交互 | components/camera-gui/README、components/camera-gui/ui-integration、interfaces；涉及显示再读 platform/display |
| IPC | interfaces/ipc-v1、共享 codec 和通信两端 |
| MPP/MPI 接口 | platform/mpp 对应模块、实际 sysroot；有缺口再定向查看 sample |
| MPP Service Pipeline | platform/mpp、components/camera-mpp-service/pipelines 和 known-limitations |
| Qt/DISP2 | architecture/resource-ownership、platform/display、ADR-0005、相关源码 |
| 构建/打包/第三方 | Application README/CMake、manifest、platform/reference-sources、相关组件限制 |
| 实际环境部署和首次运行 | deployment-bringup、STATUS、配置和相关组件限制 |
| 板端能力或资源问题 | STATUS、architecture/resource-ownership、相关 component；验证方式由用户指定 |
