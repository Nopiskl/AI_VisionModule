# V851S 实际环境构建与首次运行

本文是源码 handoff 到交叉编译环境和 V851S 目标板时的长期操作入口。它定义需要补齐的
外部输入、推荐构建/安装顺序、首次板端检查顺序以及应回传的证据；它不把源码存在或
静态审核结果表述为目标板能力。当前能力和 blocker 仍以 [`STATUS.md`](STATUS.md) 为准。

## 1. Handoff 边界

源码 handoff 应包含完整的 `Application/` 权威源码树，但不包含以下内容：

- `Application/build/` 和其他生成的构建目录；
- `Application/third_party/sunxi-mpp-sdk/` 生成 bundle；
- `TMP/` 参考树、原始 NewUI 压缩包和 Application 之外的 sample 副本；
- YOLO 模型、类别文件以及来源/授权未确认的 MPP、VIPLite 二进制；
- TinaSDK、目标 sysroot、目标 rootfs 和板端日志。

`Application/src/component/MPP/` 中受顶层 CMake 管理的迁移参考 target 属于当前源码树，
会随包保留；它们默认不构建，也不是产品运行入口。

MPP bundle 可由 handoff 内的 manifest 和脚本在获授权的环境中重建。模型、工具链和
rootfs 则是部署输入，不属于 Application 源码。收到归档后先校验同目录的 SHA-256，
再解压到新的目录：

```sh
sha256sum -c v851s-camera-handoff-20260915.tar.gz.sha256
mkdir -p /absolute/work/v851s-camera-handoff
tar -xzf v851s-camera-handoff-20260915.tar.gz \
  -C /absolute/work/v851s-camera-handoff
```

若归档名称或日期变化，以实际 `.sha256` 文件中的名称为准。不要把解出的源码覆盖到
一个已有且包含本地修改的 Application 工作树。

## 2. 构建前输入

开始配置前，实际环境需要明确以下输入。路径均应使用绝对路径，避免 CMake 从 host
环境误找 Qt、OpenCV 或系统库。

| 输入 | 最低约束 | 构建前检查 |
| --- | --- | --- |
| C/C++ 交叉编译器 | 32-bit ARMv7、hard-float，和目标 musl/libstdc++ ABI 一致 | 编译器文件存在；`-dumpmachine` 和目标架构一致 |
| 目标 sysroot | 和目标 rootfs 同源，包含 libc、libstdc++、pthread、dl、rt 等 | 不混用 host 头文件/库；记录来源和版本 |
| Qt 5 target SDK | Qt 5.12 或兼容版本，包含 Core/Gui/Widgets 和 linuxfb plugin | `Qt5_DIR` 指向 target CMake package，不是 host Qt |
| OpenCV target SDK | 包含 core、dnn、imgcodecs、imgproc、ml、videoio | `OpenCV_DIR` 指向 target CMake package，不是 host OpenCV |
| 当前已构建 SDK | v853-100ask / ARM musl | staging 头文件与库、.config、内核同源 |
| YOLO 部署数据 | 和 worker tensor 契约匹配的 `.nb` 模型及类别文本 | 单独确认来源、版本、输入尺寸和再分发权限 |
| 板端 BSP/rootfs | 对应 sensor、VIN/ISP、ION、VENC/VDEC、DISP2、V4L2 和 USB gadget 驱动 | 不由 Application 安装或替换内核侧内容 |

目标 rootfs 还需要 Qt linuxfb platform plugin、JPEG imageformat plugin 和可显示中文的
字体。MPP/VIPLite 运行库应优先使用目标 rootfs 已有且与 BSP 同源的版本；不要仅因
Application 可以生成 bundle，就默认覆盖系统库。

## 3. 重建并审核本地 MPP bundle

在 handoff 的 `Application` 目录执行：

```sh
cd /absolute/work/v851s-camera-handoff/v851s-camera-handoff-20260915/Application

SUNXI_TINA_SDK_ROOT=/home/ubuntu/tina-v853-100ask \
  ./tools/fetch_sunxi_mpp.sh

./tools/audit_sunxi_mpp.sh
```

`fetch_sunxi_mpp.sh` 要求目标 `third_party/sunxi-mpp-sdk` 不存在；这是防止静默覆盖的
安全门禁。如果要重建，应先人工核对并移动旧目录，而不是对模糊路径做递归删除。没有
本地 checkout 时脚本可以联网获取固定提交，但发布环境应优先使用已审核的本地来源。

审核通过只表示 manifest、文件哈希、静态符号、ABI 属性和若干源码约束吻合，不等于
最终链接成功，也不等于目标板上的 sensor、codec、显示或 USB 功能可用。

