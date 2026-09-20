# 运行时模型

- 文档角色：定义跨进程监督、顶层模式、状态转换和故障收敛
- 决策依据：ADR-0001、ADR-0002、ADR-0003

本页只定义系统级状态关系。MPP 模块的 Create/Bind/Start/Stop/Destroy 顺序由
[`camera-mpp-service/pipelines.md`](../components/camera-mpp-service/pipelines.md) 维护。

## GUI 与后端监督

`camera-gui` 外层会话监督器不创建 `QApplication`，负责在 Qt/MPP 会话和 YOLO 会话间
切换；Qt UI 子进程负责启动、观察和停止 MPP。业务可用性由 IPC handshake、response
和 event 确认，不能根据 PID 存在、日志文本或按钮点击推断成功。

```text
Stopped --start--> Starting --handshake--> Ready/Running
   ^                   |                         |
   |                   `--failure------------> Error
   |                                             |
   `---------------- stop/exit <--- Stopping <---+
```

监督层必须处理启动失败、异常退出、停止超时和 stale owner；具体 IPC 字段及错误码只以
[`../interfaces/ipc-v1.md`](../interfaces/ipc-v1.md) 为准。

Qt 页面状态与后端模式状态正交：Home/Camera/Album/UVC 是 UI navigation，后端仍只有
None/Camera/Playback/UVC。非录像态返回 Home 不改变后端；首页显示当前 mode 摘要。
Recording 是明确的 UI 锁定例外：后端确认后保持 Camera 页面且只允许停止录像，状态
离开 Recording 后才恢复导航。进入 Camera、选择 Album 视频或启用 UVC 才请求相应模式，
首页 OpenCV 则进入下述进程会话交接。

## MPP Service 顶层模式

MPP Service 任一时刻最多有一个活动顶层 Pipeline：

```text
                    +--> Camera ---+
Idle --enter mode --+--> Playback -+-- complete stop --> Idle
                    +--> UVC ------+
```

- Camera、Playback 与 UVC 严格互斥。
- Snapshot、Record 和 RTSP 是 Camera 会话中的按需操作或输出，不是新的顶层模式。
- 顶层模式切换必须先让旧 Pipeline 完整回到 Idle，再创建新 Pipeline。
- UVC 顶层模式先打开预配置的 gadget video 节点；Host 未 STREAMON 时不创建 ISP/VI/
  VENC，也不创建 VO。

UVC 的 Host 驱动子状态为：

```text
WaitingHost --CONNECT--> Connected --PROBE/COMMIT--> Committed
     ^                         |                         |
     |                         |                  STREAMON/bulk COMMIT
     |                         |                         v
     +---- DISCONNECT ---------+<-- STREAMOFF ----- Streaming

任意活动子状态 --ioctl/capture fault--> UvcError --leave/re-enter--> WaitingHost
```

UVC gadget 事件线程是 UVC Pipeline 内部子状态的串行控制者；数据线程只负责
Get→bounded copy/convert→Release。顶层切换先请求事件线程退出并 join，再由
`MppService` 完成最终幂等 cleanup，因此不会与 Camera/Playback 创建路径并发修改 MPP。

组件内部状态、线程和回滚由
[`camera-mpp-service`](../components/camera-mpp-service/) 负责，不在系统架构中复制。

## Playback 状态语义

Playback 的系统可见状态保持以下关系：

```text
Idle --load--> Loading --loaded--> Ready --play--> Playing <--> Paused
                  |                              |
                  `--failure--> Error            `--EOF--> Ended

Ready/Playing/Paused/Ended/Error --stop--> Stopping --> Idle
```

EOF 和异步错误必须经正常控制路径收敛；任一活动或失败状态都应能通过幂等 stop 回到
Idle。容器和 codec 是否可用由验证证据决定，不由状态机本身承诺。

## MPP 与 YOLO 模式切换

MPP 与 YOLO 不是同一服务内的两个 Pipeline，而是连同显示 writer 一起互斥的会话：

```text
请求切换
-> 当前后端拒绝新业务
-> 停止工作并释放设备/显示资源
-> MPP 报告 Stopped/退出
-> 销毁 Qt UI 子进程和 QApplication，释放 linuxfb
-> 确认 backend owner 已释放
-> YOLO 取得同一 backend lock
-> YOLO 独占 camera 和 framebuffer
-> YOLO 退出并释放资源/锁
-> 外层监督器重建 Qt UI，再启动 MPP 并完成 handshake
-> Qt 显示 Home；不自动创建 Camera Pipeline
```

任何一步失败都不得同时保留两个 camera owner 或 framebuffer writer。YOLO 启动失败或
异常退出后外层监督器直接回到 Qt 会话；MPP shutdown 超时由 Qt 监督器终止其子进程，
kernel lock 在进程退出时释放。

## 故障收敛不变量

- callback、signal handler 和媒体数据 worker 只通知正常控制路径，不直接销毁整个
  后端；UVC gadget 事件线程是明确的 Pipeline-local 控制路径，不属于数据 worker。
- 停止操作可重复执行，并最终到达相同的无 owner 状态。
- 创建中途失败必须只释放已经成功取得的资源，且按依赖关系逆序回滚。
- 后端退出前必须停止新请求、唤醒并 join 工作线程、归还 frame/stream，然后释放锁。
- GUI 收到 error 不代表资源已经释放；只有 Stopped/进程退出与 owner 检查完成后才能
  启动另一个后端。

资源级规则见 [`resource-ownership.md`](resource-ownership.md)，实现级生命周期见
[`camera-mpp-service/pipelines.md`](../components/camera-mpp-service/pipelines.md)。
