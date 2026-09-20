# Qt/linuxfb 与 DISP2 显示集成

- 文档角色：跨 GUI、MPP 服务和 V851S BSP 的平台契约
- 决策依据：
  [`ADR-0005`](../../architecture/adr/0005-qt-linuxfb-disp2-layer-composition.md)
- 当前成熟度：[`STATUS.md`](../../STATUS.md)

本页只定义显示资源 owner、平台适配边界和系统级顺序。MPP 的具体函数调用与销毁
见 [`pipelines.md`](../../components/camera-mpp-service/pipelines.md)，GUI/后端命令
见 [`IPC v1`](../../interfaces/ipc-v1.md)，当前实现和未确认范围见
[`camera-gui`](../../components/camera-gui/README.md)、
[`camera-mpp-service`](../../components/camera-mpp-service/known-limitations.md) 与
[`STATUS.md`](../../STATUS.md)。

## 1. 平台能力边界

当前目标 Qt profile 是 Qt 5.12.9 Widgets + `linuxfb`，不依赖 Qt Multimedia、
GStreamer、QML/Qt Quick、OpenGL/EGLFS、Wayland/XCB。Qt overlay 的安装、哈希、SDK
前置条件和 smoke test 由工作区 `TMP/qt_project/README.md` 维护，Application 不复制
那套教程。

linuxfb rotation patch、Qt smoke test 或 framebuffer 可打开都不能证明目标板的
stride、rotation、触摸坐标、alpha 和 DISP2 映射正确。

## 2. 显示模型

Camera/Playback 会话使用分层合成：

```text
                     LCD / DISP2 blender
                              |
             +----------------+----------------+
             |                                 |
高层：Qt UI framebuffer layer       低层：MPP VO video layer
owner = camera-gui/linuxfb           owner = camera-mpp-service/DisplayOutput
透明背景、导航、按钮和状态           Camera Preview 或 Playback
```

Qt 和 MPP 使用不同 DISP2 layer，由显示引擎合成；它们不是两个程序共享同一个
framebuffer。当前 V853 LCD/VO 物理画布为 480×800；Qt linuxfb rotation=90
提供 800×480 逻辑界面。Camera 的 VO 以完整 `display.*` 为候选画布；
Album 视频仍复用同一个 MPP video layer，但把 VO 限制在 `playback.display_*` 子矩形并
采用 display.scale_mode 选择直接铺满（stretch）或等比留边（contain）。

UVC 会话保留 Qt UI layer 显示 Host/传输状态，但不创建 MPP VO video layer；摄像头媒体
数据只送到 USB gadget/Host，而不是 LCD。

YOLO 会话改为进程级时分复用：

```text
退出 MPP -> 销毁 Qt UI/QApplication -> camera-yolo-worker 独占 /dev/fbN
                                               |
                                               `-- 退出 --> 重建 Qt UI/MPP