## 4. 交叉配置、构建与安装暂存

使用一个新的构建目录和一个新的安装暂存目录。以下示例同时构建 GUI、MPP 服务和
YOLO worker，且不把 vendor runtime 自动装入暂存 rootfs：

```sh
V851S_HANDOFF_ROOT=/absolute/work/v851s-camera-handoff/v851s-camera-handoff-20260915
V851S_BUILD_DIR=/absolute/build/v851s-camera
V851S_INSTALL_STAGE=/absolute/stage/v851s-camera

cmake -S "${V851S_HANDOFF_ROOT}/Application" \
  -B "${V851S_BUILD_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${V851S_HANDOFF_ROOT}/Application/cmake/toolchains/v851s-musl.cmake" \
  -DV851S_C_COMPILER=/absolute/toolchain/bin/arm-openwrt-linux-gcc \
  -DV851S_CXX_COMPILER=/absolute/toolchain/bin/arm-openwrt-linux-g++ \
  -DV851S_SYSROOT=/absolute/toolchain/sysroot \
  -DQt5_DIR=/absolute/target-qt/lib/cmake/Qt5 \
  -DOpenCV_DIR=/absolute/target-opencv/lib/cmake/opencv4 \
  -DSUNXI_MPP_ROOT="${V851S_HANDOFF_ROOT}/Application/third_party/sunxi-mpp-sdk" \
  -DAPPLICATION_BUILD_GUI=ON \
  -DAPPLICATION_BUILD_MPP=ON \
  -DAPPLICATION_BUILD_MPP_EXAMPLES=OFF \
  -DAPPLICATION_BUILD_YOLOV8=ON \
  -DAPPLICATION_INSTALL_VENDOR_RUNTIME=OFF \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr

cmake --build "${V851S_BUILD_DIR}" --parallel
DESTDIR="${V851S_INSTALL_STAGE}" cmake --install "${V851S_BUILD_DIR}"
```

若 Qt 或 OpenCV 位于 sysroot 中，仍建议给出其 target package 的绝对 `Qt5_DIR`/
`OpenCV_DIR`。配置日志必须确认没有找到 host `/usr/lib` 下的包。不要启用
`APPLICATION_INSTALL_VENDOR_RUNTIME`，除非已经明确 rootfs 库来源、SONAME/ABI 和
再分发权限；该开关不是“修复缺库”的通用手段。

正常安装暂存至少应产生：

```text
/usr/bin/camera-gui
/usr/bin/camera-mpp-service
/usr/libexec/v851s-camera/camera-yolo-worker
/etc/v851s-camera/mpp-service.conf
```

安装到目标板前检查三个 ELF 和动态依赖：

```sh
file "${V851S_INSTALL_STAGE}/usr/bin/camera-gui"
file "${V851S_INSTALL_STAGE}/usr/bin/camera-mpp-service"
file "${V851S_INSTALL_STAGE}/usr/libexec/v851s-camera/camera-yolo-worker"

readelf -h "${V851S_INSTALL_STAGE}/usr/bin/camera-gui"
readelf -h "${V851S_INSTALL_STAGE}/usr/bin/camera-mpp-service"
readelf -h "${V851S_INSTALL_STAGE}/usr/libexec/v851s-camera/camera-yolo-worker"

readelf -d "${V851S_INSTALL_STAGE}/usr/bin/camera-gui"
readelf -d "${V851S_INSTALL_STAGE}/usr/bin/camera-mpp-service"
readelf -d "${V851S_INSTALL_STAGE}/usr/libexec/v851s-camera/camera-yolo-worker"
```

需要确认它们是预期的 ARM ELF、解释器与目标 rootfs 一致，且 `NEEDED` 项能由目标
rootfs 提供。构建成功不能替代这一步。

## 5. 目标板部署前置条件

把安装暂存树合入测试 rootfs 后，还需单独提供：

```text
/etc/v851s-camera/yolov8.nb
/etc/v851s-camera/yolov8-classes.txt
```

然后逐项核对：

- `/etc/v851s-camera/mpp-service.conf` 与实际 LCD、camera、网卡和 UVC gadget 节点一致；
- `/mnt/extsd` 是实际已挂载介质，而不是 rootfs 上的空目录；默认录制目录为
  `/mnt/extsd/v851s-camera`，相册根目录为 `/mnt/extsd`；
- 默认 RTSP 网卡是 `eth0`，端口是 `8554`，流名是 `ch0`；若板端网卡名不同，应在
  配置中显式修改，不应在源码中硬编码第二套默认值；
