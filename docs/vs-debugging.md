# Horizon Visual Studio 调试说明

本文档记录 Windows 下用 Visual Studio 2026 调试 Horizon 的推荐流程。当前日常调试以 CMake Presets 中的 Ninja + MSVC 配置为准。

## 推荐配置

在 Visual Studio 2026 中打开仓库根目录后，使用下列 CMake 配置：

- Configure preset：`ninja-msvc`
- Build preset：`msvc-debug`
- VS 配置下拉框中常见显示名：`Debug (ninja-msvc-msvc-debug)`
- 调试目标：`HorizonExamples`
- 主示例入口：[examples/main.cpp](../examples/main.cpp)
- 生成后的可执行文件通常位于：`build/examples/Debug/HorizonExamples.exe`

这个配置对应的命令行为：

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target HorizonExamples
```

## 在 Visual Studio 2026 中调试

1. 用 Visual Studio 打开 Horizon 仓库根目录，不要打开单个 `.cpp` 文件。
2. 等待 CMake configure 完成。
3. 在配置下拉框中选择 `Debug (ninja-msvc-msvc-debug)`。
4. 在启动项/目标中选择 `HorizonExamples`。
5. 在需要的位置设置断点。
6. 按 `F5` 启动调试，或按 `Ctrl+F5` 直接运行。

如果 Visual Studio 没有显示 `HorizonExamples`，先确认 configure 没有失败，并确认 `HORIZON_BUILD_EXAMPLES` 没有被关闭。

## 给命令行或 Codex 复现 VS 构建使用

不要直接从普通 PowerShell 调用 `cmake --build --preset msvc-debug`。普通 PowerShell 可能没有加载 MSVC 的标准库 include/lib 环境，容易出现类似找不到 `<cstdint>` 的错误。

应先进入 Visual Studio 2026 Developer Command Prompt 环境，再执行构建。例如：

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target HorizonExamples"
```

如果 VS 安装路径不同，先调整 `VsDevCmd.bat` 的路径。进入 VS 开发环境后再构建，才更接近 Visual Studio 内部的调试/构建环境。

## 运行产物

构建成功后，可以直接运行：

```powershell
.\build\examples\Debug\HorizonExamples.exe
```

如果要排查启动卡住的位置，优先在 Visual Studio 中用 `F5` 调试 `HorizonExamples`，再结合 Output / Debug 输出查看 Vulkan、shader 编译和运行时依赖复制相关日志。

