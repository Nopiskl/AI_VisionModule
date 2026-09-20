# APP 目录

`APP/` 是当前应用的唯一开发、构建和部署入口，来源于实际开发板适配后的完整 APP
目录。仓库根目录旧 `Application/` 已退役，不应再作为源码或文档入口。

## 目录内容

| 路径 | 用途 |
| --- | --- |
| [`Application/`](Application/README.md) | Qt GUI、MPP 服务、YOLO worker、公共库、配置和详细设计文档 |
| `sdk-dev/` | ARM musl 工具链、sysroot、Qt/OpenCV/ISP 开发文件和便携 CMake |
| `install/` | 当前 ARM 安装暂存树，可用于制作板端运行包 |
| `runtime/` | `camera-start` 和板级 Qt 显示/触摸环境 |
| `patches/` | 随交付保留的 SDK/MPP 迁移记录 |
| [`build.sh`](build.sh) | 当前支持的离线交叉构建入口 |
| [`APP_OVERVIEW.md`](APP_OVERVIEW.md) | 三进程架构、资源关系和当前默认参数 |

## 快速构建

从仓库根目录执行：

```sh
./APP/build.sh
```

或在本目录执行：

```sh
./build.sh
```

默认构建目录为 `APP/build`，安装目录为 `APP/install`。脚本使用 `sdk-dev` 中随交付保存的
工具链和依赖，不需要把源码复制进 Tina SDK，也不会联网下载依赖。

完整的构建覆盖参数、运行包制作、板端配置、模型放置和启动命令见
[`../BUILD_AND_DEPLOY.md`](../BUILD_AND_DEPLOY.md)。真实板端能力和未完成项见
[`Application/docs/STATUS.md`](Application/docs/STATUS.md)。
从 GitHub 源码 checkout 恢复 `sdk-dev`、MPP SDK、`install` 和 Qt/OpenCV 原始归档的
方法见 [`../RELEASE_ASSETS.md`](../RELEASE_ASSETS.md)。

## 运行入口

默认安装树包含：

```text
install/usr/bin/camera-gui
install/usr/bin/camera-mpp-service
install/usr/libexec/v851s-camera/camera-yolo-worker
install/usr/bin/camera-start
install/etc/v851s-camera/mpp-service.conf
install/etc/v851s-camera/board-env.sh
```

正常板端运行只启动 `camera-start`。它会启动外层 GUI 监督器，由监督器管理 MPP 服务和
YOLO worker 的互斥切换；不要手工并行运行第二套 backend。