- `/dev/fb0` 是 Qt 应接管的 framebuffer，分辨率为 800×480，Qt linuxfb plugin 可加载；
- UVC gadget 的 descriptor、format/frame index 和 `/dev/video2` 已由系统部署层配置并
  完成 UDC 绑定；Application 不创建 configfs function；
- 不同时启动另一个会占用相同 VIN/ISP/VO/fb/VIPLite 资源的程序。

当前阶段不处理 UVC 与 ADB composite gadget 的兼容。测试 UVC 时可使用独立、已配置的
UVC gadget；不要让 Application 直接修改现有 ADB gadget。

## 6. 启动顺序

产品正常入口是 `camera-gui`。GUI 外层监督器负责启动/停止 MPP 服务，并在
Qt→YOLO→Qt 之间执行互斥交接，因此正常运行时不要再手工并行启动第二个
`camera-mpp-service`：

```sh
/usr/bin/camera-gui \
  --service /usr/bin/camera-mpp-service \
  --service-config /etc/v851s-camera/mpp-service.conf \
  --yolo /usr/libexec/v851s-camera/camera-yolo-worker \
  --yolo-model /etc/v851s-camera/yolov8.nb \
  --yolo-classes /etc/v851s-camera/yolov8-classes.txt \
  2>&1 | tee /tmp/v851s-camera-gui.log
```

如未显式设置 `QT_QPA_PLATFORM`，程序会根据 framebuffer 参数选择 linuxfb。YOLO 运行
期间 Qt 和 MPP 都应退出并释放 fb/camera；worker 退出后监督器会重新创建 Qt/MPP。
需要从外部请求 YOLO 返回 Qt 时，可向最外层 `camera-gui` 监督进程发送 `SIGUSR1`，不要
把信号发给刚好同名的 Qt 子进程。

MPP 服务的单独启动命令仅用于定位初始化或依赖问题。单独启动结束后必须确认进程已经
退出，再从 GUI 入口启动完整产品。

## 7. 首轮实机检查顺序

建议按以下顺序逐项推进。某一步出现资源泄漏、进程残留、黑屏或 kernel error 时，先
停在该步骤收集证据，不要继续叠加后续功能。

### 7.1 启动与 DISP2 图层（OPEN-012）

1. 记录 `/dev/fb0` 的实际分辨率、像素格式、stride 和 framebuffer layer handle。
2. 启动 GUI，保存 framebuffer probe 和 `Disp2LayerAdapter` 的完整诊断。
3. 确认 Qt UI handle 与配置的 `display.ui_outside_layer=4` 一致，同时与 MPP
   `display.video_layer=0` 不同；不一致时 GUI 应拒绝启动 MPP，而不是继续抢占图层。
4. 在 Camera 中观察 MPP 视频全画布、Qt 透明 overlay、alpha 和 z-order。
5. 在 Album 视频中观察 MPP 只占配置矩形 `(144,40,644,388)` 内的实际等比拟合区域，
   Qt 只清除该区域 alpha，其他控件保持可见和可触摸。

只有读取到真实 framebuffer/layer 数据并观察到正确合成，才可关闭 OPEN-012。配置值、
源码常量或日志中的“期望 handle”本身不构成通过证据。

### 7.2 Camera 基础路径

1. 从 Home 进入 Camera，检查 Preview 首帧、持续画面和返回 Home 后的资源释放。
2. 非录像状态拍照，确认 JPEG 可打开、相册能看到新文件，Preview 不被永久中断。
3. 开始录像，确认后端进入 Recording 后 Camera 页面只保留 iOS 风格停止按钮，顶部按
   后端 `record_elapsed_ms` 显示已录时长，拍照入口不可达。
4. 停止录像，确认普通 Camera 控件恢复；MP4 从 `.part` 安全提交为最终文件并可播放。
5. 录像期间用 IPC 或异常路径请求 Snapshot，确认服务端仍返回拒绝，不能只依赖 UI 隐藏。

### 7.3 RTSP

1. 在 Camera 页面启用 RTSP，确认顶部状态栏显示后端返回的实际 URL，而不是 UI 拼接值。
2. 从同网段客户端访问 `rtsp://<board-ip>:8554/ch0`，确认 H.264 纯视频播放、首个 IDR、
   SPS/PPS、断连后资源回收和再次连接。
3. 分别检查 RTSP 单独运行以及与本地录像并行；记录码率、帧率、延迟和错误日志。

### 7.4 Album/Playback

