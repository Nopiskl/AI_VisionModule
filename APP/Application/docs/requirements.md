# Application 产品需求

- 文档角色：产品范围、可观察行为、交付约束和当前待决问题的唯一规范
- 最近复核：2026-09-15
- 当前成熟度：[`STATUS.md`](STATUS.md)

本页只回答“产品必须满足什么”。架构理由、组件实现、计划顺序和当前确认情况分别由
[`architecture/`](architecture/)、[`components/`](components/)、
[`roadmap.md`](roadmap.md) 和 [`STATUS.md`](STATUS.md) 维护。

## 1. 固定字段与 Decision

正式 requirement 使用固定字段：

| 字段 | 含义 | 更新规则 |
| --- | --- | --- |
| ID | 稳定追踪标识 | 不复用；被取代后在本页“历史 ID 去向”保留映射 |
| 类型 | Functional / Safety / Ownership / Scope 等 | 只用于分类，不表示实现状态 |
| 规范要求 | 必须满足的产品行为或约束 | 语义变化需要用户明确确认 |
| Decision | `Accepted` 或 `Deferred` | 不能因实现失败或缺少板测自行降级 |
| 验收入口 | 可观察的确认范围 | 不固定命令；具体方法由主维护者在任务中指定 |

| Decision | 含义 |
| --- | --- |
| `Accepted` | 当前交付范围内必须满足 |
| `Deferred` | 保留长期价值，但不进入当前构建、capability 或验收 |
| `Proposed` | 尚未决定，只用于 Open；不能据此承诺产品行为 |
| `Superseded` | 已被取代；从当前表移除，在本页保留原决定和去向 |

实现、已确认事实和 Capability 不写入本页；统一查看 [`STATUS.md`](STATUS.md)。

## 2. 当前产品范围

- 以 800×480 为当前产品显示画布的 Qt GUI、后端进程监督和版本化 IPC；
- GUI 首页提供 Camera、UVC、OpenCV 三个入口；Camera 内提供透明 overlay、相册和
  RTSP 状态展示，OpenCV 入口交接给独立 YOLO worker；
- 单一 `camera-mpp-service` 中互斥的 Camera、Playback 与 UVC Out；
- Camera 常驻 Preview，以及按需 Snapshot、Record 和可选单路 RTSP；
- 与 MPP 服务和 Qt UI 子进程互斥、直接输出 framebuffer 的独立 YOLO worker；
- Camera/Playback 中 Qt UI layer 与 MPP video layer 的 DISP2 分层显示，以及
  Qt→YOLO→Qt 的 framebuffer 时分交接。
- UVC Out 中由已配置好的 Linux UVC gadget video 节点向 USB Host 输出摄像头画面；
  Qt UI 保持运行，UVC 不创建 MPP VO 图层。

UVC 与 ADB 的 USB composite gadget 兼容不进入当前阶段；音频也不进入当前阶段。
范围成立不表示能力已经实现或板测。

## 3. 产品架构与交付

设计理由见
[`ADR-0001`](architecture/adr/0001-unified-build-multiple-processes.md) 和
[`ADR-0004`](architecture/adr/0004-out-of-tree-development-sdk-packaging.md)。

| ID | 类型 | 规范要求 | Decision | 验收入口 |
| --- | --- | --- | --- | --- |
| ARCH-001 | Architecture constraint | 使用统一仓库和统一顶层 CMake，生成多个职责清晰的可执行程序。 | Accepted | ADR-0001；交叉构建、安装和打包 |
| ARCH-002 | Architecture constraint | 产品进程至少包括 `camera-gui`、`camera-mpp-service`、`camera-yolo-worker`。 | Accepted | ADR-0001；安装清单 |
| ARCH-003 | Architecture constraint | GUI 不直接链接 MPP、OpenCV 或 VIPLite。 | Accepted | 依赖/ELF 审核 |
| ARCH-004 | Architecture constraint | 最终只允许一个产品级 MPP owner；不得用多个 sample 进程组合产品能力。 | Accepted | ADR-0002；owner 切换验证 |
| BUILD-001 | Delivery constraint | `Application/` 是权威源码，开发采用树外源码和 out-of-source build。 | Accepted | ADR-0004；可复现构建 |
| BUILD-002 | Delivery constraint | Tina/OpenWrt 只提供工具链、BSP、rootfs 和薄 package adapter，不维护第二份业务源码。 | Accepted | ADR-0004；package 审核 |
| BUILD-003 | Supply-chain constraint | 外部 MPP 参考和授权未确认的预编译库不得直接提交或重新分发。 | Accepted | manifest、哈希和许可审核 |

