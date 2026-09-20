# Application 当前状态

> 本页保留 2026-09-16 的适配/板端记录；精简交付的当前构建与部署入口见 [使用指南](../../../00_GUIDE/BUILD_AND_DEPLOY.md)。历史 build 日志目录已不随最终工程分发。

- 文档角色：当前实现、已确认事实、能力和活动阻塞的唯一状态面板
- 最近复核：2026-09-16
- 源码：用户原始 v851s-camera-handoff-20260915.tar.gz 解压目录；原包保留，目录没有独立 Git 基线
- SDK：/home/ubuntu/tina-v853-100ask，v853-100ask / sun8iw21；GCC 6.4.1 ARM hard-float、musl 1.1.24
- 板端：Windows ADB 20080411，Linux 4.9.191 #85、GC2053、480×800 LCD、ft6336
- 测试部署：/mnt/UDISK/camera-validation-20260915；没有刷固件或改 SDK kernel/DTS/rootfs
- 产品范围：[requirements.md](requirements.md)；阶段顺序：[roadmap.md](roadmap.md)

源码、构建、具体板测和完整产品验收分别记录。下列板端结论仅覆盖指定固件、配置和运行，
不承诺长稳、其它硬件或全部媒体格式。

## 1. 当前结论

当前 SDK 的头文件/库已导入 standalone bundle，旧 Yuzukilizard 二进制不再作为产品链接
输入。Qt 5.12.9、OpenCV 4.1.0 VIN/ISP、AWIspApi adapter 和三个产品程序均已交叉构建。

板端已完成 Qt 基础显示、触摸入口、Camera/JPEG/MP4、H.264 回放启停、loopback RTSP、
OpenCV 取帧与重新打开的有限验证。UVC 按用户要求暂停；没有匹配的模型/类别文件，
YOLO/NPU 推理及 Qt→YOLO→Qt 的完整交接尚未验证。

显示条纹已定位到物理窗口配置：旧配置向 480×800 LCD 传入 800×480 画布，
实际视频层为 (0,16,800,450)，超出物理屏幕宽度。相同 MPP/采集下改为正确物理边界后
无条纹，用户已确认。Qt 横屏逻辑仍为 800×480，MPP/IPC 使用物理坐标。

用户最新指定预览采集 480×800，VO 物理窗口 (0,0,480,800)，改用 stretch 铺满。
截图和 DISP2 状态确认帧、crop、window 均为 480×800，无此前条纹或 contain 大黑边。
同时按当前 sample 修正 Camera fourcc：NV21M 改为 NV21；旧 360 宽 NV21M 路径
绕过 NV21 的 stride 设置，而 MPP 帧宽按 16 对齐为 368。不能把两者当作同一个格式。

独立媒体采集按用户纠正改为 VIPP4 1280×720，首次 Snapshot/Record/RTSP 时按需启动；
VIPP0 只负责预览，VIPP8 保留。此前 VIPP1 只能初始化，实际没有帧，不能视为双路成功。
当前运行固件 vinc@4 为 disabled，没有 /dev/video4；已验证三个媒体请求均明确返回
camera_media_unavailable，VIPP0 预览继续，未回退为 480×800 放大编码。用户自行处理第二路
固件配置后再验证 VIPP4 的实际 JPEG/MP4/RTSP；此前单 VIPP0 结果不适用于新配置。

主页新增“退出程序”。正常关闭先等待 MPP，再销毁 Qt，最后清零 framebuffer 全部虚拟页
并关闭自身 UI layer。按钮退出后 UI layer 不再启用，直接启动原厂 sample 无残留 GUI
遮挡，sample 正常退出。SIGTERM 清理通过；Qt UI 子进程被 SIGKILL、外层监督器存活时，
已观察到等待后端退出并执行 framebuffer 清理，随后原厂 sample 正常显示和退出。
全部监督者同时 SIGKILL 无用户态保证。

## 2. 同源 SDK 与构建证据

- Bundle：当前 SDK staging/source 的 91 个库制品与 6 个参考源码/配置条目；
  SDK_SOURCE.json、SDK_FEATURES.cmake、BUNDLE_MANIFEST、SHA256SUMS 保存来源和哈希。
- sensor_config / sensor_exp_gain = 76/32 字节；ioctl = 0xc04c56fc / 0xc02056fd，
  与当前内核一致；VI_ATTR_S / VIDEO_FRAME_S / VIDEO_FRAME_INFO_S = 284/136/144。