```

YOLO 运行期间没有 Qt UI layer writer 和 MPP video layer owner，worker 保留原有的
RGB565 framebuffer 输出。

## 3. 所有权

| 资源 | 唯一 owner | 允许操作 | 明确禁止 |
| --- | --- | --- | --- |
| UI framebuffer `/dev/fbN` | Camera/Playback/UVC=`camera-gui`/linuxfb；YOLO=`camera-yolo-worker` | 当前会话唯一 writer 可绘制/刷新 | Qt 与 YOLO 并发写，或 MPP mmap/write |
| UI DISP2 layer | Camera/Playback/UVC=`camera-gui`/linuxfb | linuxfb 配置，GUI adapter 查询并校验 handle | MPP close/disable/reconfigure；未验证 UAPI 写属性 |
| Video DISP2/VO layer | Camera/Playback 的 `camera-mpp-service/DisplayOutput`；UVC/YOLO 无 | 创建、配置、启动、停止自己的视频层 | Qt 直接修改；UVC 创建 VO |
| LCD timing/output | BSP/系统启动 | 初始化固定输出 | 普通 Pipeline 任意切换 |

MPP 为避免分配 UI handle，可以在自己的 VO 管理器中登记/取消登记 outside layer；该
动作不转移 UI 所有权。MPP 对 UI layer 不得执行打开、关闭、启用、禁用、属性或优先级
修改。具体 MPP API 与顺序只在 service 组件 Pipeline 文档中维护。

## 4. Display profile 的边界

MPP 服务从 [`mpp-service.conf`](../../../configs/mpp-service.conf) 读取 VO interface、
sync、画布矩形、video layer 和 UI outside layer，配置语义见
[`configuration.md`](../../components/camera-mpp-service/configuration.md)。

必须保持：

- Camera Record 编码尺寸和 Preview/Playback 显示画布是不同字段；
- MPP 只完整管理 `display.video_layer`；
- `display.ui_outside_layer` 只用于 MPP 内部登记；
- 配置中的 handle 不等于 framebuffer 映射、alpha 或 z-order 已验证；
- 更换屏幕只改变部署 profile，不隐式改变录像编码参数。
- 当前 profile 为物理 480×800、Qt 逻辑 800×480，GUI 启动前核对配置与 framebuffer
  物理尺寸；禁止向 VO 传入 Qt 旋转后的宽高；
- 相册只对 IPC 返回的实际 Playback fitted rectangle 清除 Qt alpha，照片、空状态和框外
  UI 保持不透明。

用户最新要求预览采集和显示均为 480×800，当前 profile 使用 stretch 直接填满物理窗口。
两者尺寸一致，VO 不额外拉伸预览。contain 仍可配置；源宽高比与窗口不同时，stretch 会
改变比例。两种策略只决定 VO 矩形，不旋转原始帧或更改录像分辨率。

## 5. 当前探测与目标适配器

GUI 中的 `LinuxFramebufferProbe` 只读查询 `/dev/fb0` 的宽、高、bpp、stride 和 alpha
bits，用于布局和 profile 不一致提示。它不配置 `/dev/disp` 或 UI layer，也不能证明
透明 Qt backing store 会被 DISP2 保留。

已实现的 `Disp2LayerAdapter` 是 GUI 启动 MPP 前的 fail-closed 门禁：

- 从 MPP 配置读取 `display.video_layer`、`display.ui_outside_layer` 和 backend lock；
- 用 FBIOGET_FSCREENINFO 取得 framebuffer 内存范围，再通过 /dev/disp 的
  DISP_LAYER_GET_CONFIG 只读查询候选 UI 层，确认启用且其图像地址属于该 framebuffer；
- 只有实际 UI handle 等于 outside 配置、且不等于 video handle、MPP/YOLO lock 路径相同
  时才启动 MPP；
- adapter 不链接 MPP、不配置 video layer，也不改变 framebuffer layer 状态；Qt/linuxfb
  仍负责自己 layer 的创建和绘制。

用户已将本阶段“适配完成”的源码边界限定为保证 Qt 与 MPP handle 不同。因此 alpha、
z-order、rotation 和透明 backing store 仍只探测或保留给目标板确认，不在未验证 UAPI
上主动执行 `DISP_LAYER_SET_CONFIG`。

`TMP/sdk_disp` 只用于核对 ioctl、结构体和调用语义；其预编译库/对象、私有头文件和
SDK Makefile 不得直接成为产品依赖。

## 6. 系统启动与停止

目标启动顺序：

```text
camera-gui 在 QApplication 创建前探测 framebuffer
-> Disp2LayerAdapter 查询实际 handle 并校验配置/锁契约，关闭探测句柄
-> QApplication 启动 linuxfb，打开 framebuffer 并恢复 LCD
-> 绘制透明 UI
-> 启动 camera-mpp-service 并完成 IPC/capability handshake
-> Camera/Playback: MPP 登记 UI outside layer并创建低层 video layer
   UVC: 不创建 video layer，只等待 Host