## 4. MPP 与 Camera

稳定模式边界见
[`ADR-0002`](architecture/adr/0002-single-mpp-service-multiple-modes.md)；具体实例和参数见
[`pipelines.md`](components/camera-mpp-service/pipelines.md)、
[`configuration.md`](components/camera-mpp-service/configuration.md) 与运行配置。

| ID | 类型 | 规范要求 | Decision | 验收入口 |
| --- | --- | --- | --- | --- |
| MPP-001 | Architecture constraint | `camera-mpp-service` 是唯一 MPP owner；当前提供 Camera、Playback、UVC Out 和 Camera 内可选 RTSP。UVC 不得以独立 sample 进程形成第二个 MPP owner。 | Accepted | ADR-0002；MPP 生命周期审核 |
| MPP-002 | Safety constraint | Camera、Playback 与 UVC Out 顶层 Pipeline 严格互斥，切换前必须完整停止并释放当前 Pipeline。 | Accepted | 反复模式切换和资源回收 |
| CAM-001 | Functional | Camera 模式正常运行时持续提供 LCD Preview。 | Accepted | 目标板 Preview 画面、启停和恢复 |
| CAM-002 | Functional | 用户可由 Qt 拍照操作触发单张 JPEG；拍照不得重启 Preview。 | Accepted | JPEG 输出和 Preview 连续性 |
| CAM-003 | Functional | 用户可由 Qt 录像操作开始和停止录像；停止录像不得退出 Preview。 | Accepted | 录像文件和 Preview 连续性 |
| CAM-004 | Product constraint | Camera 录像编码分辨率固定为 1280×720；Preview 显示尺寸与录像编码尺寸独立配置。 | Accepted | 配置核对和目标板 Camera 行为 |
| CAM-005 | Functional | 首版录像由用户显式开始/停止并生成单个 H.264/MP4 文件，不按时间或大小分段，也不循环覆盖。帧率和码率由配置 owner 管理，目标板确认前不构成能力承诺。 | Accepted | 录像文件及媒体属性 |
| CAM-006 | Functional | RTSP 是 Camera 内可启停的单路 H.264 视频输出，不创建第二个 camera owner；首版不提供音频数据。网络和媒体参数由配置 owner 管理。 | Accepted | RTSP SDP、重连和并发资源 |
| CAM-007 | Safety | 录像前必须确认可移动媒体根是独立真实挂载点；录像期间按配置监测挂载和容量，拔卡或空间低于保留阈值时必须受控停止。程序不得自动 mount、强制 umount、格式化或删除旧文件。 | Accepted | 满盘、拔卡和 I/O 故障恢复 |
| CAM-008 | Recoverability | MP4 MUX 默认写 repair metadata；异常提交失败时保留 `.part` 供离线恢复。未经恢复工具和目标文件系统验证，不得宣称自动断电恢复。 | Accepted | 断电、文件系统和恢复工具 |
| CAM-009 | Product constraint | 录像期间不允许拍照；服务端必须拒绝 `take_snapshot`，GUI 不得保留或提供拍照入口。该限制是产品行为，不再以 VENC/CMA 并发能力作为开放条件。 | Accepted | IPC 拒绝、录像态 GUI 和目标板交互 |

`CAM-004` 是 Accepted 产品要求，不是普通配置默认值；改变固定 1280×720 需要用户
重新决定。其他机器默认值只看运行配置。

## 5. Playback 与音频