1. 用 JPEG、H.264 MP4 和 MJPEG 样本验证有界扫描；本 SDK 对 H.265 应明确拒绝。
2. 确认照片由 Qt 播放，视频由 MPP DEMUX→VDEC→CLOCK→VO 播放到局部矩形。
3. 检查开始、暂停、继续、停止、自然播放结束、返回 Home 和损坏/不支持媒体。
4. 每次退出后重新进入 Camera，确认 VO/VDEC/DEMUX 资源已释放。

### 7.5 UVC Out

1. 在 Host 未 STREAMON 时进入 UVC 页面，确认 GUI 显示后端真实状态而不是动画即成功。
2. Host 依次协商固定 descriptor 中支持的格式/分辨率/帧率，检查 PROBE/COMMIT、
   STREAMON/OFF 和传输画面。
3. 检查 Host 断连、重连、慢消费和 UVC 页面退出；观察 bounded queue 丢旧帧但不泄漏
   VI frame、VENC stream 或 gadget buffer。
4. UVC 退出后重新进入 Camera，确认 camera/ISP 所有权可重新获得。

### 7.6 YOLO 互斥切换

1. 从 Home 的 OpenCV 入口启动 YOLO，确认 MPP 服务先退出、Qt 再释放 linuxfb，随后
   worker 才获得 shared backend lock、camera 和 fb。
2. 观察模型加载、输入输出 tensor 契约、RGB565 stride 输出以及推理帧率/内存/温度。
3. 退出 worker 或向外层监督器发送 `SIGUSR1`，确认 worker 释放 VIPLite、V4L2、mmap、
   fb 和 lock，随后 Qt/MPP 被完整重建。
4. 多次循环 Camera→YOLO→Camera；任何 `backend_busy`、残留进程或设备持续占用都应视为
   未通过，而不是重启设备掩盖。

## 8. 故障停止条件

遇到以下任一情况时，应停止综合测试并先保留现场：

- framebuffer 实际 handle 与配置不一致，或 Qt/MPP 使用同一 layer；
- `camera-gui`、Qt 子进程、MPP 服务或 YOLO worker 异常退出后仍占用设备/lock；
- loader 找不到共享库，或加载到与构建时不同来源的同名 vendor 库；
- kernel 出现 VIN/ISP/ION/DISP/VENC/VDEC/VIPLite/USB 错误；
- 可移动存储并未挂载、空间不足，或 `.part`/最终文件提交行为异常；
- UVC descriptor 与服务固定 format/frame index 不一致；
- RTSP server 因网络接口或客户端异常终止整个服务进程。

恢复前记录进程树、打开设备、挂载、内核日志和用户态日志。不要通过覆盖系统库、强制
删除未知目录或同时启动多个 backend 来试探性修复。

## 9. 回传信息

为了把实际结果写回 [`STATUS.md`](STATUS.md)，每次测试至少保留：

- handoff 归档 SHA-256，以及源码基线/工作树说明；
- 交叉编译器版本、target triple、sysroot/Qt/OpenCV 来源；
- MPP manifest、bundle `SDK_SOURCE.json`、`SDK_FEATURES.cmake` 和 `SHA256SUMS` 审核结果；
- 完整 CMake configure/build/install 日志和安装清单；
- 三个目标 ELF 的 `file`、`readelf -h`、`readelf -d` 结果；
- 板端 rootfs/BSP/kernel、sensor、LCD、USB gadget 和存储文件系统基线；
- 实际使用的 `mpp-service.conf`，以及 GUI/MPP/YOLO 标准输出和内核日志；
- 每个功能的操作序列、可观察结果、失败时刻和是否能无重启恢复；
- OPEN-012 的真实 framebuffer handle、MPP layer、alpha/z-order 和局部播放观察。

模型或 vendor 库若受授权限制，不要附在日志包中；只记录可公开的版本、哈希和来源。
单项板测成功只提升对应能力，不自动外推 Camera/Playback/UVC/YOLO 综合切换或产品验收。

## 当前 v853-100ask 依赖集成

Qt/OpenCV 的构建与安装位置见 [dependencies](../../../dependencies/README.md)。
项目 `build/stage-current-sdk` 是本地应用 staging，尚未写入 SDK rootfs 或板端。
部署前须同时带上 Qt linuxfb/JPEG 插件、OpenCV、当前 AWIspApi 及其同源动态依赖；
`build/full-sdk/elf-dependencies.json` 给出本次实际解析的 NEEDED 文件。
模型、类别、中文字体、sensor/ISP 配置、显示格式与 USB descriptor 仍由目标系统集成。
