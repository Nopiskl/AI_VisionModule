# Application Agent 总指导

本文件是 `Application/` 及其全部子目录的 Agent 总指导，也是唯一现行规则正文。
自动发现入口 [`AGENTS.md`](AGENTS.md) 只负责引导到本页，不复制规则。项目采用
“主维护者决策、Agent 调查与实施”的轻量协作方式，不为普通修改维护 task、changelog
或独立验证报告。

## 开始工作

每次任务按需阅读，不默认通读全部文档：

1. [`docs/README.md`](docs/README.md)：文档 owner、冲突规则和任务路由；
2. [`docs/STATUS.md`](docs/STATUS.md)：当前实现、已经确认的事实和阻塞项；
3. [`docs/components/`](docs/components/) 中相关组件文档，以及待修改源码和配置；
4. `docs/README.md` 为该类任务列出的接口、平台或架构材料。

先检查现有工作树并保留用户改动。若目标能从上下文、源码和文档确定，直接调查并
实施；只有缺少会实质改变方案的产品决定或权限时才请求主维护者确认。

## 轻量工作流

1. 明确用户可见目标、修改边界和现有工作树状态。
2. 判断是否改变产品行为或范围；若是，先更新
   [`requirements.md`](docs/requirements.md)。
3. 判断是否改变长期、跨组件且代价较高的决策；若是，经主维护者确认后新增或修订
   [`architecture/adr/`](docs/architecture/adr/)，并同步架构视图。
4. 按事实 owner 更新接口、平台或组件设计，再修改源码和配置。
5. 按当前用户指定的范围、环境、命令和通过条件验证；未授权时不得自行扩展到交叉
   编译、部署、目标板操作或外部系统修改。
6. 只有当前实现、已确认事实、能力边界或 blocker 变化时才更新
   [`STATUS.md`](docs/STATUS.md)；路线优先级变化才更新
   [`roadmap.md`](docs/roadmap.md)。
7. 在当前对话中交接结果、实际执行的检查和未确认事项，不生成永久过程流水账。

执行状态只需在协作会话中表达为“分析、实施、检查、阻塞、完成”，不写入仓库。

## 稳定架构边界

- 一个仓库和一套顶层 CMake 生成多个职责清晰的进程；
- `camera-gui`、`camera-mpp-service` 和 YOLO worker 保持进程隔离；
- `camera-mpp-service` 是唯一产品级 MPP owner；不同 MPP 能力由同一服务内互斥的
  Pipeline/Output 组合，不用多个 sample 进程拼接产品功能；
- GUI 不链接 MPP、OpenCV 或 VIPLite，只通过进程监督和版本化 IPC 控制后端；
- YOLO worker 与整个 MPP 服务严格互斥；
- Camera/Playback 会话中 Qt 独占 UI framebuffer/layer，MPP 只拥有独立的 VO video
  layer；YOLO 会话启动前必须退出 Qt UI 子进程，之后由 YOLO worker 临时独占同一
  framebuffer，YOLO 退出后再重建 Qt UI；
- Application 是权威源码，Tina/OpenWrt 只提供工具链、BSP、rootfs 和薄打包适配；
- 开发使用树外源码和 out-of-source build。

改变进程边界、唯一 owner、互斥关系、显示所有权或构建策略时，必须获得主维护者明确
决定，并新增或修订 ADR。功能范围和产品行为以
[`requirements.md`](docs/requirements.md) 为准，具体默认参数以
`configs/mpp-service.conf` 和
[`configuration.md`](docs/components/camera-mpp-service/configuration.md) 为准。

## 资源和生命周期不变量

- 任一时刻只有一个 backend 持有 sensor/V4L2/ISP；
- 任一时刻只有一个 MPP 顶层 Pipeline 活动；切换前等待前一 owner 完整停止并释放锁；
- Camera/Playback 会话中 MPP 不 mmap/write Qt framebuffer，Qt 不操作 MPP video
  layer；两者只在 DISP2 的不同 layer 上合成；
- YOLO 取得 framebuffer 前，MPP 服务和 Qt UI 子进程必须都已退出；YOLO 释放
  framebuffer 和 backend lock 后才能重建 Qt UI；
- MPP 对 Qt UI layer 只允许 Add/RemoveOutside 内部登记，不得 Open/Close、
  Enable/Disable、SetAttr 或 SetPriority；
- MPP 创建失败必须逆序回滚，`stop` 必须幂等；
- callback 和 signal handler 只投递事件或唤醒控制路径，不执行阻塞销毁；
- frame、stream、图像和事件队列必须有界，每次成功 Get 必须在所有路径 Release；
- 未经目标板确认，不得把其他 SoC、sample、静态符号或配置候选写成 V851S 产品能力。

## 默认禁止

除非当前用户明确授权，不得：

- 修改 `TinaSDKv5.0/`、内核、设备树或 rootfs；
- 提交工作区 `TMP/`、模型、生成 bundle 或授权未确认的 MPP/VIPLite 二进制；
- 把 sample target 描述成产品服务，或新增按功能组合拆分的 MPP 产品二进制；
- 同时启动或设计同时运行 YOLO 与 MPP；
- 在 Qt UI 会话中对 Qt layer 执行任何改变实际 layer 状态的 MPP 操作；
- 把 `TMP/sdk_disp` 的预编译库或对象直接复制为产品依赖；
- 擅自交叉编译、部署、板测或操作用户未纳入当前任务的外部环境；
- 未经测量承诺分辨率、帧率、码率、编码/解码并发、USB 带宽或内存占用；
- 清理、覆盖或回退用户已有的工作树改动。

## 参考代码和目录

- 使用 `tools/prepare_references.sh` 在工作区 `TMP/Yuzukilizard` 准备固定提交；
- sample 只用于核对 MPI 调用、回调、参数和销毁顺序，不复制 sample `main()`；
- 日常 MPI 开发先读 [`docs/platform/mpp/`](docs/platform/mpp/)；只有接口手册或当前
  sysroot 无法回答的问题才定向返回 sample；
- 参考版本、用途和边界见
  [`reference-sources.md`](docs/platform/reference-sources.md)；
- 产品组件设计统一进入 [`docs/components/`](docs/components/)；正式源码组件目录只
  保存源码、头文件、构建文件和运行所需资源；
- `src/component/` 是迁移期目录。只有代码、构建、文档和打包路径可在同一任务内保持
  一致时才移动，不为目录外观做无功能重排。

## 检查、记录和交接

具体验证方式由当前用户任务指定。Agent 必须区分“源码存在”“静态检查通过”“完成
构建”“目标板观察到”和“产品验收”，只报告实际完成的范围。用户未指定验证方式时，
可以执行与改动直接相关且安全的本地检查，但要明确列出命令和未检查事项，不得外推
硬件能力。

项目不设置独立验证目录。长期有效的资源边界和风险分别写入 architecture 或对应
component；验证结果只有在改变当前能力或 blocker 时才摘要更新 `STATUS.md`，原始日志
放在用户指定位置、CI 或外部制品系统中。普通修改历史由 Git 承担。

最终交接至少说明：结果、修改范围、保持或改变的 requirement/ADR、实际执行的检查、
未确认事项、已知风险、既有工作树状态和安全的下一步。
