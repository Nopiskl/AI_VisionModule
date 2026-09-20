# GitHub Release 资产与原路径恢复

本项目中的工具链、sysroot、MPP/VIPLite bundle、Qt/OpenCV 原始归档和安装产物都需要
保留，但不适合进入普通 Git 历史。它们不从本地工作树删除，而是按原始相对路径制作成
GitHub Release 资产；新的源码 checkout 下载资产后，可以一条命令恢复到当前项目路径。

## 1. 资产划分

默认版本 `20260920` 生成以下文件：

| Release 文件 | 包内原始路径 | 用途 |
| --- | --- | --- |
| `v851s-app-install-20260920.tar.zst` | `APP/install/` | 已交叉构建的三个程序、配置和 `camera-start` 成品安装树 |
| `v851s-app-sdk-dev-20260920.tar.zst` | `APP/sdk-dev/`、`APP/Application/third_party/sunxi-mpp-sdk/` | 离线交叉构建环境；同时包含 `APP/build.sh` 必需的 MPP SDK bundle |
| `v851s-qt-opencv-sources-20260920.tar.zst` | 下表列出的 12 个原始归档路径 | TinaSDKv4、DVP、MIPI 三套原路径下的 Qt/OpenCV/ADE 源码归档 |
| `SHA256SUMS` | Release 下载目录 | 三个资产的完整性校验 |
| `ASSET_CONTENTS.txt` | Release 下载目录 | 资产与原始路径的可读清单 |

Qt/OpenCV 源码资产保留以下精确路径，不合并、不重命名，也不只保留其中一份：

```text
TinaSDKv4.0/dl/ade-0.1.1d.zip
TinaSDKv4.0/dl/opencv-4.1.0.zip
TinaSDKv4.0/dl/opencv_contrib-4.1.0.zip
TinaSDKv4.0/dl/qt-5.12.9.tar.xz
TinaSDKv5.0/Project_for_DVP/platform/thirdparty/gui/qt/qt-5.12.9.tar.xz
TinaSDKv5.0/Project_for_DVP/platform/thirdparty/vision/opencv/ade-0.1.1d.zip
TinaSDKv5.0/Project_for_DVP/platform/thirdparty/vision/opencv/opencv-4.1.0.zip
TinaSDKv5.0/Project_for_DVP/platform/thirdparty/vision/opencv/opencv_contrib-4.1.0.zip
TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/gui/qt/qt-5.12.9.tar.xz
TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/vision/opencv/ade-0.1.1d.zip
TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/vision/opencv/opencv-4.1.0.zip
TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/vision/opencv/opencv_contrib-4.1.0.zip
```

`APP/Application/third_party/sunxi-mpp-sdk/` 虽然不在 `APP/sdk-dev/` 内，但它是当前
`APP/build.sh` 的直接输入，因此和 sdk-dev 放在同一个构建环境资产中。只恢复
`APP/sdk-dev/` 不能得到完整离线构建环境。

模型、硬件工程、BSP patch、OpenWrt package 配方和其他当前文件不由这三个资产移除；
它们仍保留在工作树。后续若决定把模型改为独立 Release，需要另行列出路径和授权边界，
不能在没有资产接替的情况下删除。

## 2. 本地生成 Release 资产

在仓库根目录执行：

```sh
./tools/package-release-assets.sh
```

默认输出到：

```text
release-assets/20260920/
```

脚本具有以下门禁：

- 所有输入路径必须存在；
- 不覆盖已有同名输出；
- tar 内保存从仓库根目录开始的原始相对路径；
- 固定排序、mtime、owner 和 group，便于内容不变时复现；
- 使用 zstd 压缩；
- 每个资产生成后必须小于 GitHub Release 单文件 2 GiB 限制；
- 最后生成 `SHA256SUMS` 和 `ASSET_CONTENTS.txt`。

修改版本或输出目录时使用：

```sh
RELEASE_ASSET_VERSION=20260920.1 \
RELEASE_ASSET_DIR=/absolute/output/v851s-release \
RELEASE_ZSTD_LEVEL=10 \
./tools/package-release-assets.sh
```

`release-assets/` 已由根 `.gitignore` 排除。资产应该上传到 GitHub Release，不能执行
`git add release-assets` 把它们放回普通 Git 历史。

## 3. 上传 GitHub Release

