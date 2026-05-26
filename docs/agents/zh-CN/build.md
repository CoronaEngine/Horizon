# Horizon 构建上下文

仅在处理 CMake、preset、构建、CI 或验证命令时加载。

## 目标

- `Horizon`：主静态库。
- `ShaderCompileScripts`：`tools/` 下的 shader/codegen 工具目标。
- `HorizonExamples`：示例目标，由 `HORIZON_BUILD_EXAMPLES` 控制。

## 常用命令

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

## 备注

- 当前推荐流程偏 Windows / MSVC / Ninja。
- 纯文档改动通常不需要 CMake 构建。
