# Corona Framework 快速开始（构建 / 运行 / 测试）

本文面向首次拉取仓库后的上手流程（Windows 为主，其他平台按 preset 类推）。

## 前置要求

- CMake 4.0+（仓库 `CMakeLists.txt` 要求 `cmake_minimum_required(VERSION 4.0)`）
- 支持 C++20 的编译器
  - Windows：MSVC（推荐）或 LLVM Clang

## 配置与构建

### 方式 A：使用 CMake Preset（推荐）

#### MSVC + Ninja Multi-Config

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug
```

#### Clang + Ninja Multi-Config

```powershell
cmake --preset ninja-clang
cmake --build --preset clang-debug
```

#### Visual Studio 2022

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
```

### 方式 B：手动开关 examples/tests

顶层 `CMakeLists.txt` 通过 `CORONA_FRAMEWORK_BUILD_EXAMPLES` / `CORONA_FRAMEWORK_BUILD_TESTS` 控制是否添加 `examples/` 与 `tests/`。

示例：关闭测试，仅构建库与示例

```powershell
cmake --preset ninja-msvc -D CORONA_FRAMEWORK_BUILD_TESTS=OFF
cmake --build --preset msvc-debug
```

## 运行示例（examples）

示例目标名由 `examples/CMakeLists.txt` 决定（与目录同名）：

- `01_event_bus`
- `02_system_lifecycle`
- `03_event_stream`
- `04_vfs`
- `05_multithreading`
- `06_game_loop`
- `07_storage`

在 Ninja/VS 多配置生成器下，可执行文件通常位于：`build/examples/<Config>/`。

例如（Debug）：

```powershell
.\build\examples\Debug\01_event_bus.exe
.\build\examples\Debug\06_game_loop.exe
```

也可直接构建单个示例 target：

```powershell
cmake --build build --config Debug --target 06_game_loop
```

## 运行测试（tests）

### 运行全部测试

```powershell
Set-Location .\build
ctest -C Debug
```

### 运行单个测试二进制

测试可执行文件通常位于：`build/tests/<Config>/`。

```powershell
.\build\tests\Debug\kernel_event_stream_test.exe
```

测试目标命名来源：`tests/CMakeLists.txt` 中的 `corona_add_test(<target> <src...>)`，生成的二进制名即 `<target>`。

## 常见问题

### 1) 运行时缺少 TBB DLL

工程会在构建后自动复制 TBB 运行时到目标输出目录（见 `cmake/CoronaTbbConfig.cmake` 的 `corona_copy_tbb_runtime_artifacts()`）。

如果仍提示缺 DLL：
- 确认你是通过 CMake 构建生成的可执行文件在对应输出目录运行
- 尝试重新构建目标：`cmake --build build --config Debug`

### 2) 第三方依赖下载缓慢或失败

- 日志库 `quill` 通过 `FetchContent` 拉取（见 `cmake/CoronaThirdParty.cmake`）。
- 若网络受限，建议配置 CMake 的代理/镜像策略或预先准备依赖缓存。

### 3) 重新生成构建目录

```powershell
Remove-Item -Recurse -Force .\build
cmake --preset ninja-msvc
cmake --build --preset msvc-debug
```
