# V851S AI 视觉模组 / 边缘协处理器

语言：[English](README.md) | **中文**

本仓库面向 Allwinner V851S/V85x 平台，保存当前经过开发板实际开发验证的 APP 交付目录、
硬件工程以及 Tina/OpenWrt 适配材料，可用于 AI 相机、无线图传和边缘视觉模块的继续开发。

> **当前应用唯一入口：** [`APP/`](APP/README.md)。原根目录 `Application/` 已退役；
> 应用源码、独立交叉开发环境、ARM 安装产物、板端启动脚本和应用文档现在统一位于
> `APP/` 下。

<p align="center">
  <img src="Image/MainUI.png" alt="AI Vision Module 工作模式选择主界面" width="900">
</p>

## 项目特性

应用使用 Qt 5.12.9 管理界面，并将媒体处理和 AI 推理拆分为三个独立进程。当前源码包含
Camera、相册、RTSP、UVC 输出、OpenCV 采集和 VIPLite YOLO 集成路径。

| 范围 | 当前实现 |
| --- | --- |
| 图形界面 | 800×480 Qt Widgets 逻辑界面，通过 `linuxfb` 旋转输出到 480×800 framebuffer |
| Camera | MPP 预览、JPEG 拍照、H.264/MP4 录像以及可选 RTSP 推流 |
| 相册 | Qt 图片显示和 MPP DEMUX/VDEC/VO 视频回放 |
| UVC | 使用系统预先配置好的 UVC gadget，把板卡作为 USB 摄像头输出 |
| AI/OpenCV | OpenCV/V4L2 采集和独立 VIPLite YOLO worker |
| 资源互斥 | MPP 与 YOLO 严格互斥；Qt UI 与 MPP 视频使用不同 DISP2 layer |
| 开发交付 | `APP/sdk-dev` 提供独立交叉开发树，`APP/install` 保存当前 ARM 安装产物 |

仓库包含真实开发板结果，但每项验证都有明确边界。准确的板端基线、已确认能力和仍待
验证的 UVC、YOLO 模型、第二路采集限制，请以
[`APP/Application/docs/STATUS.md`](APP/Application/docs/STATUS.md) 为准。

## 界面与板端效果

以下图片均来自仓库当前 [`Image/`](Image/) 目录。

| 开发板主界面 | Camera 预览与控制 | MPP/AI 检测演示 |
| --- | --- | --- |
| <img src="Image/MainUI-2.png" alt="开发板实际运行的主界面" width="100%"> | <img src="Image/camera.png" alt="开发板实际运行的 Camera 页面" width="100%"> | <img src="Image/yolov5_MPP.png" alt="开发板 MPP 与 AI 检测演示" width="100%"> |

| 相册界面 | UVC 界面 |
| --- | --- |
| <img src="Image/Album.png" alt="照片和视频相册回放界面" width="100%"> | <img src="Image/UVC.png" alt="UVC 输出界面" width="100%"> |

<p align="center">
  <img src="Image/OPENCV.png" alt="OpenCV 采集效果" width="780">
</p>

## 最新目录结构

```text
.
├── APP/                    当前 APP 交付与开发目录
│   ├── Application/        权威应用源码和顶层 CMake 工程
│   ├── sdk-dev/            ARM 工具链、sysroot、Qt/OpenCV/ISP 开发文件
│   ├── install/            当前构建生成的 ARM 安装树
│   ├── runtime/            板级环境配置和 camera-start 启动脚本
│   ├── patches/            随交付保留的 SDK/MPP 迁移记录
│   ├── build.sh            当前支持的离线交叉构建入口
│   └── APP_OVERVIEW.md     APP 架构与当前默认参数说明
├── Hardware/
│   ├── Project_for_DVP/    DVP 硬件工程、原理图、渲染图和 STEP 文件
│   └── Project_for_MIPI/   MIPI 硬件工程、原理图、Gerber 和 STEP 文件
├── Image/                  本 README 使用的 UI 效果图和开发板照片
├── tools/                  Release 资产打包和原路径恢复脚本
├── TinaSDKv4.0/            Qt/OpenCV/AWIspApi 包和源码输入
├── TinaSDKv5.0/
│   ├── Project_for_DVP/    DVP Tina/OpenWrt 板级覆盖和软件包
│   └── Project_for_MIPI/   MIPI Tina/OpenWrt 板级覆盖和软件包
├── BUILD_AND_DEPLOY.md     当前编译、运行包制作和板端部署指南
├── RELEASE_ASSETS.md       GitHub Release 资产划分和路径恢复指南
├── README.md               英文项目入口
└── README_CN.md            中文项目入口
```

`APP/` 是仓库根目录下唯一的应用入口。旧路径 `Application/...`、`01_APP/...` 和
`TinaSDKv5.0/qt_project` 均属于旧目录布局，不应继续使用。


## 应用架构

