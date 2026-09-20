# Architecture Decision Records

| ADR | 状态 | 决策 |
| --- | --- | --- |
| [`0001`](0001-unified-build-multiple-processes.md) | Accepted | 统一构建、多个进程 |
| [`0002`](0002-single-mpp-service-multiple-modes.md) | Accepted | 单一 MPP 服务、互斥 Pipeline |
| [`0003`](0003-yolo-mpp-mutual-exclusion.md) | Accepted | YOLO 与 MPP 严格互斥 |
| [`0004`](0004-out-of-tree-development-sdk-packaging.md) | Accepted | 树外开发、SDK 薄打包适配 |
| [`0005`](0005-qt-linuxfb-disp2-layer-composition.md) | Accepted | Qt UI 与 MPP video layer 通过 DISP2 合成 |

ADR 说明长期决策及权衡，不维护当前实现成熟度。当前状态见
[`../../STATUS.md`](../../STATUS.md)。

ADR 只固定决策、理由、后果和重新评审条件。配置默认值、channel/layer ID、本地参考
路径、当前类名和检查结论由配置、组件或 STATUS 维护；这些细节变化但决策不变
时，不新增 ADR。改变决策本身时新增 superseding ADR 或明确修订关系。
