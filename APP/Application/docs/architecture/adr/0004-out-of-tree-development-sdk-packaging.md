# ADR-0004：树外开发与 SDK 薄打包适配

- 状态：Accepted
- 日期：2026-09-11
- 修订：2026-09-13，移除工作区临时路径，保持源码与打包边界不变

## 背景

历史代码散落在 TinaSDK package、MPP demo 和 Application 中，容易出现重复源码、
不同 Makefile 和 SDK 路径依赖。MPP 仍依赖 V851S BSP/驱动，但应用开发不需要每次
进入整个 SDK 构建树。

## 决策

- `Application/` 是 GUI、MPP service、YOLO worker 和共享模块的权威源码；
- 使用 `cmake -S Application -B <build-dir>` 做 out-of-source build；
- standalone 工具链/sysroot 用于日常开发和 CI；
- Tina/OpenWrt 中只建立薄 package adapter，调用同一套 Application CMake；
- BSP 仍负责内核、设备树、ISP tuning、rootfs、USB Gadget 和设备权限；
- Qt 运行时、外部 MPP 参考和 vendor 二进制以固定版本、manifest 或校验值准备，
  但不成为 Application 业务源码的第二份 owner。

## 原因

- 避免 Application 与两套 DVP/MIPI package 继续分叉；
- 允许独立静态分析、单组件构建和 CI；
- 仍保留生成完整固件和部署 BSP 配置的正确入口；
- 工具链、sysroot、外部提交和依赖 manifest 可以显式固定。

## 后果

- 需要维护 standalone toolchain 和可重现 sysroot；
- SDK package adapter 必须传递明确的功能开关和安装路径；
- Qt 大源码归档不进入普通 Git blob，使用已校验的本地制品/Release/LFS 准备；
- “不依赖 TinaSDK 构建树”不等于不依赖 V851S BSP/ABI；
- 未经用户明确授权，普通 Application 任务不得顺手修改 SDK。

当前工具命令、参考树位置和 bundle 准备方法属于开发/构建事实，分别见 Application
根 README 和 [`../../platform/reference-sources.md`](../../platform/reference-sources.md)，
不由本 ADR 固定本地路径。

## 重新评审条件

只有 standalone 工具链无法表达必要的 vendor 构建步骤，且该限制有可复现证据
时，才评审将某个步骤放回 SDK；即使如此，Application 源码仍应保持唯一来源。
