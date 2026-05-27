# Horizon CMake 使用说明

本文档说明 Horizon 项目的 CMake 组织方式、推荐构建流程、可用 preset，以及一些容易踩到的约束。

## 1. 构建前提

- CMake 4.0 及以上
- 支持 C++20 的编译器
- Windows 下推荐 Visual Studio 2022 或 LLVM/Clang + Ninja
- Vulkan SDK / Vulkan Runtime 环境
- Python：可选，用于部分脚本能力；未安装时会出现警告，但不一定阻塞基础构建
- CUDA：仅在需要启用 modules/ocarina 时需要，并且需要设置环境变量 CUDA_PATH

## 2. 项目中的 CMake 结构

根目录 [CMakeLists.txt](CMakeLists.txt) 负责以下事情：

- 定义项目与全局编译选项
- 接入内嵌基础模块，并拉取其余第三方依赖
- 引入核心源码、工具和示例子目录
- 在检测到 CUDA_PATH 时附加构建 modules/ocarina

当前已经将根级逻辑拆分到这些模块文件中：

- [cmake/HorizonCoreDependencies.cmake](../cmake/HorizonCoreDependencies.cmake)：核心依赖和第三方库拉取
- [cmake/HorizonRuntimeDeps.cmake](../cmake/HorizonRuntimeDeps.cmake)：运行时依赖复制函数
- [cmake/HorizonCorona.cmake](../cmake/HorizonCorona.cmake)：内嵌 `modules/corona` 的接入桥接
- [cmake/HorizonExampleDependencies.cmake](../cmake/HorizonExampleDependencies.cmake)：示例依赖
- [cmake/HorizonOcarina.cmake](../cmake/HorizonOcarina.cmake)：可选的 ocarina 模块接入
- [cmake/HeliconShaderCompile.cmake](../cmake/HeliconShaderCompile.cmake)：shader 自动编译支持

当前约束：

- `modules/corona` 已作为仓库内一等源码接入，不再由 Horizon 在 configure 时远程下载
- Corona 的 examples/tests 由 [cmake/HorizonCorona.cmake](../cmake/HorizonCorona.cmake) 在顶层集成时显式关闭
- 其余第三方依赖仍可能通过 FetchContent 在首次 configure 时获取

## 3. 主要构建目标

项目当前常见目标如下：

- Horizon：主静态库
- ShaderCompileScripts：工具程序
- HorizonExamples：示例程序，仅在 HORIZON_BUILD_EXAMPLES=ON 时可用

其中：

- [src/CMakeLists.txt](../src/CMakeLists.txt) 定义 Horizon
- [tools/CMakeLists.txt](../tools/CMakeLists.txt) 定义 ShaderCompileScripts
- [examples/CMakeLists.txt](../examples/CMakeLists.txt) 定义 HorizonExamples

## 4. Configure Presets

Preset 定义位于 [CMakePresets.json](../CMakePresets.json)。

### Windows

- ninja-msvc：Ninja Multi-Config + MSVC，默认推荐
- ninja-clang：Ninja Multi-Config + clang/clang++
- vs2022：Visual Studio 2022 生成器

### Linux

- ninja-linux-gcc：Ninja Multi-Config + GCC
- ninja-linux-clang：Ninja Multi-Config + Clang

### macOS

- ninja-macos：Ninja Multi-Config

所有 configure preset 都继承自 base，但每个生成器使用独立构建目录，避免 Ninja 和 Visual Studio 之间复用同一个 CMake cache：

- ninja-msvc：`${sourceDir}/build/ninja-msvc`
- ninja-clang：`${sourceDir}/build/ninja-clang`
- vs2022：`${sourceDir}/build/vs2022`
- ninja-linux-gcc：`${sourceDir}/build/ninja-linux-gcc`
- ninja-linux-clang：`${sourceDir}/build/ninja-linux-clang`
- ninja-macos：`${sourceDir}/build/ninja-macos`

所有 preset 仍共享：

- installDir = `${sourceDir}/install`
- FetchContent 源码缓存 = `${sourceDir}/build/_deps`

另外默认开启：

- CMAKE_EXPORT_COMPILE_COMMANDS=ON

## 5. Build Presets 命名规则

build preset 基本遵循以下模式：

- msvc-debug / msvc-release / msvc-relwithdebinfo / msvc-minsizerel
- clang-debug / clang-release / clang-relwithdebinfo / clang-minsizerel
- vs2022-debug / vs2022-release / vs2022-relwithdebinfo / vs2022-minsizerel
- linux-gcc-debug / linux-gcc-release / linux-gcc-relwithdebinfo / linux-gcc-minsizerel
- linux-clang-debug / linux-clang-release / linux-clang-relwithdebinfo / linux-clang-minsizerel
- macos-debug / macos-release / macos-relwithdebinfo / macos-minsizerel

