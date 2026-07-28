# Vape V4 控制器重建工程

对 Vape V4启动器本体进行的重建工程。

## 已还原内容

当前工程包括：

- 账号登录和浏览器 App Auth 页面状态机；
- Minecraft JVM 进程发现、定时刷新和已注入状态；
- EXE 侧本地控制服务；
- 相邻官方 `vape_v4.dll` 的反射式注入协调流程；
- 分阶段加载、缓存询问、加载完成、启动器过期和错误页面；
- 原始内嵌 PNG 资源和 Proxima Nova 字体；
- Logo 位移、页面蒙版、加载进度、悬停、输入光标和浏览器登录指示器动画；
- 固定逻辑画布和 DPI 缩放。

控制器使用约 60 FPS 的刷新循环。浏览器登录页的四格指示器按照反编译结果每 20 ms 更新一次透明度。

## 构建

在本目录执行：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

构建产物位于：

```text
build/Release/vape_v4_rewrite.exe
```

全部 PNG 和字体资源均以 Windows `RCDATA` 编译进 EXE，运行时不需要外置 `assets` 目录。

构建后可以运行仓库提供的校验脚本，检查 PE 文件和全部内嵌运行资源：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-build.ps1
```

GitHub Actions 会在 Windows x64 环境中执行相同的 Release 构建和校验，并保留短期构建产物。

## Product DLL 双启动差异

当前 Product 集成使用同目录下固定名称的 `Vape421Native.dll`，并明确区分两种启动方式：

- Native Injector 直接注入：没有 Loader token handoff，DLL 的 native `gat()` 返回 `"0"`；
- Loader 在线启动：Loader 按用户名向本地 Service 登录，通过 loopback controller socket 向 DLL 返回长期 token，并接收 `trs` 加载进度；加载完成后显示 Finished Loading 页面。

controller socket 使用 decomp 已确认的 `0x269` token 请求、`0x25c` 进度和 `0x25e` 完成命令。Service 不创建 token `"0"` 的开发者账户，不校验 HWID；用户名不存在时创建账户，存在时复用已有账户和长期 token。

`gat()` 保持 Java 可见方法名 `gat()Ljava/lang/String;` 并由 JNI native 实现，不新增 `native_gat()` 方法，也不修改现有 Java 在线业务。Bootstrap 共享内存只传 controller 端口和 Service 地址，不包含 token。完整协议记录位于工作区上级文档 `../loader_product_token_handoff_design.md`。

## 许可证

本仓库采用 [CC0 1.0 Universal](LICENSE) 许可。