- 当前 ISP 标识 5b30d9b8f8190196d863bc91576af5356022b433 已在 MPP service 和 AWIspApi
  产物中确认；最终 link map 指向当前 bundle。
- MUX 使用当前单层 channel API；Bind 后设置 SPS/PPS，正常 StopChn(FALSE) 排空。
- ISP2VE 参数请求同步从当前 ISP 回填，其余释放/错误通知交给控制路径。
- DEMUX 使用 FD 输入；H.265 解码未在当前 SDK 启用，服务明确拒绝。
- ABI/链接来源检查通过；三个应用、Qt 插件及 smoke test 递归解析共 29 个 ARM ELF32
  hard-float，无 TEXTREL；三个安装应用无构建机绝对 RPATH。GUI 只直接依赖 Qt/runtime。
- TinyVision 五个原始补丁与 OpenCV/contrib 4.1.0、ADE 0.1.1d 校验通过；
  OpenWrt 指定镜像文件 404，使用官方同版本归档并固定 SHA-256。
- OpenCV 本地 VIN 补丁及 Qt 本地触摸补丁已从归档原文件重放并与编译输入逐字节比较；
  重复 prepare 校验通过。

## 3. 已完成的板端范围

| 能力 | 实际结果 | 边界 |
| --- | --- | --- |
| Qt 基础显示/字体 | 用户确认 Qt 5.12.9 linuxfb 文字与时间正常；中文字体已部署 | 仅当前 LCD/profile |
| 触摸 | local single-touch/range 适配后，用户确认可以进入 Camera | 多点触摸未验证 |
| Camera Preview | 当前 VIPP0 采集 480×800，VO 480×800 铺满；合成截图无条纹 | Qt rotation=90；VIN 仍按采集比例裁剪 sensor |
| Snapshot | 旧单 VIPP0 1280×720 路径 JPEG 可读取 | 新 VIPP4 待固件启用；当前缺节点拒绝已验证 |
| Record | 旧单 VIPP0 路径 1280×720 H.264 MP4 正常停录；FFmpeg 完整解码通过 | 新 VIPP4 待固件启用；未外推新链路成功 |
| Record 门禁 | 录像中的 Snapshot 被服务端拒绝 | GUI/服务边界保持 |
| Playback | 5 次 H.264 循环：4 次运行中 Pause/Resume/Stop，1 次自然 EOF 后停止；修复后的服务正常退出 | 不是所有 codec/container 的结论 |
| Camera 生命周期 | 修正 producer/VO 停止顺序后，10 次快速 enter/leave，加预览状态直接 shutdown，服务正常退出；dmesg 无新增地址/崩溃告警 | 长稳未验证；UVC 测试暂停 |
| RTSP | 旧单 VIPP0 路径 loopback 预览/录像并发/重启 3 轮，SPS/PPS/IDR 和序号连续 | 新 VIPP4 待固件启用；真实网络/多客户端未测 |
| OpenCV VIN | 640×480 BGR 两次打开，每次 20 帧；MPP 停止后再次重复通过 | 仅连续单 plane NV21 布局 |
| Backend lock | MPP 持锁时 OpenCV 立即拒绝占用，无并发 sensor owner | YOLO 模型端未验证 |
| 显示几何 | 板端 DisplayGeometry 检查通过：0/90/180/270、Qt 正向变换逆映射、越界拒绝 | 映射只处理矩形，不旋转帧像素 |
| 后端退出恢复 | 最终 GUI 相册选片→播放→Pause/Resume→Stop→Camera→shutdown 通过；MPP exit=0、crash=false；UI layer/LCD 恢复，合成截图保留 GUI | 当前 BSP 专有 close 副作用 |
| GUI 退出 | 首页按钮→MPP exit=0→UI 清空并禁用；随后原厂 sample exit=0，合成截图无 GUI 遮挡 | 仅当前 BSP framebuffer 语义 |
| UVC | 仅源码/API 与编译检查；早期缺节点失败恢复已观察 | 按用户要求暂停，不声明 Host 取流 |
| YOLO/NPU | worker 完整交叉链接 | 无匹配模型与类别，未执行推理 |

## 4. 已修复的问题与边界

