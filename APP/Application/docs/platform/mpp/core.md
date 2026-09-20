# MPP 基础：SYS、绑定与资源所有权

本页给出 VI、VO、VENC、VDEC 共同依赖的 MPI 基础模型。接口签名以实际 sysroot 为准；
这里列出的核心符号均能在当前 sun8iw21 参考头文件中找到。

对应公共头文件：`mpi_sys.h`、`mm_comm_sys.h`、`mm_common.h`；各模块还要包含自己的
`mpi_<module>.h` 与 `mm_comm_<module>.h`。

## 1. 先理解对象，而不是背 sample 顺序

MPP 用“模块 + 设备 + 通道”标识一个可连接端点：

```c
typedef struct MPP_CHN_S {
    MOD_ID_E mModId;
    int mDevId;
    int mChnId;
} MPP_CHN_S;
```

常见映射如下。`mDevId` 在不同模块中的含义并不完全相同，不能统一理解为硬件设备号。

| 端点 | `mModId` | `mDevId` | `mChnId` |
| --- | --- | --- | --- |
| VI 输出 | `MOD_ID_VIU` | VIPP/VI device | VirChn |
| VENC | `MOD_ID_VENC` | sun8iw21 sample 通常为 `0` | VENC channel |
| VDEC | `MOD_ID_VDEC` | sun8iw21 sample 通常为 `0` | VDEC channel |
| VO 输入 | `MOD_ID_VOU` | VO video layer handle | VO channel |
| DEMUX/CLOCK/MUX | 对应 `MOD_ID_*` | 含义随模块 ABI 变化 | 对应 channel |

channel ID、layer handle 和 VIPP ID 是运行资源，不是功能名称。sample 中的 `0`、`4`、
`HLAY(2, 0)` 只能说明示例如何填字段，不能作为其他板卡或产品的默认值。

## 2. SYS 生命周期

最小进程级生命周期：

```text
清零 MPP_SYS_CONF_S
-> 设置 nAlignWidth
-> AW_MPI_SYS_SetConf
-> AW_MPI_SYS_Init
-> 创建并运行各业务模块
-> 停止、解绑并销毁全部业务模块
-> AW_MPI_SYS_Exit
```

| API | 作用 | 关键约束 |
| --- | --- | --- |
| `AW_MPI_SYS_SetConf` | 设置全局系统配置 | 在 `AW_MPI_SYS_Init` 前调用；`nAlignWidth` 的合法范围看当前 `mm_comm_sys.h` |
| `AW_MPI_SYS_GetConf` | 读取系统配置 | 只用于核对，不代替应用自己的配置 owner |
| `AW_MPI_SYS_Init` | 初始化 MPP 运行环境 | 成功后才可创建业务组件 |
| `AW_MPI_SYS_Exit` | 退出 MPP | 必须在所有 worker、绑定和业务组件释放之后 |
| `AW_MPI_SYS_GetVersion` | 获取 MPP 版本字符串 | 建议写入启动日志，便于绑定运行证据与 ABI |
| `AW_MPI_SYS_GetCurPts` | 读取系统 PTS | PTS 相关单位和同步策略以当前头文件/平台为准 |
| `AW_MPI_SYS_InitPtsBase` | 初始化 PTS 基准 | 多媒体时钟需要统一基准时使用 |
| `AW_MPI_SYS_SyncPts` | 同步 PTS 基准 | 适合长时间运行的时钟校正，不应在数据线程中频繁调用 |

`AW_MPI_SYS_Init_S1`、`AW_MPI_SYS_Init_S2`、`AW_MPI_SYS_Init_S3` 在 sun8iw21 头文件中
存在，但普通应用不应自行拆分初始化；除非当前 BSP 明确要求，否则使用完整的
`AW_MPI_SYS_Init`。

推荐由进程内唯一的 runtime owner 管理 SYS 引用和退出。任何模块仍在运行时调用
`AW_MPI_SYS_Exit` 都属于生命周期错误。

## 3. 通用状态骨架

MPI 隐藏了大部分底层 component state 切换，但应用仍需遵守以下骨架：

```text
不存在
-> Create：对象存在，通常处于可配置/Idle 状态
-> SetAttr/RegisterCallback/Bind
-> Enable/Start：进入产出或消费数据的运行状态
-> 可选 Pause/Resume：只适用于声明支持的模块
-> Stop/Disable：回到可配置/可销毁状态
-> UnBind/Destroy：对象消失
```

