# 跨进程接口

本目录只保存两个或多个进程共同消费的稳定协议：

- [`ipc-v1.md`](ipc-v1.md)：`camera-gui`、CLI 与 `camera-mpp-service` 的控制协议。

接口文档定义线格式、字段、命令、事件、兼容性和安全边界，不记录某个后端内部的
MPI 调用、线程、文件提交或板测限制。后端实现说明见
[`../components/camera-mpp-service/`](../components/camera-mpp-service/)，
当前实现与未确认事项见 [`../STATUS.md`](../STATUS.md) 和通信双方的组件文档；具体验证
由主维护者在任务中指定。
