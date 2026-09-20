# 平台集成

本目录保存依赖 Allwinner BSP、内核或硬件、但可被多个产品组件复用的平台知识：

- [`mpp/`](mpp/)：MPP/MPI 的 SYS、VI、VO、VENC、VDEC 接口，以及 SmartIPC 和 UVC
  gadget 参考流程；
- [`display/`](display/)：Qt/linuxfb、framebuffer、DISP2 与 MPP VO 的显示集成；
- [`reference-sources.md`](reference-sources.md)：外部手册、固定 sample 版本、用途和
  适用边界。

平台文档可以说明 vendor API 的对象、状态、数据所有权和通用组合方式，但不定义产品
channel ID、默认参数、业务按钮、IPC 字段或某个 service 的实例化 Pipeline。具体实现、
已知限制和未确认范围归相关 [`components/`](../components/)，当前能力只看
[`STATUS.md`](../STATUS.md)。