并非每个模块都同时提供 `Create`、`Enable` 和 `Start`：VI 把 VIPP 与 VirChn 分开，VO
把 device/layer/channel 分开，VENC/VDEC 主要以 channel 为生命周期对象。上图表示共同
约束，不是可以原样粘贴的 API 序列。

静态属性尽量在 Start/Enable 前设置；只有当前头文件和实现明确允许的字段才在运行中
修改。应用不应绕过 MPI 直接操作内部 `COMP_STATETYPE`，也不能通过多次 Start/Stop
猜测状态；wrapper 应保存自己的 created/enabled/started flag。

## 4. Tunnel 与 non-tunnel

MPP 有两种数据传递方式：

```text
tunnel:
Source --AW_MPI_SYS_Bind--> Sink
MPP 内部传递 buffer，并在组件之间完成归还

non-tunnel:
应用 SendFrame/SendStream -> 组件 -> GetFrame/GetStream -> 应用 Release
```

| 模式 | 应用负责什么 | 适合场景 |
| --- | --- | --- |
| Tunnel | 创建端点、Bind、启动、停止、UnBind | VI→VO、VI→VENC、DEMUX→VDEC、VDEC→VO |
| Non-tunnel | 输入数据、输出取回、所有超时和 Release | JPEG 单拍、裸流编码/解码、VENC→RTSP 用户态分发 |

同一端口不能因为“想多取一份数据”就同时按 tunnel 和 non-tunnel 使用。例如，同一个
已绑定的 VI VirChn 不应再调用 `AW_MPI_VI_GetFrame`；需要手动 YUV 时应规划独立 VirChn
或经目标平台验证的分流机制。

## 5. Bind/UnBind

核心接口：

```c
ERRORTYPE AW_MPI_SYS_Bind(MPP_CHN_S *src, MPP_CHN_S *dst);
ERRORTYPE AW_MPI_SYS_BindExt(MPP_CHN_S *src, MPP_CHN_S *dst,
                             MppBindControl *control);
ERRORTYPE AW_MPI_SYS_UnBind(MPP_CHN_S *src, MPP_CHN_S *dst);
ERRORTYPE AW_MPI_SYS_GetBindbyDest(MPP_CHN_S *dst, MPP_CHN_S *src);
```

推荐的部分顺序是：

```text
两端 Create 完成
-> 配置静态属性和 callback
-> Bind
-> 启动 Sink/Filter 的接收能力
-> 启动 Source 产出数据
```

停止时先阻止新数据和等待 worker 收敛，再按绑定图反向解绑。实际 Start 先后会因模块
而异，因此各模块页面提供自己的建议顺序。

每次 Bind 成功后都要记录 `(src,dst)`，只有记录成功的边才执行 UnBind。创建中途失败时
不得“猜测性解绑”未建立的关系。

`BindExt` 的控制字段随平台演进，当前没有明确需求时使用 `Bind`；不要从其他 SoC 的
`MppBindControl` 字段推断 sun8iw21 行为。

## 6. Get/Release 是借用关系

以下接口返回的是组件持有的 buffer，不是应用永久拥有的内存：

| Get | 必须配对的 Release |
| --- | --- |
| `AW_MPI_VI_GetFrame` | `AW_MPI_VI_ReleaseFrame` |
| `AW_MPI_VENC_GetStream` | `AW_MPI_VENC_ReleaseStream` |
| `AW_MPI_VDEC_GetImage` | `AW_MPI_VDEC_ReleaseImage` |
| `AW_MPI_VDEC_GetDoubleImage` | `AW_MPI_VDEC_ReleaseDoubleImage` |

通用规则：

1. 只有 Get 返回成功才把资源标为 borrowed；
2. 成功后无论处理成功、超时、停止还是异常，都必须执行对应 Release；
3. Release 后不得继续访问原指针；跨线程或进入异步 sink 前必须复制或转移到有明确
   生命周期的自有 buffer；
4. 队列必须有上限。消费者跟不上时采用明确的丢帧、断开或背压策略，不能无限堆积；
5. 先让 Get 调用可退出并 join worker，再销毁它依赖的 channel。

