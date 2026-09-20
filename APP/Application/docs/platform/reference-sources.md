# 外部参考来源登记

本页只登记 Application 调查和迁移所使用的外部资料：来源、固定版本、本地准备方式、
适用范围以及不能证明的事项。MPP/MPI 用法、sample 选择、显示契约、产品 Pipeline 和
当前确认情况分别由 platform、components 和 STATUS 维护，不在这里复制。

## 1. 登记字段与使用规则

每项外部参考至少记录：Reference ID、上游/来源、版本或哈希、本地位置/准备方法、适用
问题、不能证明的范围、许可/ABI 风险。版本未固定时必须明确写出，不能把本机目录当作
可复现依赖。

使用顺序：

1. 先读项目内的事实 owner 和实际构建使用的 sysroot/BSP 头文件；
2. 只有现有资料存在明确缺口时，按 Reference ID 定向查看外部内容；
3. 记录采用了哪些接口事实、与当前 V851S 基线的差异和仍需验证项；
4. 外部内容进入源码、构建、bundle 或目标系统前，单独确认许可、来源、ABI 和授权；
5. sample、其他 SoC 文档和静态符号不能证明 V851S 的性能、并发或板端能力。

## 2. 来源登记

### REF-MPP-CURRENT-SDK：当前 v853-100ask SDK（正式编译依据）

- 来源：`/home/ubuntu/tina-v853-100ask`，目标 v853-100ask、sun8iw21/AW1886；
- 实现：`external/eyesee-mpp/middleware/sun8iw21`；package 目录是构建适配层；
- 导入：`tools/import_sunxi_mpp.py` 读取已构建 staging 产物及本 SDK Linux 4.9；
- ISP 版本标记：`5b30d9b8f8190196d863bc91576af5356022b433`；
- 可复现证据：生成的 SDK_SOURCE.json、SDK_FEATURES.cmake、BUNDLE_MANIFEST、SHA256SUMS；
- 正式 API/ABI 以本来源优先，下面的 Yuzukilizard 仅用于历史差异；
- 不能证明：实际板卡烧录内容、sensor/tuning、性能或 GUI/USB 板端运行。

### REF-MPP-SUN8IW21：Yuzukilizard

- 上游：`https://github.com/ohdarling/Yuzukilizard.git`
- 固定版本：`94bb93ad67fd862c5f6fe6c29fbc0f54950e7107`
- 标准本地位置：`<workspace>/TMP/Yuzukilizard`
- 准备方法：在 `Application/` 执行 `./tools/prepare_references.sh`；脚本使用 sparse
  checkout 展开上游 `Software/`，本地目录可删除后按固定提交重建
- 适用问题：sun8iw21 MPP 头文件、实现、sample、BSP package 组织、camera/ISP/NPU
  邻近资料和工具来源
- 不能证明：当前产品 sysroot 的最终 ABI、目标板 sensor/ISP/DISP2 组合、资源数量、
  性能或稳定性
- 许可/ABI：上游 `Software/sunxi-mpp` 没有可覆盖发布的明确软件许可；预编译库仍与
  ARM/musl/hard-float 和特定 BSP 绑定，正式纳入前必须完成授权与 ABI 审核

`TMP/` 只用于阅读和差异核对，不得成为正式源码或构建必须存在的隐式输入。固定树中
日常相关位置为：

```text
Software/BSP/platform/allwinner/eyesee-mpp/middleware/sun8iw21/
Software/sunxi-mpp/
Software/Samples/
```

UVC Gadget Out 使用同一固定版本中的以下路径，不使用名称相近但用途不同的 sample：

```text
Software/BSP/platform/allwinner/eyesee-mpp/middleware/sun8iw21/
  sample/sample_uvcout/
  include/uvc.h
  include/video.h
```

UVC 协议和产品化差异已归纳到 [`mpp/uvc.md`](mpp/uvc.md)。sample 能证明邻近 BSP
使用的事件/ioctl 顺序，不能证明当前板卡的 UDC descriptor、device node、Host 兼容性
或与 ADB composite function 共存。

### REF-MPP-V821-GUIDE：V821 MPP 开发指南

- 来源/版本：`TMP/html/_sources/board/mpp/Tina_Linux_多媒体MPP_开发指南/`；版本未固定
- 适用问题：MPI 对象、状态、接口分组、参数解释和较新接口演进
- 不能证明：V851S ABI、字段/符号存在、online/缩放/旋转能力、模块数量或性能
- 许可/ABI：只读资料；引用前保留 V821 平台限定，最终接口以实际 sysroot 为准

### REF-MPP-SUN252：sun252iw1 归纳与 demo

- 来源/版本：`TMP/Agent_SKILLS/packages/multimedia/sun252iw1/`；版本未固定
- 适用问题：SYS/VI/VO/VENC/VDEC 模块化阅读、camera/编码/网络线程分层
- 不能证明：`AWVideoInput_*` 等高层 API 属于 sun8iw21 MPI，或当前结构字段、内存语义
  和可链接符号一致
- 许可/ABI：二次归纳和其他 SoC 源码，只用于发现核对项，不直接进入产品依赖

### REF-APP-V853：V853 AI recorder Application

- 来源/版本：`<V853_REFERENCE_ROOT>/V853_AI_recorder/Application`；版本未固定
- 适用问题：capture、display、VENC、record、storage、frame pool、thread queue、
  media sync、G2D 和板级配置的模块化思路
