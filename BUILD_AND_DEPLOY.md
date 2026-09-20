# APP 编译与部署

本文对应当前仓库的 [`APP/`](APP/README.md) 目录。

GitHub 源码 checkout 中的 `APP/install`、`APP/sdk-dev`、MPP SDK bundle 和 Qt/OpenCV
原始归档由 Release 资产恢复，且保持当前相对路径不变。首次使用先按
[`RELEASE_ASSETS.md`](RELEASE_ASSETS.md) 下载、校验和恢复所需资产。

## 1. 交付内容和主机要求

构建主机使用 Linux x86_64，需要 Bash、GNU Make 和基础 GNU 工具。`APP/sdk-dev` 已包含：

- ARM musl hard-float 交叉工具链和 sysroot；
- CMake 3.27.9 主机工具；
- Qt 5.12.9 target/host 开发文件；
- OpenCV 4.1.0、AWIspApi/ISP adapter 和目标依赖；
- 与当前 SDK 配套的 MPP/VIPLite 开发输入。

Windows 共享目录可用于浏览和编辑，但应通过 SSH 在 Linux 主机执行构建。复制 `APP/`
时使用能保留权限和符号链接的方法，例如 `cp -a` 或 tar；不要用普通文件复制破坏
`APP/sdk-dev/toolchain` 中的符号链接。

板端固件必须提供匹配的 musl/libstdc++、Qt 运行库和插件、OpenCV、AWIspApi、MPP、
VIPLite，以及对应的 VIN/ISP/DISP/VENC/VDEC/USB 内核驱动。树外工具链能完成编译，不会
自动改变板端 ABI 或驱动能力。

## 2. 编译

在仓库根目录执行：

```sh
./APP/build.sh
```

也可以在 APP 目录执行：

```sh
cd APP
./build.sh
```

脚本默认同时构建：

- `camera-gui`；
- `camera-mpp-service`；
- `camera-yolo-worker`。

默认树外构建目录为 `APP/build`，安装暂存目录为 `APP/install`。脚本不会重新构建 Qt、
OpenCV 或 MPP，也不会联网下载依赖。

使用新的绝对路径覆盖构建目录、安装目录或并行数：

```sh
APPLICATION_BUILD_DIR=/absolute/build/v851s-camera \
APPLICATION_INSTALL_DIR=/absolute/stage/v851s-camera \
BUILD_JOBS=4 \
./APP/build.sh
```

额外 CMake 参数可以直接追加到脚本命令末尾。除非正在更换底层 SDK，不需要手工运行
`APP/Application/tools/` 中的 SDK 导入工具。

## 3. 构建输出

默认安装树中的主要文件如下：

| 路径（相对 `APP/install`） | 内容 |
| --- | --- |
| `usr/bin/camera-gui` | Qt GUI、进程监督和模式切换 |
| `usr/bin/camera-mpp-service` | Camera、Playback、Record、RTSP、UVC 的唯一 MPP 服务 |
| `usr/libexec/v851s-camera/camera-yolo-worker` | OpenCV/VIPLite YOLO 工作进程 |
| `usr/bin/camera-start` | 可搬移的统一启动脚本 |
| `etc/v851s-camera/mpp-service.conf` | MPP 服务和媒体通路配置 |
| `etc/v851s-camera/board-env.sh` | Qt framebuffer、旋转和触摸环境配置 |

`APP/install/usr/include` 和 `APP/install/usr/lib/libcamera_common.a` 是开发产物，不需要复制
到只运行应用的板端目录。

部署前至少确认三个 ELF 的架构和 loader：

```sh
file APP/install/usr/bin/camera-gui
file APP/install/usr/bin/camera-mpp-service
file APP/install/usr/libexec/v851s-camera/camera-yolo-worker

readelf -h APP/install/usr/bin/camera-gui
readelf -l APP/install/usr/bin/camera-gui | grep 'Requesting program interpreter'
```

当前交付应显示 ARM EABI5，并使用 `/lib/ld-musl-armhf.so.1`。这只能确认文件格式，不能
替代板端动态库、驱动和设备节点检查。

## 4. 制作运行包

从当前安装树制作只含运行文件的归档：

