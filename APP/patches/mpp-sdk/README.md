# MPP SDK API 差异补丁

## 当前 APP 的状态

`../../Application` 已包含本补丁及后续 BUG、显示、退出和板级配置修复。**直接构建当前 APP；不要重复应用本补丁，也不要反向应用它来切换 SDK。**

此目录只保留从原 handoff 使用的 Yuzukilizard MPP 接口，迁移到当前 V853 TINA4 MPP 的差异。它不是任意 TINA4/TINA5 或 SoC 之间的通用兼容开关。

## 文件和基线

| 文件 | 用途 |
|---|---|
| 0001-handoff-to-current-sdk-mpp.patch | MPP API、结构字段、链接依赖和 SDK 导入的移植差异 |
| handoff-original.tar.gz | 精确适用基线：原 v851s-camera-handoff-20260915 |

补丁是相对原始 Application 的第一层迁移差异。保留原包是为了让补丁有明确、可直接应用的基线；日常开发以已修复的当前 Application 为准。

## 差异范围

- MUX：原 group/channel 调用模型迁移到当前 SDK 的 channel API。
- VENC/ISP：码控字段、ISP2VE 参数类型与同步回调适配。
- DEMUX/VDEC/CLOCK：输入方式、结构字段、接口与 codec 功能开关适配。
- 头文件/库：改为当前 SDK 成套导入；限制公开 include 路径，处理静态库依赖闭包。
- 工具：SDK 库导入、来源描述和 ABI 检查入口。

MPP 二进制、完整头文件不塞进文本补丁。它们在当前 APP 的 third_party/sunxi-mpp-sdk 内；更换 SDK 时应重新导入匹配的一套。

## 在原 handoff 上应用

仅在需要重现 API 迁移、或将差异移植到同源旧分支时操作。使用新的空工作目录，不在当前 Application 中执行：

```bash
PORT=/path/to/AI_module_final/01_APP/patches/mpp-sdk
WORK=/path/to/new-handoff-port
mkdir -p "$WORK"
tar -xzf "$PORT/handoff-original.tar.gz" -C "$WORK"
cd "$WORK/v851s-camera-handoff-20260915/Application"

patch --dry-run --batch --forward --fuzz=0 -p1 \
  < "$PORT/0001-handoff-to-current-sdk-mpp.patch"
patch --batch --forward --fuzz=0 -p1 \
  < "$PORT/0001-handoff-to-current-sdk-mpp.patch"
```

上述结果是“原 handoff + MPP API 迁移”，不包含随后合入当前交付的全部 APP BUG 修复。需要完整 APP 时直接使用 `01_APP/Application`，不以此临时目录替代它。

已有分支若与基线不同，先用 dry-run 查看冲突，逐处对照 SDK 头文件和 sample 合并，不使用强制或忽略错误的方式覆盖。

## 重新导入当前 SDK 的 MPP

在 API 已匹配当前 SDK 的源码中，可使用：

```bash
python3 tools/import_sunxi_mpp.py \
  --sdk-root /path/to/tina-v853-sdk \
  --board v853-100ask \
  --destination /path/to/new-sunxi-mpp-sdk
```

导入到新目录后再替换开发包并调整构建入口。当前 SDK 的库不能与旧 GitHub bundle 的头文件混用；其他 SDK 还需同步调整导入布局与 API，不能只复制 .so。
