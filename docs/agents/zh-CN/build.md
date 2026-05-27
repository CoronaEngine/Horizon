# Horizon 构建上下文

仅在处理 CMake、preset、构建、CI 或验证命令时加载。

## 目标

- `Horizon`：主静态库。
- `ShaderCompileScripts`：`tools/` 下的 shader/codegen 工具目标。
- `HorizonExamples`：示例目标，由 `HORIZON_BUILD_EXAMPLES` 控制。

## 常用命令

优先使用统一开发入口：

```powershell
.\tools\dev.ps1 status
.\tools\dev.ps1 configure
.\tools\dev.ps1 build Horizon
.\tools\dev.ps1 build ShaderCompileScripts
.\tools\dev.ps1 build HorizonExamples
```

`tools/dev.ps1` 只封装已有命令。调试 CMake 或 CI 问题时，可以直接运行底层命令：

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target HorizonExamples
```

## 规则

- 根 `CMakeLists.txt` 负责项目选项、依赖和子目录。
- `src/CMakeLists.txt` 定义 `Horizon`。
- `tools/CMakeLists.txt` 定义 `ShaderCompileScripts`。
- `examples/CMakeLists.txt` 定义 `HorizonExamples`。
- `include/` 是公共 include surface。
- 不要把 Vulkan、VMA、Windows 或仅实现层需要的类型暴露到公共头，除非公共 API 确实需要。
- 修改 CMake 后，运行 configure 和最小相关 build。
- 新人和 Agent 的常用入口是 `tools/dev.ps1`；只有需要排查底层问题时才直接用 `cmake`。
- 每个 configure preset 使用独立的 `build/<preset>` 目录，不要假设所有生成器共享 `build/`。
- FetchContent 依赖采用共享源码缓存和 preset 本地构建目录；细节见 `docs/cmake-usage.md`。

## 深度参考

只在任务需要时加载这些长文档：

- CMake 结构、preset、构建目录、可选模块、常见问题：`docs/cmake-usage.md`
- Visual Studio / VS CMake 调试、可执行文件路径、命令行复现：`docs/vs-debugging.md`

## 备注

- 当前推荐流程偏 Windows / MSVC / Ninja。
- 纯文档改动通常不需要 CMake 构建。