可先用下面命令查看本地可用 preset：

```powershell
cmake --list-presets
```

## 6. 推荐用法

### Windows: Ninja + MSVC

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target HorizonExamples
```

Visual Studio 2026 日常调试也推荐使用这一组 preset。VS 配置下拉框中常见显示名是 `Debug (ninja-msvc-msvc-debug)`，详细流程见 [vs-debugging.md](vs-debugging.md)。

如果你只想构建库和工具：

```powershell
cmake --preset ninja-msvc -D HORIZON_BUILD_EXAMPLES=OFF
cmake --build --preset msvc-debug --target Horizon ShaderCompileScripts
```

### Windows: Visual Studio 2022

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug --target HorizonExamples
```

### Windows: Clang

```powershell
cmake --preset ninja-clang
cmake --build --preset clang-debug --target HorizonExamples
```

### Linux

```bash
cmake --preset ninja-linux-gcc
cmake --build --preset linux-gcc-debug --target HorizonExamples
```

或：

```bash
cmake --preset ninja-linux-clang
cmake --build --preset linux-clang-debug --target HorizonExamples
```

### macOS

```bash
cmake --preset ninja-macos
cmake --build --preset macos-debug --target HorizonExamples
```

## 7. 示例开关与可选模块

### HORIZON_BUILD_EXAMPLES

根级选项如下：

```cmake
option(HORIZON_BUILD_EXAMPLES "Build Horizon examples (and ocarina tests when ocarina is enabled)" ${PROJECT_IS_TOP_LEVEL})
```

行为说明：

- 作为顶层项目构建时，默认开启
- 关闭后不会进入 examples 子目录，也不会拉取示例依赖 stb 和 glfw

示例：

```powershell
cmake --preset ninja-msvc -D HORIZON_BUILD_EXAMPLES=OFF
```

### ocarina 模块

项目不会默认构建 modules/ocarina。只有在环境变量 CUDA_PATH 已定义时，才会执行 [cmake/HorizonOcarina.cmake](../cmake/HorizonOcarina.cmake) 中的逻辑。

如果需要该模块，先确认环境变量存在：

```powershell
$env:CUDA_PATH
```

如果命令结果为空，CMake 会跳过 ocarina 的 add_subdirectory。

## 8. 目录与产物

当前 preset 都放在 `build/<preset>` 下，因此常见产物路径通常在这些位置：

- build/ninja-msvc/bin/Debug
- build/ninja-msvc/bin/Release
- build/ninja-msvc/bin/RelWithDebInfo
- build/ninja-msvc/lib
- build/ninja-msvc/compile_commands.json

其他 preset 的产物位于对应目录，例如 `build/vs2022`、`build/ninja-clang`、`build/ninja-linux-gcc`。

FetchContent 依赖的源码克隆统一放在 `build/_deps/*-src`，供多个 preset 复用。每个 preset 仍会在自己的构建目录下生成独立的第三方构建产物，例如 `build/ninja-msvc/deps/*-build`，避免不同编译器或生成器共用同一份第三方 CMake cache。

项目中部分目标还会在构建后自动复制 Helicon 运行时依赖到目标输出目录。

## 9. FetchContent 依赖缓存设计

Horizon 的第三方依赖需要同时解决两个问题：

- 多个 configure preset 不应重复从 GitHub clone 同一份源码。
- 不同编译器或生成器不应共用同一份第三方构建目录，否则容易混用 CMake cache、编译器探测结果或生成器状态。

因此项目采用“源码共享、构建分离”的布局：

- 共享源码缓存：`build/_deps/*-src`
- 共享下载/更新临时目录：`build/_deps/*-tmp`
- preset 本地构建目录：`build/<preset>/deps/*-build`

例如 `ninja-msvc` 会使用：

- `build/_deps/glslang-src`
- `build/_deps/glslang-tmp`
- `build/ninja-msvc/deps/glslang-build`

如果之后再 configure `ninja-clang` 或 `vs2022`，CMake 会复用 `build/_deps/glslang-src`，但会生成各自独立的 `build/ninja-clang/deps/glslang-build` 或 `build/vs2022/deps/glslang-build`。

实现入口是 [cmake/HorizonFetchContent.cmake](../cmake/HorizonFetchContent.cmake)：

- `HORIZON_FETCHCONTENT_SOURCE_ROOT` 默认指向 `${PROJECT_SOURCE_DIR}/build/_deps`。
- `HORIZON_FETCHCONTENT_BINARY_ROOT` 默认指向 `${PROJECT_BINARY_DIR}/deps`。
- `FETCHCONTENT_BASE_DIR` 被固定到共享源码缓存根目录，使 CMake 自己的 FetchContent 临时目录也集中到 `build/_deps`。
- 项目内依赖声明使用 `horizon_fetchcontent_declare(...)`，它会在未显式指定目录时自动补上共享 `SOURCE_DIR` 和 preset 本地 `BINARY_DIR`。

