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

构建过程会将 `assets` 目录复制到可执行文件旁边。

构建后可以运行仓库提供的校验脚本，检查 PE 文件和全部运行资源：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify-build.ps1
```

GitHub Actions 会在 Windows x64 环境中执行相同的 Release 构建和校验，并保留短期构建产物。

## 许可证

本仓库采用 [CC0 1.0 Universal](LICENSE) 许可。
