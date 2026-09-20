# UVC Gadget 输出参考流程

- 文档角色：Linux UVC gadget 用户态协议、sun8iw21 `sample_uvcout` 映射与媒体资源边界
- 主要基线：REF-MPP-SUN8IW21 的 `sample_uvcout`、`include/uvc.h` 和 `include/video.h`
- 验证边界：源码与 ABI 静态归纳，不代表目标 Host 已能枚举或稳定取流

UVC Out 的方向是“板卡作为 USB Device，把板载摄像头画面输出给 PC Host”。UVC gadget
video 节点是 V4L2 `VIDEO_OUTPUT`，不是 `/dev/video*` sensor capture 节点，也不是 MPP
模块。MPP 负责 ISP/VI 和可选 VENC；Linux UVC gadget 驱动负责 USB class control、输出
队列和 Host 传输。

本页说明可复用的平台协议。产品实际状态机、channel 和错误回滚由
[`camera-mpp-service` Pipeline](../../components/camera-mpp-service/pipelines.md) 定义，
配置由其 [`configuration.md`](../../components/camera-mpp-service/configuration.md)
定义。

## 1. 参考源码

固定参考树中的主要文件为：

```text
TMP/Yuzukilizard/Software/BSP/platform/allwinner/eyesee-mpp/
  middleware/sun8iw21/sample/sample_uvcout/
    sample_uvcout.c
    sample_uvcout.h
    sample_uvcout.conf
    sample_uvcout_config.h
    Readme.txt
  middleware/sun8iw21/include/
    uvc.h
    video.h
```

这里使用的是 `sample_uvcout`。`sample_UVC` 等名称相近目录不应替代该参考，因为本功能
需要的是 gadget output 的 Host event、PROBE/COMMIT 和 V4L2 output buffer 流程。

部署侧的 `setusbconfig`/configfs 脚本可用于核对 descriptor，但它不属于媒体服务：创建
USB gadget、绑定 UDC、组合 ADB function 和选择 device node 是系统部署职责。

## 2. 固定格式索引

首版沿用 sample 的稀疏格式/帧索引和 30 fps 间隔。部署公开的 configfs descriptor 与
应用接受的索引必须一致：

| Format index | 格式 | Frame index | 尺寸 | `dwFrameInterval` |
| --- | --- | --- | --- | --- |
| 1 | MJPEG | 1 | 1920×1080 | 333333（100 ns 单位） |
| 1 | MJPEG | 2 | 1280×720 | 333333 |
| 1 | MJPEG | 3 | 640×480 | 333333 |
| 2 | YUYV | 1 | 320×240 | 333333 |
| 3 | H.264 | 1 | 1920×1080 | 333333 |
| 3 | H.264 | 2 | 1280×720 | 333333 |

`dwMaxVideoFrameSize` 按 sample descriptor 使用 `width × height × 2`。这是一项 Host
协商上限，不是编码帧一定达到的长度；应用内部仍必须单独限制实际 VENC copy 和 gadget
buffer 的 `bytesused`。

格式索引是稀疏表，必须按 `(formatIndex, frameIndex)` 精确查找。只检查“索引不大于
最大值”会错误接受不存在的组合。

## 3. Host 控制状态机

应用打开已经配置、绑定好的 gadget video 节点，确认它同时提供
`V4L2_CAP_VIDEO_OUTPUT` 和 `V4L2_CAP_STREAMING`，再订阅：

```text
UVC_EVENT_CONNECT
UVC_EVENT_DISCONNECT
UVC_EVENT_STREAMON
UVC_EVENT_STREAMOFF
UVC_EVENT_SETUP
UVC_EVENT_DATA
```

典型状态变化为：

```text
open/subscribe
-> WaitingHost
-> CONNECT
-> Connected
-> PROBE SET_CUR / GET_*
-> COMMIT SET_CUR + DATA
-> Committed
-> STREAMON（isochronous）或 COMMIT 后启动（bulk）
-> Streaming
-> STREAMOFF / DISCONNECT
-> 停止并回到 Committed / WaitingHost
```

`SETUP` 事件携带 USB control request。对 Streaming interface 的
`UVC_VS_PROBE_CONTROL` 和 `UVC_VS_COMMIT_CONTROL`，应用处理 `SET_CUR`、`GET_CUR`、
`GET_MIN`、`GET_MAX`、`GET_DEF`、`GET_RES`、`GET_LEN` 和 `GET_INFO`，并通过 gadget 的
send-response ioctl 返回结果。`SET_CUR` 的真正 control payload 在随后的 `DATA` 事件中；
不得把 SETUP 和 DATA 当成两次独立协商。

