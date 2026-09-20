# MPP 迁移参考工具

本页说明 `Application/src/component/MPP/` 下从 sample 提取的可选参考目标。它们用于
依赖验证、MPI 生命周期核对和迁移实验，不是产品运行时组件，也不能组合成产品功能。

源码入口：[`../../../src/component/MPP/`](../../../src/component/MPP/)。

## 参考目标

| 源码目录 | 构建目标/产物 | 参考用途 |
| --- | --- | --- |
| `preview/` | `camera-mpp-preview` | VI/ISP 到 VO 的最小预览路径 |
| `rtsp/` | `camera-mpp-rtsp` | VI/VENC 与 RTSP sink 的迁移参考 |
| `preview+encoder/` | `camera-mpp-recording-reference` | VI/VENC/MUX 录像路径参考 |

这些目标仅在显式启用 `APPLICATION_BUILD_MPP_EXAMPLES` 时参与构建。它们可以保留随
sample 带来的原始 `Readme.txt`，但原始说明中的设备节点、参数、网络地址和资源数量
都不是产品默认值或 V851S 能力证明。

## 与产品 Service 的边界

- 产品唯一 MPP owner 是 `camera-mpp-service`；它的实际设计见
  [`../camera-mpp-service/`](../camera-mpp-service/)。
- 不允许同时启动多个参考目标占用同一 sensor，也不允许用多个参考进程实现
  Preview + Record + RTSP。
- 从参考目标提取到产品代码时，必须重新建立配置校验、资源账本、幂等 stop、失败
  rollback、有界队列和 Get/Release 配对。
- MPP/MPI 接口优先读取 [`../../platform/mpp/`](../../platform/mpp/) 并核对实际
  sysroot；sample 的外部来源、固定版本和适用边界见
  [`../../platform/reference-sources.md`](../../platform/reference-sources.md)。

## Standalone bundle 边界

正式依赖由 `tools/import_sunxi_mpp.py` 从当前 v853-100ask SDK 导入。
[third_party/README.md](../../../third_party/README.md) 维护生成方式与来源。

可选录像参考已使用同一 SDK 的 sample_vi2venc2muxer 和 sample_common_venc。
RTSP 参考中的旧 product_mode/sensor_type 和 ALIGN 已按当前 API 调整。
三个参考 target 已在当前工具链交叉链接；仍默认关闭，不作为产品入口。

静态审核涵盖来源、哈希、ARM 对象、共享库、符号与所有权约束。
部分 AAC/OpenSSL 对象未标 VFP 参数属性，需要结合最终 ARM 链接检查；
单凭缺少属性不能判定 soft-float。所有构建结果不代表板端能够运行。
当前能力以 [STATUS.md](../../STATUS.md) 为准。
