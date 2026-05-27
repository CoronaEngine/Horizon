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
- FetchContent 依赖采用共享源码缓存和 preset 本地构建目录。

## Preset 和目录

- Preset 定义位于 `CMakePresets.json`。
- Windows 默认推荐 `ninja-msvc` / `msvc-debug`；另有 `ninja-clang` 和 `vs2022`。
- Linux preset 为 `ninja-linux-gcc`、`ninja-linux-clang`；macOS preset 为 `ninja-macos`。
- 不确定本机可用项时运行 `cmake --list-presets`。
- 常见构建目录为 `build/ninja-msvc`、`build/ninja-clang`、`build/vs2022`、`build/ninja-linux-gcc`、`build/ninja-linux-clang`、`build/ninja-macos`。
- `HORIZON_BUILD_EXAMPLES` 控制 `examples/` 和示例依赖；关闭后不会生成 `HorizonExamples`。
- `modules/ocarina` 只在需要 CUDA 且 `CUDA_PATH` 已设置时参与。

## FetchContent 规则

- 共享源码缓存：`build/_deps/*-src`。
- 共享下载/更新临时目录：`build/_deps/*-tmp`。
- Preset 本地第三方构建目录：`build/<preset>/deps/*-build`。
- 新增 FetchContent 依赖时优先使用 `horizon_fetchcontent_declare(...)`。
- 不要直接使用裸 `FetchContent_Declare(...)`，除非依赖确实需要自定义 `SOURCE_DIR` 或 `BINARY_DIR`。
- 如果出现 `build/<preset>/_deps`，优先检查旧 cache 或是否有新依赖绕过 `horizon_fetchcontent_declare(...)`。
- 日常清理优先删除具体 `build/<preset>`；只有需要重新拉取第三方源码时才删除 `build/_deps`。

## Visual Studio / MSVC

- Visual Studio 调试时打开仓库根目录，不要只打开单个 `.cpp`。
- VS 2026 日常使用 `ninja-msvc` / `msvc-debug`，配置下拉框常见显示名为 `Debug (ninja-msvc-msvc-debug)`。
- 调试目标通常选 `HorizonExamples`，主入口为 `examples/main.cpp`。
- 构建产物通常位于 `build/ninja-msvc/examples/Debug/HorizonExamples.exe`。
- 如果 VS 没有显示 `HorizonExamples`，先确认 configure 成功且没有关闭 `HORIZON_BUILD_EXAMPLES`。
- 命令行复现 MSVC 构建时，优先进入 Visual Studio Developer Command Prompt；普通 PowerShell 可能缺少 MSVC include/lib 环境，表现为找不到 `<cstdint>` 等标准头。

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target Horizon"
```

按任务替换 target；库验证优先用 `Horizon`，复现示例调试时用 `HorizonExamples`。

## 备注

- 当前推荐流程偏 Windows / MSVC / Ninja。
- 纯文档改动通常不需要 CMake 构建。