`GET_LEN` 是小端的 `sizeof(uvc_streaming_control)`。不支持的 request/selector 保留负
response length 让 gadget stall，不能用未初始化 control 继续启动采集。

## 4. 数据路径

只有有效 COMMIT 且 Host 开始传输后，产品才创建 sensor 路径：

```text
MJPEG/H.264:
ISP -> VI(NV21) -> Bind -> VENC -> GetStream -> bounded copy
                                            -> ReleaseStream
                                            -> gadget MMAP output buffer

YUYV:
ISP -> VI(NV21) -> GetFrame -> stride-aware NV21-to-YUYV copy
                         -> ReleaseFrame -> gadget MMAP output buffer
```

H.264 启动时先准备 SPS/PPS，并请求 IDR；参数集应在有界队列中保持到成功输出，worker
还应丢弃实际 I/IDR slice 以前的普通帧，使 Host 不从不可解码的 P 帧开始。VENC pack
在 sun8iw21 ABI 中可由 Addr0/Addr1/Addr2 三段组成；复制长度必须逐段检查，且在进入
gadget 等待前归还 stream。

YUYV 路径可以使用硬件转换，也可以像当前产品实现一样针对小尺寸 320×240 做 CPU 转换。
CPU 实现必须使用 VI frame 的实际 Y/VU stride，并按 `Y0 U Y1 V` 排列写入输出；不能假定
plane 紧密排列。

## 5. Gadget buffer 与背压

V4L2 output 队列的基本顺序为：

```text
VIDIOC_REQBUFS(MMAP, fixed count)
-> 对每个 index: VIDIOC_QUERYBUF -> mmap -> VIDIOC_QBUF
-> VIDIOC_STREAMON
-> poll(POLLOUT) -> VIDIOC_DQBUF
-> copy one complete frame, set bytesused -> VIDIOC_QBUF
```

应用复制帧池和 ready queue 也必须固定深度。Host 变慢时应淘汰旧帧并计数，不应持有
MPP frame/stream 等待 USB，也不得无界分配内存。没有 ready frame 时不要 DQBUF；这样
不会取走一个 gadget buffer 后长期不归还。

停止时先结束 gadget streaming，再让采集线程退出并归还所有 MPP 对象，随后 `munmap`，
最后用 `REQBUFS(count=0)` 释放驱动队列。部分初始化失败和重复 stop 必须走同一幂等回滚。

## 6. 与 vendor sample 的产品化差异

| `sample_uvcout` 参考行为 | 产品化处理 |
| --- | --- |
| sample 进程自行 `AW_MPI_SYS_Init/Exit` | 单一 `camera-mpp-service` 只初始化一次 MPP runtime；UVC 是第三个互斥顶层 Pipeline |
| 全局变量和 signal 驱动退出 | Pipeline-local event loop 串行控制；worker 只取数据和投递故障 |
| 通过最大索引近似验证格式 | 精确验证稀疏 `(format, frame)` 表，非法组合回落到默认 profile |
| `GET_LEN`/control 处理含 sample 兼容性瑕疵 | 明确按小端长度和 bounded request payload 处理 |
| YUYV 依赖私有 `/dev/g2d` 流程 | 当前产品对 320×240 使用 stride-aware CPU 转换，减少另一设备 owner |
| 数据线程直接围绕设备缓冲运行 | 先 bounded copy、立即 Release MPP 对象，再由有界队列承受 Host 背压 |
| 错误和断连路径分散 | STREAMOFF、DISCONNECT、模式退出和中途失败共用逆序 cleanup |
| sample/脚本准备 USB gadget | 产品服务只消费预配置 node，不创建/修改 configfs，也不处理 ADB composite |

这些差异不改变 Host 可见格式索引，但改变了资源所有权和故障收敛方式。

## 7. 当前仍需目标环境确认

- 实际 UDC、configfs descriptor、gadget video node 编号及权限；
- isochronous 与 bulk event/ioctl 时序是否与当前 BSP 驱动一致；
- 各格式在 Linux/Windows/macOS Host 的枚举、payload 和解码兼容；
- H.264 UVC descriptor/payload 的 Host 支持和 SPS/PPS/IDR 行为；
- sensor/ISP/VENC 的目标板吞吐、内存和断连/重连稳定性；
- 与 ADB 的 composite gadget 设计。最后一项是当前明确非目标，不由媒体服务源码推断。

因此，源码实现或 vendor sample 存在只能形成候选能力；板端能力状态统一由
[`STATUS.md`](../../STATUS.md) 维护。