| ID | 类型 | 规范要求 | Decision | 验收入口 |
| --- | --- | --- | --- | --- |
| PLAY-001 | Architecture constraint | Playback 使用 MPP DEMUX/VDEC/CLOCK/VO 视频路径播放本地文件。 | Accepted | Pipeline 核对和目标板播放 |
| PLAY-002 | Functional | Playback VO 输出画布读取产品显示配置；输入视频保持编码尺寸，由 VO 按显示策略在画布内缩放。 | Accepted | 显示配置和实际播放画面 |
| PLAY-003 | Architecture constraint | Qt 只提供播放控件、状态展示和 IPC，不使用 Qt Multimedia 或 Qt 软件解码作为产品播放后端。 | Accepted | GUI/依赖审核 |
| PLAY-004 | Scope | 首版控制范围为 load/play/pause/resume/stop/EOF/error；seek、自动连续播放/队列管理、倍速和字幕不进入当前阶段。 | Accepted | IPC 和实际状态转换 |
| PLAY-005 | Safety | 创建 VDEC 前必须按配置执行 codec、尺寸、帧率、码率和 B-frame 门禁；门禁是保守候选，不是支持宣称，实际组合必须由目标板结果确认。 | Accepted | 配置门禁、媒体样本和目标板结果 |
| PLAY-006 | Functional | 相册扫描服务声明的产品输出目录 `storage_root`，在进入页面及媒体产出完成后按时间刷新产品生成的 JPEG/MP4；用户可以选择和手动切换上一项/下一项，但这不构成自动播放列表或队列管理。 | Accepted | 媒体目录样本、Qt 交互和 IPC |
| PLAY-007 | Functional | 相册照片由 Qt 在预览框内等比例显示；视频仍由 MPP Playback 在配置的 VO 子矩形内显示，Qt 对该子矩形保持透明并仅绘制框外控件。 | Accepted | Qt 截图、VO layer 属性和目标板合成 |
| AUDIO-001 | Scope | 当前阶段不播放或录制音频，不引入 ADEC、AO 或 A/V sync。 | Accepted | 依赖和运行路径审核 |

## 6. GUI、显示、YOLO 与所有权

进程隔离、YOLO 互斥和显示分层的理由见
[`ADR-0001`](architecture/adr/0001-unified-build-multiple-processes.md)、
[`ADR-0003`](architecture/adr/0003-yolo-mpp-mutual-exclusion.md)、
[`ADR-0005`](architecture/adr/0005-qt-linuxfb-disp2-layer-composition.md)；平台契约见
[`linuxfb-disp2.md`](platform/display/linuxfb-disp2.md)。