同一个 Release 中上传：

```text
v851s-app-install-<version>.tar.zst
v851s-app-sdk-dev-<version>.tar.zst
v851s-qt-opencv-sources-<version>.tar.zst
SHA256SUMS
ASSET_CONTENTS.txt
```

建议 Release tag 使用同一版本，例如 `assets-20260920`，并在说明中记录：

- 对应源码 commit；
- `SHA256SUMS`；
- 工具链和目标 ABI；
- Qt/OpenCV/MPP 来源；
- 公开或私有发布所依据的再分发权限。

本仓库脚本只在本地准备文件，不会自动创建 Release 或上传外部服务。公开发布第三方
源码、模型或厂商二进制前，仍需由项目维护者确认相应许可；没有公开权限时可使用私有
Release 或受控制品存储。

## 4. 下载后恢复到当前项目路径

先 clone 源码仓库，再把同一 Release 的三个 `.tar.zst` 和 `SHA256SUMS` 下载到一个独立
目录，例如：

```text
/absolute/download/v851s-assets/
├── SHA256SUMS
├── v851s-app-install-20260920.tar.zst
├── v851s-app-sdk-dev-20260920.tar.zst
└── v851s-qt-opencv-sources-20260920.tar.zst
```

在源码仓库根目录执行：

```sh
./tools/restore-release-assets.sh \
  /absolute/download/v851s-assets \
  all
```

脚本会先逐个校验 SHA-256，再恢复到：

```text
APP/install/
APP/sdk-dev/
APP/Application/third_party/sunxi-mpp-sdk/
TinaSDKv4.0/dl/...
TinaSDKv5.0/Project_for_DVP/platform/thirdparty/...
TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/...
```

脚本拒绝覆盖任何已有目标。如果当前工作树已经保留这些原文件，不需要恢复，也不要为
测试而覆盖；先对比 Release 的 SHA-256 即可。

也可以按需只恢复一类：

```sh
./tools/restore-release-assets.sh /absolute/download/v851s-assets install
./tools/restore-release-assets.sh /absolute/download/v851s-assets sdk-dev
./tools/restore-release-assets.sh /absolute/download/v851s-assets sources
```

手工解压时必须在仓库根目录的上层语义下恢复包内相对路径：

```sh
zstd -dc /absolute/download/v851s-assets/v851s-app-sdk-dev-20260920.tar.zst \
  | tar -xf - -C /absolute/path/to/v851s
```

不要在 `APP/` 内解压这个资产，否则包内的 `APP/sdk-dev` 会错误变成
`APP/APP/sdk-dev`。

## 5. 恢复后的检查和构建

确认关键路径：

```sh
test -x APP/build.sh
test -d APP/sdk-dev/toolchain
test -f APP/Application/third_party/sunxi-mpp-sdk/SDK_SOURCE.json
test -x APP/install/usr/bin/camera-gui
test -x APP/install/usr/bin/camera-mpp-service
test -x APP/install/usr/libexec/v851s-camera/camera-yolo-worker
test -f TinaSDKv4.0/dl/qt-5.12.9.tar.xz
test -f TinaSDKv5.0/Project_for_DVP/platform/thirdparty/vision/opencv/opencv-4.1.0.zip
test -f TinaSDKv5.0/Project_for_MIPI/platform/thirdparty/vision/opencv/opencv-4.1.0.zip
```

完整构建环境恢复后，从仓库根目录执行：

```sh
./APP/build.sh
```

板端部署仍按 [`BUILD_AND_DEPLOY.md`](BUILD_AND_DEPLOY.md) 操作。Release 中的
`APP/install/` 是保留原项目路径的成品安装树，不应直接解压到目标板 `/`；先在源码树
恢复，再从其中制作或选择板端运行包。

## 6. 更新资产的规则

当任一被打包路径发生变化时：

1. 使用新的 `RELEASE_ASSET_VERSION`，不要覆盖旧 Release 文件；
2. 重新生成三个资产和 `SHA256SUMS`；
3. 在干净源码 checkout 中执行恢复检查；
4. 确认 `APP/build.sh` 仍可定位工具链、Qt、OpenCV、ISP 和 MPP bundle；
5. 上传为新的 Release，并在源码文档中更新推荐资产版本；
6. 旧资产在确认不再需要前继续保留，避免破坏历史源码的可恢复性。
