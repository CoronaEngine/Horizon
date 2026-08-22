# 跨平台基础设施测试设计

## 状态

本设计已于 2026-08-22 获得确认，可用于编写和执行实施计划。

## 范围

本设计覆盖 `src/core`、`src/math`、`src/ast`、`src/dsl` 四个基础设施 target，以及 `tests/` 下注册的测试。它不表示 Helicon、Vulkan 运行时、Ocarina、示例、工具或完整的 `Horizon` 引擎 target 已经支持 Linux 或 macOS。

## 目标

在保持明确单向依赖的前提下构建四个基础设施 target，并在 Windows、Linux、macOS 上运行其注册的 host 端测试，同时不安装、不配置任何仅供引擎使用的依赖。

## 依赖关系

基础设施依赖关系保持为：

```text
horizon-dsl -> horizon-ast -> horizon-math -> horizon-core
```

上层可以直接依赖任意更底层模块；禁止增加反向依赖和循环依赖。

引擎依赖关系位于基础设施之上，并且可以关闭：

```text
Horizon / Helicon / examples / tools
                 |
                 v
       infrastructure targets
```

## 构建边界

新增 CMake 选项 `HORIZON_BUILD_ENGINE`，默认值为 `ON`，以保持现有开发行为。四个基础设施子目录始终参与配置。只有在 `HORIZON_BUILD_ENGINE=ON` 时，才配置 Helicon、顶层 `Horizon` target、着色器集成、引擎运行时依赖处理和引擎专用编译选项。

Conan 增加对应的布尔选项 `with_engine`，默认值同样为 `True`。`core` 目标族传入 `&:with_engine=False` 和 `&:with_tests=True`。新增 `engine` 目标族，用于直接构建顶层 `Horizon` target，并传入 `&:with_engine=True`；这样在 `core` 被明确为基础设施目标族后，不会破坏现有 `Horizon` target 的映射。依赖引擎的其他目标族继续保持 `with_engine=True`。

基础设施依赖仅保留四个模块实际使用的包：

- `quill`
- `fmt`
- `spdlog`
- `xxhash`

以下依赖只属于引擎：

- `ktm`
- `pfr`
- `spirv-tools`
- `volk`
- `vulkan-headers`
- `vulkan-memory-allocator`
- `slang`
- `tracy`

`with_examples`、`with_tools`、`with_ocarina` 和 `with_vision_hotfix` 要求 `with_engine=True`。无效组合必须在 Conan 校验或 CMake 配置阶段给出直接、可定位的错误。

## 平台运行时

`horizon-core` 必须在每个 CI 操作系统上提供相同的平台 API，不能仅在非 Windows 系统上省略实现。

当前 Windows 实现从 `runtime/platform.cpp` 移至 `runtime/platform_windows.cpp`。新增 `runtime/platform_posix.cpp`，为 Linux 和 macOS 提供：

- 使用 `dlopen`、`dlclose`、`dlsym` 操作动态模块；
- Linux 使用 `.so`，macOS 使用 `.dylib`；
- 使用 `backtrace`、`dladdr`、`abi::__cxa_demangle` 生成调用栈和符号；
- `horizon-core` 在需要时私有链接 `${CMAKE_DL_LIBS}`。

公共头 `core/header.h` 不得包含 `windows.h`。DLL 标注宏只能在 MSVC 的动态链接模式下使用 `__declspec`，在其他编译器下应展开为可移植的可见性标注。当前四个模块是静态库，通过公共的 `OC_STATIC_LINK` 定义让 API 宏展开为空，避免静态库消费者错误地产生 `dllimport` 引用。

遇到不支持的操作系统时，CMake 必须明确配置失败，不能静默产出缺少实现的库。

## 平台回归测试

新增一个导出 `horizon_test_value()` 的小型共享测试模块。host 测试通过 Core 公共 API 加载该模块的精确路径，解析并调用导出符号，销毁模块句柄，检查当前平台的动态库命名约定，并确认 traceback 接口可调用。

该测试验证公共行为，不检查实现文本，并通过 CTest 在三个操作系统上执行。

## 跨平台开发流程

`tools/dev.py` 继续作为唯一 CI 入口。

- Windows 继续使用仓库中按配置维护的 MSVC profile。
- Linux 和 macOS 使用 `conan profile detect --force` 创建具名 Conan profile，并覆盖所请求配置的 build type 和 C++ 标准。
- Windows 从 `.bat` 文件加载 Conan 构建环境；POSIX 系统通过 `/bin/sh` source `.sh` 文件，再由当前 Python 进程以 JSON 传回完整环境，避免依赖 GNU `env -0`。
- CMake 继续使用现有按目标族和配置隔离的构建目录与 preset。

工作流实现继续由 `tools/test_workflow.py` 独立进行单元测试。

## CI 契约

`.github/workflows/core-tests.yml` 使用 `fail-fast: false` 的矩阵，包含：

- `windows-latest`，并初始化 MSVC；
- `ubuntu-latest`；
- `macos-latest`。

每个矩阵任务只执行以下工程操作：

1. 以 Debug 配置和 `core` 目标族构建 `horizon-tests`。
2. 使用 Python generator 检查支持 32 个元素的 `src/core/tuple.h`。
3. 运行 `build/conan/core/debug` 中注册的全部 CTest。

工作流设置 `HORIZON_CONAN_EXPORT_LOCAL_RECIPES=false`，因为基础设施依赖图不使用仓库内的引擎配方。工作流不得构建 `Horizon`、Helicon、示例、工具、Ocarina 或 CUDA target。

## 验收标准

满足以下条件时，实施才算完成：

1. Python 工作流单元测试通过。
2. Tuple 生成检查通过。
3. Windows 上执行一次全新的 `horizon-tests` 配置与构建并通过。
4. Windows 上全部本地注册 CTest 通过。
5. GitHub Actions 的 Windows、Ubuntu、macOS 三个任务全部通过。
6. `git diff --check` 不报告空白字符错误。
7. 架构文档明确区分“基础设施可移植”与“完整引擎可移植”。