```sh
V851S_APP_ROOT=/absolute/path/to/repository/APP

tar -C "${V851S_APP_ROOT}/install" \
  -czf "${V851S_APP_ROOT}/app-runtime.tar.gz" \
  usr/bin/camera-gui \
  usr/bin/camera-mpp-service \
  usr/bin/camera-start \
  usr/libexec/v851s-camera/camera-yolo-worker \
  etc/v851s-camera
```

运行归档不包含模型，也不会安装板端系统动态库。根据实际部署授权，单独准备：

```text
etc/v851s-camera/yolov8.nb
etc/v851s-camera/yolov8-classes.txt
```

模型必须与当前 worker 的输入、输出 tensor 和类别顺序匹配。

## 5. 板端部署与启动

示例部署到板端可写数据盘：

```sh
mkdir -p /mnt/UDISK/ai-module
tar -xzf /path/to/app-runtime.tar.gz -C /mnt/UDISK/ai-module
/mnt/UDISK/ai-module/usr/bin/camera-start
```

`camera-start` 会根据自身位置计算应用根目录，因此整个 `ai-module` 目录可以移动。它会
加载同一运行树中的 `etc/v851s-camera/board-env.sh`，然后启动外层 `camera-gui` 监督器。
不要再并行手工启动第二个 `camera-mpp-service` 或另一个占用 camera/ISP/framebuffer 的
程序。

需要覆盖配置或模型时可以使用：

```sh
CAMERA_CONFIG=/absolute/path/to/mpp-service.conf \
CAMERA_YOLO_MODEL=/absolute/path/to/yolov8.nb \
CAMERA_YOLO_CLASSES=/absolute/path/to/yolov8-classes.txt \
/mnt/UDISK/ai-module/usr/bin/camera-start
```

## 6. 板级配置

应用部署前必须核对以下文件：

- [`APP/Application/configs/mpp-service.conf`](APP/Application/configs/mpp-service.conf)：
  VIPP、分辨率、VO、相册矩形、RTSP、存储和 UVC 节点；
- [`APP/runtime/board-env.sh`](APP/runtime/board-env.sh)：framebuffer、Qt 旋转、触摸节点、
  触摸旋转和坐标范围。

当前默认配置是：

| 项目 | 默认值 |
| --- | --- |
| Qt 逻辑界面 | 800×480 |
| framebuffer/VO 物理画布 | 480×800 |
| Qt 输出 | `/dev/fb0`，rotation=90 |
| 触摸 | `/dev/input/event3`，rotate=270，range=480x800 |
| Preview | VIPP0，480×800，NV21 |
| Snapshot/Record/RTSP 媒体采集 | 按需 VIPP4，1280×720 |
| RTSP | `rtsp://<board-ip>:8554/ch0`，网卡 `eth0` |
| UVC | `/dev/video2`，要求系统已经创建并绑定 gadget |
| 录像目录 | `/mnt/extsd/v851s-camera` |
| 相册根目录 | `/mnt/extsd` |

当前验证固件上的 `/mnt/extsd` 曾是小容量 boot-resource 分区，不能仅因目录存在就作为
录像盘。正式运行前应确认它是实际挂载的数据介质，或者修改 `storage.root`、
`playback.media_root` 和挂载检查策略。

VIPP4 必须由固件启用并提供对应节点；若不存在，拍照、录像和 RTSP 应明确返回媒体采集
不可用，而不能把低分辨率 Preview 静默放大。UVC gadget descriptor、UDC 绑定及与 ADB
的组合方式属于系统部署配置，不由应用创建。

## 7. 当前验证边界

当前 APP 来自具体开发板上的适配与验证目录。已确认内容、测试固件、显示/触摸参数、
Camera、回放、RTSP、OpenCV 和退出恢复结果记录在
[`APP/Application/docs/STATUS.md`](APP/Application/docs/STATUS.md)。

仍需按目标固件和部署输入继续确认的范围包括：

- 固件启用后的 VIPP4 拍照、录像和 RTSP；
- UVC descriptor、Host 取流和断连重连；
- 匹配模型下的 NPU 推理和 Qt→YOLO→Qt 完整循环；
- 真实网络客户端、存储异常、断电恢复和长稳；
- 另一块板卡上的 framebuffer layer、触摸、camera 和 USB 节点。

源码存在、交叉构建成功和单项板测不能自动外推为所有硬件与组合场景均已验收。
