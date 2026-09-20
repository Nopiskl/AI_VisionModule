> 最终交付的直接构建入口：`../build.sh`。使用说明见 [APP 编译与部署](../../00_GUIDE/BUILD_AND_DEPLOY.md)，无需再应用 BUG 或 MPP 补丁。

# V851S Camera Application

`Application` 是板端 Camera 产品程序的权威源码树：一套顶层 CMake 生成 Qt GUI、
单一 MPP 服务和独立 YOLO worker。Tina/OpenWrt 只作为工具链、BSP、rootfs 和薄
package adapter，不维护第二份业务源码。

## 当前目标

| CMake target | 产物 | 职责 |
| --- | --- | --- |
| `camera_gui` | `camera-gui` | 800×480 Qt Widgets、后端监督和 Home/Camera/Album/UVC/OpenCV 入口 |
| `camera_mpp_service` | `camera-mpp-service` | 唯一 MPP owner；Camera/可选 RTSP/Playback/UVC Out |
| `camera_yolov8_opencv` | `camera-yolo-worker` | OpenCV/V4L2 + VIPLite YOLO worker；互斥接管 camera/fb |

`APPLICATION_BUILD_MPP_EXAMPLES=ON` 还会构建 sample 派生的 Preview、RTSP 和
Recording reference。它们只用于迁移/调用顺序参考，不是产品服务。

当前实现、已确认事实和阻塞项以 [`docs/STATUS.md`](docs/STATUS.md) 为准。源码存在不
表示已交叉编译或板测；产品边界见
[`docs/architecture/README.md`](docs/architecture/README.md)，
文档入口和事实归属见 [`docs/README.md`](docs/README.md)，MPP/MPI 开发入口见
[`docs/platform/mpp/`](docs/platform/mpp/)，组件设计入口见
[`docs/components/`](docs/components/)。后续 Agent 先阅读
[`agent.md`](agent.md)。

把源码 handoff 带到交叉编译环境或 V851S 目标板时，按
[`实际环境构建与首次运行`](docs/deployment-bringup.md) 补齐工具链、目标 Qt/OpenCV、
本地 MPP bundle、模型和 rootfs 依赖，并按顺序保留构建与板端证据。

## 导入当前 SDK 并交叉编译

在 Ubuntu 主机上执行（Windows 浏览共享目录时先 SSH 到 Ubuntu）。正式依赖来自
当前 SDK 已构建的 staging 目录；旧 Yuzukilizard 二进制 bundle 与当前内核 ABI 不兼容。

```sh
cd /home/ubuntu/tina-v853-100ask/V85X_AIModule/Project/v851s-camera-handoff-20260915/Application
export SUNXI_TINA_SDK_ROOT=/home/ubuntu/tina-v853-100ask

# 新环境第一次导入；目的目录已存在时不会覆盖。
python3 tools/import_sunxi_mpp.py --sdk-root "$SUNXI_TINA_SDK_ROOT"
bash tools/audit_sunxi_mpp.sh

# CMake >= 3.16；当前环境使用已下载并校验的便携 CMake。
export CMAKE_BIN="$PWD/../build/host-tools/cmake-3.27.9-linux-x86_64/bin/cmake"
bash tools/build_current_sdk.sh
```

当前目录已经导入，后续可直接执行构建脚本。脚本先比较 bundle/SDK 来源和 ARM ABI，
再默认构建 MPP 产品服务，产物为
`../build/current-sdk/services/camera-mpp-service/camera-mpp-service`。
`APPLICATION_BUILD_DIR` 可指定另一树外构建目录。

导入包括同源头文件、MPP/ISP、编解码器、MUX/DEMUX、TinyServer、传递依赖、
运行库、VIPLite 和内核显示 UAPI。来源记录在 bundle 的 `SDK_SOURCE.json`、
`SDK_FEATURES.cmake`、`BUNDLE_MANIFEST`、`SHA256SUMS`。
`fetch_sunxi_mpp.sh` 是本地导入兼容入口，要求 `SUNXI_TINA_SDK_ROOT`，
不再下载 GitHub 预编译库。历史参考树仍可由 `prepare_references.sh` 单独准备。

