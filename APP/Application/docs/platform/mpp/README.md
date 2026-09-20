# Allwinner MPP/MPI 开发入口

- 文档角色：跨组件复用的 MPP/MPI 开发手册
- 主要基线：sun8iw21 `mpi_*.h`、`mm_comm_*.h` 与对应 sample
- 验证边界：静态归纳，不代表 V851S 板端能力或性能

本目录回答“MPP 组件本身怎样使用”。它把常用接口、对象层级、数据所有权和参考流程
从 sample 中抽取出来，开发普通 VI、VO、VENC、VDEC 链路时应先读这里，再对照实际
sysroot 头文件；不再把某个 sample 的 `main()` 当作日常接口手册。

`camera-mpp-service` 的 channel 选择、状态机、配置投影和失败回滚仍由组件
[`pipelines.md`](../../components/camera-mpp-service/pipelines.md) 负责。本目录不定义
该服务的产品行为。

## MPP、MPI 和“标准模块”分别是什么

- **MPP**（Media Processing Platform）是 Allwinner 的媒体组件框架和运行时；
- **MPI** 是应用调用 MPP 的 C 接口层，常见符号以 `AW_MPI_*` 开头；
- **SYS、VI、VO、VENC、VDEC** 是该 MPP 框架中的标准模块角色，不是跨厂商统一 ABI；
- **V4L2、DRM/DISP2、RTSP/RTP、MP4** 分别属于内核采集/显示、网络和容器边界，不能
  因为 sample 把它们放在一起就都称为 MPI。

因此，本手册所说的“通用”是指可在本项目多个 MPP 组件/流程中复用，并尽量按同代
Allwinner MPI 归纳；它不承诺可以不经适配地移植到其他厂商或其他 Allwinner SoC。

## 文档导航

| 文档 | 回答的问题 |
| --- | --- |
| [`core.md`](core.md) | SYS 初始化、`MPP_CHN_S`、Bind、tunnel/non-tunnel、callback、MMZ 和通用生命周期 |
| [`vi.md`](vi.md) | VIPP、VirChn、ISP、VI 属性、绑定采集和手动取帧 |
| [`vo.md`](vo.md) | VO device、video layer、channel、绑定显示和手动送帧 |
| [`venc.md`](venc.md) | VENC 属性、码控、绑定/手动输入、取流、SPS/PPS、IDR 和 JPEG |
| [`vdec.md`](vdec.md) | VDEC 属性、帧/流模式、绑定/手动输入输出、EOF、Pause/Resume |
| [`smart-ipc.md`](smart-ipc.md) | VI→VENC 后如何把同一编码流交给 RTSP/录像，以及 Preview/AI/Playback 的边界 |
| [`uvc.md`](uvc.md) | Linux UVC gadget 的 PROBE/COMMIT、Host event、输出 buffer 与 VI/VENC 数据路径 |

推荐阅读顺序：

```text
core -> 目标模块文档 -> smart-ipc（需要 RTSP/录像组合流程时）
                       或 uvc（需要 USB Device 输出时）
     -> docs/components/camera-mpp-service/pipelines.md（修改产品服务时）
```

## 三层事实不能混用

| 层级 | 用途 | 本目录处理方式 |
| --- | --- | --- |
| 当前构建实际使用的 sysroot 头文件/库 | 最终 ABI 与可链接符号 | 实施前必须再次核对，优先级最高 |
| sun8iw21 头文件、实现和 sample | 当前 V851S 邻近平台的主要静态参考 | 决定本手册的核心接口名和调用配对 |
| V821 指南、sun252iw1/rt_media 资料 | 其他 SoC 的概念与演进参考 | 只补充模型和风险，不直接成为 V851S 结论 |

本次归纳使用的本地参考位置见
[`reference-sources.md`](../reference-sources.md)，主要包括：

```text
TMP/Yuzukilizard/Software/BSP/platform/allwinner/eyesee-mpp/
  middleware/sun8iw21/include/media/
  middleware/sun8iw21/sample/

TMP/html/_sources/board/mpp/
  Tina_Linux_多媒体MPP_开发指南/

TMP/Agent_SKILLS/packages/multimedia/sun252iw1/
  eyesee-mpp/middleware/
  rt_media-mpp_demo/
```

其中 V821 指南明确只适用于 V821；`Agent_SKILLS` 是从其他源码生成的二次资料。它们
可以帮助发现新的接口分组或线程模型，但不能覆盖 sun8iw21 头文件，也不能证明目标板
支持某分辨率、帧率、并发数、online 模式或编码特性。

## 文档中的约束词

- **必须**：来自接口所有权、Get/Release 配对或本项目稳定安全边界；违反后通常会泄漏、
  阻塞或造成非法状态。
- **推荐**：多个 sample 和接口状态共同支持的稳健实现方式；不是厂商 ABI 的逐字规定。
- **候选**：头文件存在但当前产品未使用，或只在其他 SoC 资料中看到；采用前需核对
  当前 sysroot、库符号与目标板。

## 使用本手册时仍需确认的内容

开发新链路前至少确认：

1. 实际编译使用的 `mpi_*.h` 函数签名与结构字段；
2. `PAYLOAD_TYPE_E`、`PIXEL_FORMAT_E`、colorspace、宽高和 stride 是否在链路两端一致；
3. channel、VIPP、ISP、VO layer 是否被其他进程或组件占用；
4. 每个成功的 Create/Enable/Bind/Get 是否进入资源账本并有对应逆操作；
5. callback 和 worker 是否只投递事件、使用有界队列并可被停止路径唤醒；
6. 能力结论是否有主维护者认可的 V851S 目标板事实。

当前产品成熟度只看 [`STATUS.md`](../../STATUS.md)，产品默认值只看
`configs/mpp-service.conf` 和 service 组件配置文档。