- 不能证明：V851S 的双码流/双 NPU、buffer 数、channel ID、分辨率、库名或 ABI
- 许可/ABI：仅作架构对照；不复制巨大上下文、OpenCV 依赖或 V853 固定参数

### REF-QT-OVERLAY：Qt 5.12.9 外置 overlay

- 来源/版本：`TMP/qt_project` 与 `TMP/qt_project-v851s-qt5.12.9-overlay.tar`；源码包
  版本为 Qt 5.12.9，外层制品哈希应在实际使用记录中填写
- 适用问题：QtBase、linuxfb、smoke test、SDK apply 输入和移植步骤
- 不能证明：当前 rootfs 已安装、目标 framebuffer/layer 映射正确或 GUI 已通过板测
- 许可/ABI：使用前复核 Qt/LGPL、补丁来源、工具链和目标 rootfs ABI

### REF-DISP2：DISP2 指南、源码与 MPP UI sample

- 来源/版本：`TMP/sdk_disp`，以及 REF-MPP-SUN8IW21 中的 `sample_UILayer`/`mpi_vo.c`；
  版本未独立固定
- 适用问题：DISP2 `CONFIG2`、z-order、alpha、framebuffer 映射和 MPP outside layer 语义
- 不能证明：V851S 实际 handle、Qt framebuffer 格式/旋转或板端合成行为
- 许可/ABI：`TMP/sdk_disp` 的预编译 `.so/.o` 不直接作为产品依赖；缺少完整头文件和
  构建闭包时只能参考指南/源码

### REF-UI-20260915：AI Vision Module Qt GUI

- 来源/版本：用户提供的
  `TMP/NewUI/AI_Vision_Module_UI_v1.0.0_20260915.zip`；SHA-256
  `f20d2f002e63c34687fef35101bda20e24084874f9e38c10e29e074ff1abaca6`
- 适用问题：800×480 Home/Camera/Album/UVC 页面视觉、1600×960 设计坐标、自绘图标和
  Qt Widgets 控件布局
- 产品化处理：只迁入 `src/dashboard.*`、`src/pages/` 和 `src/ui/` 的绘制源码，再由
  Application 的 `MainWindow` 接既有 IPC/监督；Windows Qt6 runtime、DLL、独立 main、
  构建/部署脚本、截图和测试不进入产品 target
- 不能证明：Qt 5.12 目标构建、linuxfb alpha、DISP2 合成、目标字体/触摸和媒体功能
- 许可/ABI：该包由用户声明为自己设计的 GUI；若未来对外发布，仍应由主维护者确认
  视觉资产/字体许可。产品继续绑定 Qt 5.12，不使用包内 Qt6 DLL

### REF-VO-REGION-GUIDE：VO 指定区域显示说明

- 来源/版本：用户提供的 `TMP/NewUI/VO指定区域显示使用说明.md`；SHA-256
  `f5f14784d7a5394fa0182dfe21f1a38fb8bce8e13ae778ca5fb6621e53210c75`；内容明确针对
  V821 `sun300iw1`
- 适用问题：`GetVideoLayerAttr -> 修改 stDispRect -> SetVideoLayerAttr` 的屏幕目标矩形
  语义、边界和 YUV 偶数对齐核对
- 不能证明：V851S layer handle、priority、alpha、动态重配、缩放能力或目标板表现；文档
  中 HLAY 示例不得复制成当前配置
- 许可/ABI：只读用户资料；最终字段和符号以 REF-MPP-CURRENT-SDK 同源 bundle 与目标 sysroot
  为准。Application 当前只复用既有 video layer，不采用文档的多视频 layer 方案

## 3. 进入项目文档的路由

| 要解决的问题 | 项目内首要入口 |
| --- | --- |
| SYS、VI、VO、VENC、VDEC、SmartIPC、UVC gadget 接口和常见 sample 缺口 | [`mpp/`](mpp/) |
| `camera-mpp-service` 的配置、实例 ID、Pipeline、回滚和技术缺口 | [`../components/camera-mpp-service/`](../components/camera-mpp-service/) |
| Qt/linuxfb、DISP2 与 MPP VO owner/合成契约 | [`display/`](display/) |
| 仓库内 MPP 迁移参考目标和实际目录 | [`../components/mpp-reference-tools/README.md`](../components/mpp-reference-tools/README.md)；源码在 [`../../src/component/MPP/`](../../src/component/MPP/) |
| standalone bundle 的依赖、符号、ABI 和许可风险 | [`../components/mpp-reference-tools/README.md`](../components/mpp-reference-tools/README.md) |
| 当前实现、已确认事实和能力 | [`../STATUS.md`](../STATUS.md) |

新增外部来源时先补全登记字段；如果其知识已被项目手册吸收，本页只保留来源和边界，
具体调用顺序、sample 映射或产品选择继续由对应领域 owner 维护。

## REF-QT-OPENCV-CURRENT-SDK

当前 SDK 集成入口为 [SDK overlay](../../../../02_SDK_OVERLAYS/README.md)，APP 开发依赖位于 [sdk-dev](../../../sdk-dev)。Qt 输入是用户提供的
Qt 5.12.9 overlay；OpenCV package 固定 TinyVision commit
356b94946a4887b721423f41711c7bc94fe74a18，版本 4.1.0、contrib 4.1.0、ADE 0.1.1d。
OpenCV 五个原始补丁保持不变，当前 SDK 的额外适配单列 0005 补丁；ISP adapter 只链接
当前 MPP bundle，避免再次引入旧 ISP 与当前内核 ioctl ABI 不一致。