新增 FetchContent 依赖时，优先使用：

```cmake
horizon_fetchcontent_declare(
    dependency-name
    GIT_REPOSITORY https://example.com/dependency.git
    GIT_TAG main
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(dependency-name)
```

不要直接使用裸 `FetchContent_Declare(...)`，除非该依赖确实需要自定义 `SOURCE_DIR` 或 `BINARY_DIR`。如果第三方子项目也使用 FetchContent，并且需要在 Horizon 顶层构建时接入同一套目录规则，可以像 `modules/corona/cmake/CoronaThirdParty.cmake` 那样先判断 `COMMAND horizon_fetchcontent_declare`，顶层构建时使用 Horizon 包装函数，独立构建该子项目时保留原生 `FetchContent_Declare(...)`。

常见清理方式：

- 只清理某个 preset 的 CMake cache 和构建产物：删除 `build/<preset>`。
- 强制重新构建第三方依赖，但保留已 clone 的源码：删除 `build/<preset>/deps` 后重新 configure/build。
- 强制重新拉取第三方源码：删除 `build/_deps`。

正常日常开发通常只需要清理 `build/<preset>`。删除整个 `build` 会同时移除共享源码缓存，下一次 configure 需要重新 clone 所有 FetchContent 依赖。

## 10. 多生成器切换的注意事项

每个 configure preset 都有自己的 build 子目录。

这意味着：

- 可以在同一生成器下切换不同 configuration
- 可以在 ninja-msvc 和 vs2022 之间切换，而不会复用同一个 CMake cache
- 如果某个 preset 的缓存损坏，只需要清理对应的 `build/<preset>` 目录

Windows 下清理 ninja-msvc 示例：

```powershell
Remove-Item -Recurse -Force .\build\ninja-msvc
```

这只会清理 ninja-msvc 的 CMake cache 和构建产物，不会删除共享的第三方源码克隆。只有在需要强制重新拉取第三方依赖时，才删除 `build/_deps`。

之后再重新 configure：

```powershell
cmake --preset ninja-msvc
```

## 11. 常见问题

### 第一次 configure 很慢

原因通常是 CMake 正在获取剩余第三方依赖，这是预期行为。当前 `modules/corona` 已在仓库内，不属于这类远程拉取。

如果 `build/_deps` 已经存在，后续 configure 通常只会检查或更新已有 checkout，不应在各个 `build/<preset>/_deps` 下重新 clone 同一批源码。若看到新的 preset 私有 `_deps` 目录，优先检查是否有新增依赖绕过了 `horizon_fetchcontent_declare(...)`。

### 找不到 Python

根级 CMake 会执行 find_package(Python)。如果未安装 Python，会给出 warning。基础编译通常仍可继续，但依赖 Python 的工作流可能不可用。

### 没有 HorizonExamples 目标

通常是以下两种情况之一：

- configure 时关闭了 HORIZON_BUILD_EXAMPLES
- 当前 configure 失败，导致 examples 子目录没有正确生成目标

### configure 失败或缓存异常

优先删除对应的 preset 构建目录，再重新执行 configure。例如：

```powershell
Remove-Item -Recurse -Force .\build\ninja-msvc
cmake --preset ninja-msvc
```

### 出现多个 `_deps` 目录

当前预期只有一个共享 `_deps` 目录：

```text
build/_deps
```

如果出现 `build/<preset>/_deps`，通常是以下原因之一：

- 旧 cache 或旧目录残留；确认当前 `CMakeCache.txt` 中的 `FETCHCONTENT_BASE_DIR` 指向 `build/_deps` 后，可以删除这个旧目录。
- 新增依赖直接调用了 `FetchContent_Declare(...)`，没有走 `horizon_fetchcontent_declare(...)`。
- 第三方子项目在 Horizon 顶层构建时绕过了 Horizon 的 FetchContent 目录规则。

排查时可以先看当前 cache：

```powershell
Select-String -Path .\build\ninja-msvc\CMakeCache.txt -Pattern "FETCHCONTENT_BASE_DIR|HORIZON_FETCHCONTENT"
```

再检查源码里的 FetchContent 声明：

```powershell
rg "FetchContent_Declare|horizon_fetchcontent_declare" CMakeLists.txt cmake modules
```

## 12. 建议工作流

如果你只是日常开发，推荐使用下面这套最稳定的流程：

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target HorizonExamples
```

如果你准备在不同生成器之间切换，可以直接执行对应 configure preset。不同 preset 会进入各自的 `build/<preset>` 目录。
