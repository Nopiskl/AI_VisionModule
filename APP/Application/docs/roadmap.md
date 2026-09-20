# Application 交付路线图

- 文档角色：计划优先级、依赖关系、Workstream 边界和阶段出口的唯一入口
- 最近复核：2026-09-15
- 当前实现与阻塞：[`STATUS.md`](STATUS.md)
- 产品范围：[`requirements.md`](requirements.md)

本页只回答“按什么顺序推进、进入条件是什么、到什么出口结束”。它不维护源码完成度、
配置默认值、任务步骤、验证命令或运行日志。

## 1. 固定字段与优先级

| 字段 | 含义 |
| --- | --- |
| ID | 稳定的计划追踪标识 |
| Priority | `Now / Next / Later / Deferred`，表示计划顺序而非实现状态 |
| Outcome | 完成后可观察的交付结果 |
| Entry gate | 启动前必须具备的依赖或条件 |
| Exit gate | 结束并允许后续 Workstream 使用的可观察条件 |
| Covers | 关联的 requirement/Open ID，不改变其 Decision |

`Now` 不表示已经开始或已经完成；当前实现和能力只看 `STATUS.md`。

## 2. 依赖关系

```text
源码阶段：WS-D Qt/DISP2 owner 门禁 -> WS-E Qt/MPP/YOLO 会话交接

WS-A 可追溯构建/打包
├── WS-B Camera 单能力
├── WS-C Playback 单能力
├── WS-D 的目标显示出口
├── WS-E 的目标切换出口
└── WS-UVC 的目标 UVC 出口

WS-B + WS-C + WS-D + WS-E + WS-UVC
└── WS-F 产品综合验收
```

WS-D/WS-E 的源码实现按主维护者决定前置，不以缺少实机阻塞；它们的 Capability 和最终
Exit gate 仍依赖 WS-A 产物与目标环境。WS-B、WS-C、WS-D 可以在共同的可运行输入就绪
后按硬件条件交错推进，但不能跳过各自 Exit gate。

## 3. Workstream 总表

| ID | Priority | Outcome | Entry gate | Exit gate | Covers |
| --- | --- | --- | --- | --- | --- |
| WS-A | Now | 生成可追溯的 V851S 交叉构建、链接、安装和 rootfs package | 明确 Git/worktree、工具链、sysroot、Qt/MPP 来源和许可边界 | 构建和打包可重复；产物绑定源码、工具链、sysroot、manifest 和配置 | ARCH-001～003、BUILD-001～003、HW-001 |
| WS-B | Next | Camera Preview、Snapshot、Record、RTSP 形成目标板可用行为 | WS-A 提供可运行 MPP 产物；sensor/ISP/LCD/存储/网络可用 | 纳入的子能力达到需求行为；录像中 Snapshot 被拒绝且专用录像态 UI 正确收敛；失败可回到 Preview/Idle | ARCH-004、MPP-001/002、CAM-001～009、GUI-008、AUDIO-001 |
| WS-C | Next | 相册目录/照片与 MPP Playback 状态、局部显示及实际需要的媒体组合得到确认 | WS-A 提供可运行 Qt/MPP 产物；VO、存储目录和用户指定媒体样本可用 | 进入页面/媒体产出后的目录刷新、手动前后切换、照片显示、局部视频显示及需要支持的组合绑定样本和结果；失败组合被排除或修复 | MPP-001/002、PLAY-001～007、AUDIO-001 |
| WS-D | Now | 800×480 Qt UI 与 MPP video layer 在 DISP2 上可靠合成且 owner 不重叠 | 源码门禁只依赖 Accepted owner 契约；目标出口需 WS-A 产物和 DISP2 环境 | Camera 全画布与 Album 实际子矩形的映射、透明、层级和重启行为得到确认；OPEN-012 关闭 | GUI-001～003、GUI-005～007、YOLO-003、DISP-001～008、OWN-002、OPEN-012 |
| WS-E | Now | 首页 OpenCV 入口在外层会话监督下完成 MPP/Qt 与 YOLO 双向交接并恢复唯一 owner/writer | 源码实现需 WS-D owner 契约；目标出口需双方可运行产物 | 双向切换和异常恢复无设备、buffer、线程、fb 或 layer 泄漏；Camera 检测占位不绕过互斥 | ARCH-002/003、GUI-005/007、YOLO-001～003、OWN-001～003 |
| WS-F | Later | 当前产品范围通过综合验收并具备发布追溯 | WS-B～E 和 WS-UVC 达到各自出口 | 长稳、故障、性能、升级、许可和发布条件满足，可标记 Product accepted | 全部当前 Accepted requirements |
| WS-UVC | Now | 板卡作为 UVC Device，经 Host 协商按固定格式输出摄像头画面，并由 800×480 GUI 后端状态驱动控件，在断连/模式切换后完整回收 | UVC-000～004 已 Accepted；源码阶段可使用固定 sample，目标出口需 WS-A 产物和系统预配置 gadget node | GUI 不再使用本地动画状态；PROBE/COMMIT、全部格式、STREAMON/OFF、断连/重连、bounded buffer 和模式切换得到确认；不以 ADB composite 兼容作为出口 | MPP-001/002、GUI-004/005、UVC-000～004 |

具体验证方式、设备、循环次数、样本和通过阈值由主维护者在启动相应工作时指定，不固化
在 roadmap 中。

## 4. Open decision 路由

| Open | Workstream | Workstream 负责产出 | Decision owner |
| --- | --- | --- | --- |
| OPEN-012 | WS-D | DISP2 映射、透明合成和重启事实 | [`requirements.md`](requirements.md) |

Workstream 可以产生事实和建议，不能自行把 Proposed 改成 Accepted。

## 5. 从 Workstream 形成工作项

开始会改变实现或能力结论的工作时，在当前协作会话或主维护者使用的外部跟踪工具中
明确：

1. 主 Workstream 和覆盖的 requirement/Open；
2. Entry gate 是否满足；
3. 本次可观察目标和非目标；
4. 修改的事实 owner、风险和止损方式；
5. 用户指定的检查范围，以及哪些事项本次不确认。

命令、日志、实现步骤和临时调查不回写 roadmap；长期结论直接进入对应事实 owner。

## 6. 更新规则

| 变化 | 是否更新本页 | 应更新的位置 |
| --- | --- | --- |
| Priority、依赖、Outcome、Entry/Exit gate 变化 | 是 | roadmap；必要时记录产品决定 |
| requirement/Open Decision 变化 | 只更新 Covers/路由 | requirements 是 Decision owner |
| 实现完成、失败或出现新阻塞 | 否 | STATUS 和对应 component |
| 取得影响当前能力的新结果 | 否 | STATUS；稳定限制进入 architecture/component |
| 配置、类、Pipeline 或检查命令变化 | 否 | config/components 或当前协作记录 |

更新本页时必须同步“最近复核”；不得使用 `Now/Next/Later` 推断当前完成度。