| ID | 类型 | 规范要求 | Decision | 验收入口 |
| --- | --- | --- | --- | --- |
| GUI-001 | Functional | Camera 页面提供“拍照”、“录像”和按 capability 启用的“RTSP”三个 Qt 控件。 | Accepted | Qt 构建和目标板交互 |
| GUI-002 | Architecture constraint | Qt 控件只发送版本化 IPC；GUI 不直接调用 MPP 或 DISP2 视频输出接口。 | Accepted | IPC/依赖审核 |
| GUI-003 | Functional | 控件的 busy、recording 和 error 状态以后端确认事件为准，不因用户点击直接假定成功。 | Accepted | 后端故障、断线和恢复 |
| GUI-004 | Functional | GUI 提供 UVC 模式入口，并展示等待 Host、已连接、已 COMMIT、正在传输和故障状态；GUI 只通过 IPC 切换模式，不直接操作 UVC gadget 或 MPP。 | Accepted | Qt 构建、IPC 和目标板交互 |
| GUI-005 | Functional | 首页固定提供 Camera、UVC、OpenCV 三个入口。非录像态返回首页只切换导航页面，不隐式停止当前 Camera/RTSP/UVC；用户进入另一功能时再通过既有互斥切换收敛，首页必须提示当前仍活动的后端模式。Recording 按 GUI-008 锁定 Camera 页面，不能返回首页。 | Accepted | Qt 交互、IPC 日志和模式切换 |
| GUI-006 | Functional | Camera 页面 RTSP 开关以后端确认状态为准；启用成功后在页面上方状态栏显示后端返回的实际 RTSP URL，停止、断线或错误后同步清除或标记状态，不得由 GUI 猜测地址。 | Accepted | RTSP 事件、get_status 和 Qt 展示 |
| GUI-007 | Scope | Camera“检测”按钮本阶段保留为可见但禁用的延期入口；它不得启动与 MPP Camera 并发的 YOLO。OpenCV 首页入口才触发完整 Qt→YOLO→Qt 互斥会话交接。 | Accepted | Qt 控件状态与进程生命周期 |
| GUI-008 | Functional | 后端确认进入 `camera/recording` 后，GUI 必须锁定 Camera 页面，隐藏拍照、RTSP、检测、相册和返回主页等其它操作，只保留 iOS 风格的停止录像按钮；顶部显示后端单调时钟给出的已录时长。停止成功或异常中断后恢复普通 Camera 控件。 | Accepted | IPC 状态/计时字段、Qt 录像态交互和异常恢复 |
| GUI-009 | Functional | 主页提供“退出程序”。退出时先请求 MPP shutdown 并等待媒体收尾，再销毁 Qt/linuxfb；由 GUI 清除自身 framebuffer 内容并通过当前 BSP 的 FBIOBLANK 关闭 UI 层，后续较低层程序不得被残留 Qt 图像遮挡。SIGTERM/SIGINT 使用同一关闭路径。录像时先停止录像返回主页后退出。 | Accepted | 退出按钮、进程退出、UI layer 状态、后续 sample 显示 |
| DISP-001 | Platform constraint | 第一阶段 Qt 5.12.9 使用 `linuxfb` QPA。 | Accepted | Qt 构建与板端启动 |
| DISP-002 | Ownership | Camera/Playback/UVC 会话中 Qt 是 UI framebuffer 的唯一 writer，MPP 不得写或 mmap；YOLO 会话中必须先退出 Qt UI 子进程，再由 YOLO worker 独占同一 framebuffer。 | Accepted | ADR-0005、进程交接和目标板显示 |
| DISP-003 | Functional | Camera/Playback 会话中 Qt UI layer 位于 MPP VO video layer 之上，由 DISP2 完成硬件合成。 | Accepted | layer 门禁和目标板透明合成 |
| DISP-004 | Ownership | MPP 只管理视频层；除 Add/RemoveOutside 内部登记外，不得改变 Qt UI layer。 | Accepted | 源码检查和反复 MPP 启停 |
| DISP-005 | Evidence constraint | UI layer 的透明像素格式、alpha、channel/layer ID 和 z-order 必须在目标板确认。 | Accepted | DISP2 查询和目标板透明合成 |
| DISP-006 | Product constraint | 显示配置独立管理 VO interface/sync/x/y/width/height；更换显示画布不得改变录像编码尺寸。 | Accepted | 配置核对和不同显示画布 |
| DISP-007 | Ownership | Camera/Playback/UVC 会话启动 MPP 前必须查询 linuxfb 的实际 DISP2 handle，并确认它与配置的 MPP video handle 不同且等于 outside-layer 配置；服务只在 Camera/Playback 创建/销毁 video handle，对 UI handle 只作 outside-layer 登记。 | Accepted | 启动门禁、DISP2 handle 查询和 MPP 重启 |
| DISP-008 | Product constraint | 当前 Qt 逻辑界面为 800×480，LCD/VO 物理画布为 480×800，Qt rotation=90。按用户最新指定参数，VIPP0/ISP0 预览采集 480×800，video layer 0 直接铺满 (0,0,480,800)。录像/拍照/RTSP 使用同一 ISP 的独立 VIPP4 1280×720 输出（按需启用）；录像编码仍固定 1280×720。相册继续使用物理子矩形和 Qt 坐标映射。 | Accepted | 配置、IPC、V4L2/VO 及目标画面 |
| YOLO-001 | Architecture constraint | YOLOv8 使用独立 OpenCV/V4L2 + VIPLite worker。 | Accepted | ADR-0003；独立构建 |
| YOLO-002 | Safety constraint | YOLO worker 与整个 MPP 服务不能同时运行；两者必须使用同一个跨进程 backend lock。 | Accepted | 双向 owner 切换和异常恢复 |
| YOLO-003 | Functional | GUI 首页的 OpenCV 入口进入现有 YOLO 会话；进入时停止 MPP 并销毁 Qt UI 会话以释放 framebuffer，YOLO 退出后由外层会话监督器重建 Qt UI 和 MPP。 | Accepted | Qt→YOLO→Qt 进程生命周期与异常退出 |
| OWN-001 | Ownership | 任一时刻只有一个 backend 持有 sensor/V4L2/ISP。 | Accepted | backend lock/板端切换 |
| OWN-002 | Ownership | Camera/Playback 中 Qt UI layer 与 MPP video layer 不得重叠；UVC 中 Qt 保持 UI layer 且 MPP 不创建 VO；YOLO 中 Qt/MPP 均不存在，由 worker 临时独占 framebuffer。任一交接阶段最多只有一个 framebuffer writer。 | Accepted | ADR-0005、layer 查询和会话切换 |
| OWN-003 | Safety constraint | backend 切换必须等待 `stopped`、进程或深度 Idle，并确认跨进程锁已释放。 | Accepted | 反复切换和故障恢复 |