- Qt 黑屏：本 BSP 每次 close /dev/disp 都会关闭 LCD，包含只读 GET 查询。
  Layer 校验移到 QApplication 前；后端 SYS Exit 后由仍活动的 GUI 打开自己的 framebuffer
  恢复 LCD。关闭 GUI/交接 YOLO 时跳过恢复，不改变显示 owner。
- 触摸：ft6336 事件不符合 Qt 原生 Type A/B 帧结构；本地 Qt patch 提供显式 single-touch
  与 range 覆盖，避免改用户已经验证的触摸硬件或 DTS。
- Vendor stdout/stderr 从 GUI 状态文本中分离，直接写运行日志，避免控制台内容覆盖画面。
- Camera 快速退出同样发现晚到帧问题；改为先 DisableVirChn，再 StopChn 排空，最后解除
  tunnel，避免 VO Idle 队列中的帧在空 handle 上返回；修复后完成 10 次快速启停。
- Playback 退出崩溃：先停止 DEMUX/VDEC 生产者，在解除 tunnel 前清 EOF 并让 VO
  完成实际 Executing→Idle 排空；解决 EOF 自行变 Idle 后仍持有帧的问题。
- OpenCV：REQBUFS 后重新 G_FMT；当前 sunxi-vin NV21 报 width*1.5 bytesperline，
  但实际 Y 步长为 width。限定驱动、布局、对齐和大小检查后修正，仍拒绝真正 padding。
- GUI 对回放矩形做物理→逻辑逆旋转，再缩放到设计坐标；启动前拒绝物理尺寸不匹配。

MPP 仍有 ISP TDM/温度 ioctl、AEWB 初始化和 GC2053 帧率 profile 等厂商告警；
这些运行未复现旧 ABI 错误，但不能据此声明 ISP 所有控制或画质均正常。

## 5. 视野的直接证据

读取当前 V4L2 subdev（只执行 GET）：

| 运行配置 | Sensor/ISP | VIN 输出 | VIN crop.request |
| --- | --- | --- | --- |
| 应用采集 1280×720 | 1920×1088 | 1280×720 | (0,4,1920,1080) |
| 原厂 sample 采集 360×640 | 1920×1088 | 360×640 | (654,0,612,1088) |

上表是前一轮的取景证据：1280×720 保留完整水平视野，竖向比例采集会做居中裁剪。
当前预览已按用户要求改为 480×800；独立 VIPP4 1280×720 的实际视野仍待出帧验证。
屏幕缩放、摄像头安装方向与 VIN 裁剪应分别判断；本轮未修改 kernel/DTS。

## 6. 产物与证据位置

路径相对项目根目录：

| 内容 | 路径 |
| --- | --- |
| 应用构建、安装 | build/full-sdk、build/stage-current-sdk |
| 应用构建日志 | build/build-full-sdk.log、build/install-full-sdk.log |
| ABI、ELF 哈希和依赖 | build/full-sdk/abi/results.json、build/full-sdk/elf-dependencies.json |
| MPP link map | build/full-sdk/services/camera-mpp-service/camera-mpp-service.map |
| 板测脚本与隔离运行文件 | build/board-20080411/runtime |
| 板测日志、截图、媒体 | build/board-20080411/logs、build/board-20080411、build/board-20080411/media |
| Qt/OpenCV 当前交付入口 | [SDK overlay](../../../02_SDK_OVERLAYS/README.md) |

Ubuntu SSH 执行源码操作/交叉构建；Windows ADB 执行本轮授权板测。
UDISK 用作本次媒体存储；板上 /mnt/extsd 实际是小容量 boot-resource 分区，
不能按默认配置当成录像 SD 卡。正式部署必须选择正确挂载点和剩余空间策略。

## 7. 未完成与发布边界

- 用户启用 vinc@4 后验证独立 VIPP4 出帧及 JPEG/MP4/RTSP；
- 更多媒体比例/方向覆盖、受控长稳与资源测量；
- UVC descriptor/Host 取流，待用户配置好后继续；
- 匹配模型、NPU 推理、Qt→YOLO→Qt 完整切换；
- 真实网络、慢/多 RTSP 客户端、存储异常、断电修复；
- 所有 codec/container 组合、损坏/截断文件、B 帧等；
- 正式 rootfs/package 集成、启动配置、版本基线和 bundle 再分发边界。

当前板测不构成完整 Product accepted，不推断帧率上限、带宽、温度或长期稳定性。