-> Camera Preview、Playback Ready 或 UVC WaitingHost
```

Playback Ready 之前，服务通过 `GetVideoLayerAttr/SetVideoLayerAttr` 写入实际 `stDispRect`；
`media_loaded` 将同一物理矩形回传 Qt。Qt 按 linuxfb 的实际 rotation 对物理矩形做逆旋转，再从 800×480
逻辑坐标缩放到 1600×960 设计坐标后清 alpha。rotation=90 时，物理
(x,y,w,h) 对应逻辑 (y,480-x-w,h,w)。Playback stop/close 或照片选择后恢复相册不透明背景。

任何 handle 或 lock 不一致都会保留 Qt 错误页但禁止启动 MPP，不能带着重叠配置继续
运行。

停止 MPP 时，服务只停止自己的 Pipeline/video layer 并移除内部 outside 登记；Qt UI
由仍活动的 GUI 在后端进程退出后重新打开 framebuffer 恢复 LCD（当前 BSP 的
全局 close 副作用见下文）。切换 YOLO 时，Qt 先请求 MPP `shutdown` 并等待进程退出，然后退出整个 UI
子进程以触发 linuxfb/QApplication 析构；外层监督器看到 Qt 特殊退出码后才启动 worker。
YOLO 退出后按相反顺序恢复。任何一方都不执行全局 display reset。

## 7. 失败处理

- profile 与 framebuffer 尺寸不一致时提示并阻止错误能力承诺；
- UI alpha/z-order 未验证时不能声称透明合成可用；
- MPP 启停不得改变 Qt UI layer 的实际状态；
- 透明叠加失败时修正 adapter/profile，不能退回 Qt 与 MPP 同写 framebuffer；
- YOLO 启动失败或异常退出后必须关闭其 camera/fb fd 和 backend lock，再恢复 Qt；
- `SIGUSR1` 可要求外层监督器终止 YOLO 并返回 Qt；第二次信号用于 worker 不收敛时升级
  为强制终止。

具体测试步骤不在仓库中固定；主维护者指定检查方式后，只有影响当前能力或 blocker 的
结论才摘要更新 [`STATUS.md`](../../STATUS.md)。

### 当前 v853-100ask BSP 映射

本 SDK 的 dev_fb.c 没有实现 FBIOGET_LAYER_HDL_0/1；未知 ioctl 默认返回 0，
不能把该返回值当成查询成功。当前 sun8iw21 默认 framebuffer 映射到 channel=1、
layer_id=0，MPP 的 HLAY(channel, layer_id)=channel*4+layer_id，所以候选
display.ui_outside_layer 为 4。GUI 用当前内核导入的 sunxi_display2.h 和只读
DISP_LAYER_GET_CONFIG 验证候选层；地址无法核对、层未启用或驱动错误均拒绝启动 MPP。
使用硬件旋转的独立缓冲区时可能无法按 framebuffer 地址确认，须针对实际 BSP
扩展证据来源，不允许直接跳过门禁。图层 alpha、z-order 和目标画面仍须板测。


### 当前 BSP 的 /dev/disp 关闭副作用

当前 dev_disp.c 的 disp_release 不区分只读句柄或其他 display owner；未启用
CONFIG_DISP2_SUNXI_SATA_TEST 时，每次 close 都尝试关闭图层并关闭 LCD。因此
“只执行 GET ioctl”不等于整个 open/query/close 序列没有显示副作用。
图层契约检查必须在 QApplication/linuxfb 初始化之前完成。sunxi_fb_open 会重新启用
LCD，之后 Qt 持有 framebuffer。运行中的 UI 会话不得再次打开并关闭 /dev/disp 做探测。
MPP SYS Exit 关闭其 display fd 也有此副作用。若 Qt 会话仍活动，GUI 在收到后端退出
信号后重新打开自身 framebuffer，通过 sunxi_fb_open 恢复 LCD；不打开 /dev/disp，也
不调用 MPI。Qt 正在关闭或切换 YOLO 时不恢复，由下一个显示 owner 打开 framebuffer。

### 正常退出后的 UI layer 交还

当前 sunxi_fb_release 为 no-op；仅销毁 QWidget/关闭 fb fd 不会清掉残留 UI。
GUI 退出时先通过 IPC shutdown 等待 MPP 收尾，再让 QWidget/QApplication/linuxfb 析构。
随后在 GUI 自己的 framebuffer 中清零包括 alpha 的所有虚拟页，调用
FBIOBLANK(FB_BLANK_POWERDOWN) 关闭该 framebuffer 对应的 UI 层。当前 dev_fb.c 此操作
只设置 UI config.enable=0，不关闭其它视频层。清零避免后续 fb_open 又开启 UI 时残留遮挡。
下一次 GUI 启动前 FB_BLANK_UNBLANK 恢复自己的层，再完成 layer 门禁。
不通过 /dev/disp SET_CONFIG 操作其它层。用户本轮明确要求此退出行为，显示 owner 不变。

UI 子进程被 SIGKILL 时无法执行析构。只要外层 session supervisor 仍存活，它会等待
后端释放锁后清空并关闭 UI framebuffer layer。若整个进程组/所有监督者被 SIGKILL，
用户态无法保证清理；正常退出按钮、SIGTERM/SIGINT 才是完整资源退出路径。
