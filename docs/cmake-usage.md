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
- 通过 FetchContent 拉取第三方依赖
- 引入核心源码、工具和示例子目录
- 在检测到 CUDA_PATH 时附加构建 modules/ocarina

当前已经将根级逻辑拆分到这些模块文件中：

- [cmake/HorizonCoreDependencies.cmake](../cmake/HorizonCoreDependencies.cmake)：核心依赖和第三方库拉取
- [cmake/HorizonRuntimeDeps.cmake](../cmake/HorizonRuntimeDeps.cmake)：运行时依赖复制函数
- [cmake/HorizonExampleDependencies.cmake](../cmake/HorizonExampleDependencies.cmake)：示例依赖
- [cmake/HorizonOcarina.cmake](../cmake/HorizonOcarina.cmake)：可选的 ocarina 模块接入
- [cmake/HeliconShaderCompile.cmake](../cmake/HeliconShaderCompile.cmake)：shader 自动编译支持

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

所有 configure preset 都继承自 base，并共享以下目录：

- binaryDir = ${sourceDir}/build
- installDir = ${sourceDir}/install

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

当前 preset 统一将构建目录放在 build 下，因此常见产物路径通常在这些位置：

- build/bin/Debug
- build/bin/Release
- build/bin/RelWithDebInfo
- build/lib
- build/compile_commands.json

项目中部分目标还会在构建后自动复制 Helicon 运行时依赖到目标输出目录。

## 9. 多生成器切换的注意事项

这个项目最需要注意的一点，是所有 configure preset 共享同一个 build 目录。

这意味着：

- 可以在同一生成器下切换不同 configuration
- 不建议直接在 ninja-msvc 和 vs2022 之间复用同一个已存在的 build 目录
- 生成器切换前应清理 build，避免缓存和生成器状态冲突

Windows 下清理示例：

```powershell
Remove-Item -Recurse -Force .\build
```

之后再重新 configure：

```powershell
cmake --preset ninja-msvc
```

## 10. 常见问题

### 第一次 configure 很慢

原因通常是 FetchContent 正在下载第三方依赖，这是预期行为。

### 找不到 Python

根级 CMake 会执行 find_package(Python)。如果未安装 Python，会给出 warning。基础编译通常仍可继续，但依赖 Python 的工作流可能不可用。

### 没有 HorizonExamples 目标

通常是以下两种情况之一：

- configure 时关闭了 HORIZON_BUILD_EXAMPLES
- 当前 configure 失败，导致 examples 子目录没有正确生成目标

### 切换生成器后 configure 失败

优先删除 build 目录，再重新执行对应的 configure preset。

## 11. 建议工作流

如果你只是日常开发，推荐使用下面这套最稳定的流程：

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target HorizonExamples
```

如果你准备在不同生成器之间切换，先清理 build，再重新 configure，不要直接复用旧缓存。