GUI 需要与当前 ARM/musl 工具链匹配的 Qt 5.12+，完整 YOLO worker 需要目标 OpenCV。
准备好依赖后，以独立构建目录接入：

```sh
APPLICATION_BUILD_DIR="$PWD/../build/full-sdk" bash tools/build_current_sdk.sh \
  -DAPPLICATION_BUILD_GUI=ON \
  -DQt5_DIR=/absolute/target-qt/lib/cmake/Qt5 \
  -DAPPLICATION_BUILD_YOLOV8=ON \
  -DOpenCV_DIR=/absolute/target-opencv/lib/cmake/opencv4
```

交叉链接成功不能代替板端运行。实际能力和依赖构建进度见
[docs/STATUS.md](docs/STATUS.md)。

## 运行和配置

MPP 服务：

```sh
/usr/bin/camera-mpp-service \
  --config /etc/v851s-camera/mpp-service.conf
```

GUI：

```sh
/usr/bin/camera-gui \
  --service /usr/bin/camera-mpp-service \
  --service-config /etc/v851s-camera/mpp-service.conf \
  --yolo /usr/libexec/v851s-camera/camera-yolo-worker \
  --yolo-model /etc/v851s-camera/yolov8.nb \
  --yolo-classes /etc/v851s-camera/yolov8-classes.txt
```

`camera-gui` 的外层进程不打开 framebuffer；它启动 Qt UI 子进程。首页 OpenCV 入口
选择 YOLO 后，UI
先停止 MPP 并退出以释放 linuxfb，再启动 worker 直接输出 framebuffer。worker 退出后
自动重建 Qt/MPP；YOLO 运行期间可向外层监督器发送 `SIGUSR1` 请求返回 Qt。模型和类别
文件属于部署输入，仓库不会自动安装未提供的模型。

GUI 当前固定为 800×480。Camera 使用透明 Qt overlay；相册照片由 Qt 显示，视频由 MPP
在局部 VO 矩形显示。页面接线、状态真值和后续修改约束见
[`GUI 集成文档`](docs/components/camera-gui/ui-integration.md)。目标 rootfs 还需提供可用的
中文字体（主题首选 Noto Sans CJK SC）和 Qt JPEG imageformat plugin。

机器可读默认值在 [`configs/mpp-service.conf`](configs/mpp-service.conf)，参数语义在
[`camera-mpp-service 配置`](docs/components/camera-mpp-service/configuration.md)，IPC 线格式在
[`docs/interfaces/ipc-v1.md`](docs/interfaces/ipc-v1.md)。

UVC 模式把板卡作为 USB Device 使用：服务只打开系统已配置并绑定好的 gadget
`VIDEO_OUTPUT` 节点，Host STREAMON 后才创建 ISP/VI/VENC 路径。USB configfs gadget 的
创建、UDC 绑定以及与 ADB composite function 的兼容属于部署范围，本阶段不由产品服务
修改。协议和 sample 映射见 [`UVC 平台文档`](docs/platform/mpp/uvc.md)。

## 目标运行环境边界

standalone 构建仍依赖与导入 archive 兼容的目标环境：32-bit ARMv7 hard-float、
musl/libstdc++/libgcc、V851S MPP/ION/VIN/ISP/DISP/VENC/VDEC 驱动接口、sensor DTS/
firmware/ISP tuning，以及 manifest 选择的运行库。`APPLICATION_INSTALL_VENDOR_RUNTIME`
只安装启用目标需要的 vendor runtime；rootfs 已提供 ABI 兼容版本时应关闭，避免
静默替换系统库。

## 当前 SDK 的完整构建入口

用户提供的 Qt 5.12.9 覆盖包及 TinyVision OpenCV 4.1.0 已在当前 SDK 下完成适配。
在 `../../dependencies` 执行 `bash build-application-current.sh` 可重复构建三个产品程序，
并安装到 `../build/stage-current-sdk`。依赖来源、补丁、构建顺序及运行库目录见
[dependencies/README.md](../../dependencies/README.md)。目标板能力仍以
[docs/STATUS.md](docs/STATUS.md) 为准。