| 程序 | 当前安装路径 | 职责 |
| --- | --- | --- |
| `camera-gui` | `APP/install/usr/bin/camera-gui` | Qt 界面、用户交互、后端进程监督和模式切换 |
| `camera-mpp-service` | `APP/install/usr/bin/camera-mpp-service` | 独占管理预览、显示、拍照、录像、RTSP、回放和 UVC 的 MPP 资源 |
| `camera-yolo-worker` | `APP/install/usr/libexec/v851s-camera/camera-yolo-worker` | YOLO 会话中的 OpenCV 采集、VIPLite 推理和 framebuffer 输出 |
| `camera-start` | `APP/install/usr/bin/camera-start` | 可搬移的板端统一启动入口 |

GUI 通过本地 IPC 控制 MPP 服务。进入 YOLO 前，GUI 外层监督器先停止 MPP 并释放 Qt
framebuffer；YOLO worker 退出后再重建 Qt/MPP。Camera 和相册会话中，Qt 独占 UI
framebuffer layer，MPP 使用独立 video layer，由 DISP2 完成硬件合成。

## 编译和部署

当前 `APP/` 已包含交叉工具链、目标 sysroot、目标 Qt/OpenCV 开发文件、导入的 MPP SDK、
便携 CMake 和当前安装树。构建不需要把源码复制进 Tina SDK，也不会联网下载依赖。

在仓库根目录执行：

```sh
./APP/build.sh
```

默认构建目录是 `APP/build`，安装暂存目录是 `APP/install`。当前三个程序均为 ARM EABI5
可执行文件，使用 musl hard-float loader `/lib/ld-musl-armhf.so.1`。

构建目录覆盖、运行包制作、模型放置、板端配置和启动方式见
[`BUILD_AND_DEPLOY.md`](BUILD_AND_DEPLOY.md)。APP 的简要架构和默认参数见
[`APP/APP_OVERVIEW.md`](APP/APP_OVERVIEW.md)。
大型构建依赖和安装成品通过保留原路径的 GitHub Release 资产保存，打包和恢复方法见
[`RELEASE_ASSETS.md`](RELEASE_ASSETS.md)。

## 当前默认板级配置

机器可读默认值由
[`APP/Application/configs/mpp-service.conf`](APP/Application/configs/mpp-service.conf)
和 [`APP/runtime/board-env.sh`](APP/runtime/board-env.sh) 管理。当前配置包括：

- 480×800 物理 framebuffer/VO 画布，Qt 逻辑界面为 800×480；
- Qt `linuxfb` 旋转 90 度，并使用当前触摸屏坐标变换；
- VIPP0 以 480×800 负责预览；
- VIPP4 按需以 1280×720 负责拍照、录像和 RTSP，固件必须启用对应节点；
- RTSP 默认网卡 `eth0`、端口 `8554`、流名 `ch0`；
- UVC 输出节点 `/dev/video2`；
- 媒体路径默认位于 `/mnt/extsd`，如果它不是实际数据盘挂载点，必须修改配置。

其他板卡的 layer handle、触摸节点、媒体挂载点、摄像头节点和 USB gadget 配置不能直接
照搬这些默认值。

## 硬件与 BSP 版本

原 README 中的硬件渲染图和 PCB 布局图继续保留如下，原始图片仍位于对应硬件工程目录。

| DVP 板卡渲染图 | DVP PCB 布局图 |
| --- | --- |
| <img src="Hardware/Project_for_DVP/1.png" alt="DVP 板卡渲染图" width="100%"> | <img src="Hardware/Project_for_DVP/2.png" alt="DVP PCB 布局图" width="100%"> |

<p align="center">
  <img src="Hardware/Project_for_MIPI/1.png" alt="MIPI PCB 布局图" width="900">
</p>

| 版本 | 硬件工程 | Tina/OpenWrt 材料 | 主要用途 |
| --- | --- | --- | --- |
| DVP | `Hardware/Project_for_DVP` | `TinaSDKv5.0/Project_for_DVP` | DVP 摄像头和相对简单的板级启动路线 |
| MIPI | `Hardware/Project_for_MIPI` | `TinaSDKv5.0/Project_for_MIPI` | MIPI/ISP、完整厂商媒体栈和主综合应用路线 |

`TinaSDKv4.0/` 保存开发环境所需的 Qt 5.12.9、OpenCV 4.1.0 和 AWIspApi 源码/包输入。
`TinaSDKv5.0/Project_for_*` 保存板级覆盖和软件包修改，并不是完整、可独立编译的 Tina SDK
checkout。

## 平台资料

- [Tina Linux 开发资料](https://tina.100ask.net/)
- [全志开源社区 SDK 获取指南](https://v853.docs.aw-ol.com/study/study_3getsdktoc/)
- [柚木 PI-V851S 开发资料](https://forums.100ask.net/t/topic/3009)
- [MPP 使用参考](https://forums.100ask.net/t/topic/3107)
- [E907 开发参考](https://forums.100ask.net/t/topic/7119)