## 7. UVC Out 与硬件证据门禁

| ID | 类型 | 规范要求 | Decision | 验收入口 |
| --- | --- | --- | --- | --- |
| UVC-000 | Scope | UVC Out 进入当前实现和构建范围；`camera-mpp-service` 使用预先配置并绑定好的 UVC gadget video 节点。本阶段不创建/修改 configfs gadget，也不要求与 ADB composite function 共存。 | Accepted | 配置、依赖和进程权限审核 |
| UVC-001 | Functional | 板卡作为 USB UVC Device 向 PC Host 输出板载摄像头画面；进入 UVC 模式只打开 gadget 并等待 Host，isochronous 模式收到有效 STREAMON 后、bulk 模式完成有效 COMMIT 后才创建采集/编码路径。 | Accepted | Host 枚举、画面和延迟观察 |
| UVC-002 | Protocol constraint | UVC Pipeline 必须处理 Host CONNECT/DISCONNECT、PROBE/COMMIT、STREAMON/STREAMOFF；断连或 STREAMOFF 必须停止数据线程、归还 frame/stream、释放 mmap buffer 和全部 ISP/VI/VENC 资源，同时保留 UVC 模式等待重连。 | Accepted | 协商、断连/重连和资源回收 |
| UVC-003 | Functional | 首版格式索引与 `sample_uvcout` 一致：MJPEG 为 1920×1080、1280×720、640×480 @30 fps；YUYV 为 320×240 @30 fps；H.264 为 1920×1080、1280×720 @30 fps。实际 configfs descriptor 必须以相同索引公开；能力声明仍受目标板确认门禁约束。 | Accepted | Host 格式枚举、逐格式取流和媒体属性 |
| UVC-004 | Safety constraint | Gadget mmap 输出缓冲和应用帧队列必须有界；任何成功取得的 VI frame 或 VENC stream 都必须在同一迭代释放，慢 Host 通过丢弃旧帧收敛，不得形成无界积压。 | Accepted | 源码审核、慢消费者和长稳观察 |
| HW-001 | Evidence gate | 芯片资源细节不阻塞静态设计和源码工作；作出产品能力承诺前必须补齐实际硬件事实。 | Deferred | 工具链、BSP、板卡和测量环境就绪 |

UVC 模式不使用 MPP VO，因此 Qt 保持 framebuffer/UI layer owner；Camera/Playback/UVC
之间仍先完整释放旧 Pipeline 再创建新 Pipeline。ADB composite gadget 兼容需要单独的
产品决定和 USB descriptor/枚举验证，不能从本轮 UVC 逻辑实现外推。

## 8. Open decisions

Open 使用固定字段 `ID / 待决问题 / Decision / 影响范围 / 关闭所需证据或决定`：

| ID | 待决问题 | Decision | 影响范围 | 关闭所需证据或决定 |
| --- | --- | --- | --- | --- |
| OPEN-012 | V851S 实际 framebuffer 格式、当前 0/1 layer handle 与 framebuffer 的映射、alpha 和 z-order 范围是什么？ | Proposed | Qt/MPP 显示验收 | DISP2 查询、Qt 透明合成和 MPP 重启结果 |

