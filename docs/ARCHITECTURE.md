# 控制器架构

## 范围和入口

`src/main.cpp` 创建 `ControllerModel` 和 `ControllerUi`。工程只覆盖原始 `vape_v4.exe` 的控制器职责，DLL 和 Java 层位于明确的边界之外。

原程序使用固定 `824x484` 逻辑画布。重构程序在 Win32 窗口中按 DPI 缩放，并通过 GDI+ 双缓冲绘制页面。

## 模块

| 模块 | 职责 |
| --- | --- |
| `controller_ui` | 消息循环、命中测试、60 FPS 重绘、页面和动画 |
| `controller_model` | 页面状态、App Auth、Minecraft 发现、加载阶段和缓存状态 |
| `local_controller_service` | DLL 与控制器之间的 loopback 命令通道 |
| `injection_coordinator` | EXE 侧反射式 DLL 注入协调 |

## 原始状态映射

原程序主分派函数为 `FUN_14003E900 @ 0x14003E900`，状态读取函数为 `FUN_14009A680 @ 0x14009A680`。

| 原始状态 | 页面 |
| ---: | --- |
| `-98` | 账号登录或浏览器认证 |
| `-97`、`-96` | Minecraft 发现和选择 |
| `-99` | 启动器过期 |
| 其他 | 分阶段加载、缓存询问或完成 |

浏览器认证标志位于 `0x140421B56`，页面状态位于 `0x1404221D8`。

## 帧更新

窗口使用 16 ms 定时器驱动帧更新。每帧执行：

1. 更新模型中的 Minecraft 两秒轮询或加载阶段；
2. 检测页面变化并启动蒙版过渡；
3. 插值 Logo 位置和加载进度；
4. 更新 Browser Auth 四格指示器；
5. 请求重绘。

Browser Auth 指示器依据 `FUN_14003F920 @ 0x14003F920` 每 20 ms 更新四个 alpha。加载进度依据 `FUN_14003D180 @ 0x14003D180` 向 `max(stage / 116, 0.05)` 逼近。

## App Auth

浏览器认证流程由非 VM 代码恢复：

1. 请求 `/api/v1/app-auth/generate`；
2. 打开 `/app-auth/proceed/<challenge>`；
3. 每两秒轮询 `/api/v1/app-auth/status`；
4. 成功后保存访问令牌并进入 Minecraft 选择。

账号密码登录最终进入 `FUN_140985D2D` 的虚拟化代码。当前内存 dump 未生成可直接反编译的业务代码区，因此此路径保持明确的 VM 边界。

## 注入边界

`InjectionCoordinator` 只负责 EXE 侧协调：定位相邻官方 DLL、在目标进程内加载并传入本地控制服务端口。DLL 的实现、导出加载器内部逻辑和 Java 客户端不属于本仓库。