sun8iw21 常见超时参数约定为：负数阻塞、`0` 立即返回、正数等待相应毫秒数；具体接口
仍以当前头文件和模块文档为准。产品服务推荐有限超时，以便停止请求能够收敛。

## 7. Callback 不是销毁线程

公共 callback 形态：

```c
typedef ERRORTYPE (*MPPCallbackFuncType)(
    void *cookie,
    MPP_CHN_S *channel,
    MPP_EVENT_TYPE event,
    void *eventData);
```

常见事件包括 buffer release、编码超时/满、EOF、首帧渲染和 VI timeout。事件数据的
实际类型由事件和模块共同决定，不能统一强转。

callback 中推荐只做：

- 校验 `channel` 和 `event`；
- 复制最小事件数据；
- 写入有界 mailbox、eventfd/self-pipe 或设置原子标志；
- 立即返回。

不要在 callback 中 `Stop`、`UnBind`、`Destroy`、`pthread_join`、等待磁盘或发送网络；
这些动作可能与 MPP 内部线程互相等待。销毁统一回到控制线程执行。

## 8. MMZ 和缓存一致性

sun8iw21 提供：

| API | 作用 |
| --- | --- |
| `AW_MPI_SYS_MmzAlloc_Cached` | 分配可供媒体硬件使用的 cached 内存，返回物理/虚拟地址 |
| `AW_MPI_SYS_MmzFlushCache` | CPU 写入后、交给硬件读取前刷新缓存 |
| `AW_MPI_SYS_MmzFlushCache_check` | 带检查选项的刷新接口 |
| `AW_MPI_SYS_GetVirMemInfo` | 查询已知虚拟地址对应的物理地址与 cached 属性 |
| `AW_MPI_SYS_MmzFree` | 释放与分配调用配对的物理/虚拟地址 |

不要把普通 `malloc` 地址伪装成媒体物理地址。MMZ 地址类型在不同 SoC 头文件中可能从
32 位变化到更宽类型，复制其他平台示例前必须核对函数签名。CPU/硬件双向访问时还要
核对当前平台是否提供 invalidate 语义；sun8iw21 公共头文件只暴露上述 flush 接口，
不能直接照搬 sun252iw1 文档中的 `AW_MPI_SYS_MmzInvalidCache`。

## 9. 可等待 handle

`AW_MPI_SYS_HANDLE_ZERO`、`AW_MPI_SYS_HANDLE_SET`、`AW_MPI_SYS_HANDLE_ISSET` 和
`AW_MPI_SYS_HANDLE_Select` 为 MPP handle 提供类似 `fd_set/select` 的等待接口；部分
组件还能通过 `GetHandle` 取得可等待句柄。使用时仍要提供独立的停止唤醒来源，不能把
无限 `Select` 当作线程退出机制。

## 10. 推荐资源账本

每个封装对象至少维护以下状态：

```text
sysInitialized
created endpoints[]
enabled devices/channels[]
started channels[]
registered callbacks[]
bindings[(src,dst)]
borrowed buffers/streams
running workers[]
```

正常停止和失败回滚调用同一个 cleanup：

```text
拒绝新工作
-> 唤醒并 join worker
-> Stop/Disable 数据流
-> 归还 borrowed buffer
-> 反向 UnBind
-> 逆序 Destroy/Disable
-> SYS Exit
```

不要仅根据错误码文字决定是否清理；根据“哪个成功步骤已经写入账本”清理，才能使
`stop()` 幂等。

## 11. 常见错误定位

| 现象 | 优先检查 |
| --- | --- |
| `NOTREADY` / 非法状态 | SYS 是否成功；Create/Start 顺序；是否在 Executing 中设置静态属性 |
| `EXIST` | channel/layer ID 冲突；前次失败是否未销毁 |
| `BUSY` | 仍在绑定、运行或存在未归还 buffer |
| `NOBUF` / buffer full | Get 后未 Release；消费者太慢；队列或 VBV 配置不足 |
| Start 成功但无数据 | source 是否 Enable；格式/尺寸是否一致；绑定端点是否填错 |
| stop 卡住 | worker 是否无限阻塞；callback 是否执行了阻塞控制；是否先销毁后 join |

具体错误码值不要复制到业务代码；使用当前 `mm_comm_*.h` 的符号，并在日志中记录原始
十六进制返回值、模块、device/channel 和状态。