Agent 可以收集事实，但不能自行把 Proposed 改成 Accepted。关闭或被取代的 Open 从当前
规范视图移除，其 ID 与去向保留在下一节。

主维护者决定 OPEN-012 保留至实机验证阶段，通过 DISP2 查询和实际透明合成结果关闭；
当前源码门禁或配置值不能提前替代该结果。

## 9. 历史 ID 去向

以下 ID 已关闭，不再是当前 Open；保留映射是为了防止旧讨论中的 ID 失去语义：

| 原 ID | 当前事实去向 |
| --- | --- |
| `OPEN-001` | CAM-006、ADR-0002：保留 RTSP |
| `OPEN-002` | AUDIO-001：音频延期 |
| `OPEN-003` | CAM-009、GUI-008：用户决定录像期间不允许拍照，并采用仅保留停止按钮和后端已录时长的专用录像态 UI |
| `OPEN-004`、`OPEN-005` | UVC-000～004：用户于 2026-09-14 恢复 UVC Out；ADB composite 兼容明确留在当前非目标 |
| `OPEN-006` | DISP-001～004、ADR-0005：linuxfb/DISP2 分层 |
| `OPEN-007` | CAM-005/006 和 Service 配置：媒体默认值由配置 owner 管理 |
| `OPEN-008` | HW-001：能力承诺前补齐资源事实 |
| `OPEN-009` | 当前非目标：不设计实体按键 |
| `OPEN-010` | CAM-005/007/008：单文件、存储保护和 repair metadata |
| `OPEN-011` | PLAY-005：候选门禁与目标板媒体组合确认 |

## 10. 当前非目标

- YOLO 与 MPP 同时运行、YOLO 与 Qt 同时写 framebuffer、共享 Camera frame 或同时打开
  同一 sensor；
- Qt、OpenCV、VIPLite 和 MPP 单体链接，或用多个 sample 进程组合产品功能；
- UVC 与 ADB composite function 的共存、由产品服务创建/修改 configfs gadget，或实现
  Camera/Playback/UVC 无缝热切换；
- 音频、Playback seek/自动连续播放与队列管理/倍速/字幕，或 Qt Multimedia/GStreamer
  软件播放；相册的手动上一项/下一项不属于这里的播放列表；
- 自动 mount、强制 umount、格式化 SD 卡或容量不足时自动删除旧录像；
- 将 repair metadata 宣称为已具备自动断电修复；
- GPIO、ADC 或 Linux input 实体拍照/录像按键；
- 将 `TMP/sdk_disp` 预编译库/对象直接作为产品依赖；
- 从其他 SoC/sample 外推 V851S 的分辨率、帧率、并发和性能能力；
- 在授权确认前重新分发 MPP/VIPLite 二进制；
- 依赖 sample `main()`、全局变量或 signal handler 构建产品服务。

## 11. 追踪与更新规则

```text
requirement ID
-> ADR / architecture（稳定设计与理由）
-> roadmap workstream（顺序、依赖和出口）
-> component / interface / source（一次实现）
-> 用户指定的实际检查
-> STATUS（当前 Implementation / Confirmed / Capability）
```

| 变化 | 更新本页 | 其他 owner |
| --- | --- | --- |
| 产品行为、范围、非目标或 Open 决定变化 | 必须；Accepted 语义需用户确认 | 必要时更新 ADR/architecture |
| 组件类、Pipeline、配置默认值或实现变化 | 否，除非改变产品行为 | components、配置、源码 |
| Workstream 顺序或阶段出口变化 | 否 | roadmap |
| 构建、静态检查或板端取得新结果 | 否 | 影响能力/阻塞时更新 STATUS |
| 实现失败或硬件暂未通过 | 不降低 requirement | component/STATUS 记录差距和阻塞 |

只有当前范围的 Accepted requirement 均有实现和用户认可的结果，单能力、综合切换、
长稳、故障、构建打包和许可出口全部满足后，STATUS 才能标记 Product accepted。
