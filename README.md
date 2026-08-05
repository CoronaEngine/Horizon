# Horizon

## 构建指南

本项目使用 **CMake Presets + Conan** 管理构建和第三方依赖。第一次构建会下载并编译依赖，耗时较长是正常现象；之后的增量构建会快很多。

下面的默认目标是 `HorizonExamples`，它不需要 CUDA。只有构建可选的 Ocarina 模块时才需要安装 CUDA。

### 1. 工具环境准备

在 Windows 上准备以下工具，并在新打开的终端中确认命令可用：

| 工具          | 需要的内容                                                   | 检查命令                                                   |
| ------------- | ------------------------------------------------------------ | ---------------------------------------------------------- |
| Git           | 用于克隆和更新源码                                           | `git --version`                                            |
| Visual Studio | 安装“使用 C++ 的桌面开发”工作负载，并勾选 MSVC C++ 生成工具和 Windows 10/11 SDK | 打开对应版本的“x64 Native Tools Command Prompt”，执行 `cl` |
| CMake         | **4.0 或更高版本**，并加入 `PATH`                            | `cmake --version`                                          |
| Ninja         | 并加入 `PATH`                                                | `ninja --version`                                          |
| uv            | Python 工具与 Conan 的管理入口，并加入 `PATH`                | `uv --version`                                             |

不需要单独安装 Python 或 Conan：`uv` 会根据仓库中的 `pyproject.toml` 和 `uv.lock` 创建本地环境并安装固定版本的 Conan。

如果上面的检查命令有任何一个找不到，请先完成安装并重新打开终端或 IDE，再继续下面的步骤。

### 2. 构建方式一：使用 IDE

无论使用哪种 IDE，都要**打开仓库根目录**（包含 `CMakeLists.txt` 和 `CMakePresets.json` 的 `Horizon` 文件夹），不要打开 `build` 目录。首次 Configure 会自动准备 Conan 依赖。

1. 选择配置预设：首次建议选择 `relwithdebinfo`；调试代码时选择 `debug`。
2. 执行 Configure，等待它完成。
3. 选择并构建目标 `HorizonExamples`。

#### Visual Studio

使用“打开本地文件夹”打开仓库根目录。等待 CMake 配置完成后，在 CMake Targets 视图中选择 `HorizonExamples` 并生成。Visual Studio 会自动使用 MSVC 环境。

#### CLion

打开仓库根目录后，在 `Settings > Build, Execution, Deployment > Toolchains` 中选择已安装的 **Visual Studio / MSVC** 工具链；在 CMake Profile 中选择预设 `relwithdebinfo`，然后构建 `HorizonExamples`。

#### VS Code（必须完成此设置）

安装 **CMake Tools** 扩展后，按 `Ctrl+Shift+P`，运行 `Preferences: Open User Settings (JSON)`，加入以下设置：

```json
{
  "cmake.useCMakePresets": "always",
  "cmake.useVsDeveloperEnvironment": "always"
}
```

这会让 CMake Tools 在每次配置和构建时都继承 Visual Studio 的 MSVC/Windows SDK 环境，不需要每个项目手动切换。随后执行：

1. `CMake: Select Configure Preset`，选择 `relwithdebinfo`；
2. `CMake: Configure`；
3. `CMake: Build Target`，选择 `HorizonExamples`。

若输出中反复出现 `fatal error C1083`，并提示找不到 `<cstdint>`、`<algorithm>` 等 C++ 标准库头文件，通常不是源码错误，而是 VS Code 的构建进程没有拿到 MSVC 环境。确认上述设置为 `always` 后，运行 `CMake: Delete Cache and Reconfigure`，再重新构建。

### 3. 构建方式二：使用脚本（推荐用于命令行）

在仓库根目录执行。脚本会自动执行 Conan 安装、CMake 配置和构建，并在构建前加载 MSVC 环境：

```powershell
uv run --frozen python tools/dev.py build
```

常用命令：

```powershell
# 构建 Debug 版本
uv run --frozen python tools/dev.py build --configuration debug

# 已经成功配置过后，只做快速增量构建
uv run --frozen python tools/dev.py build-fast

# 删除当前配置的构建目录后，重新完整构建
uv run --frozen python tools/dev.py rebuild
```

构建文件位于 `build/conan/<配置名>/`，例如默认配置使用 `build/conan/relwithdebinfo/`。脚本方式与 IDE 使用同一套 CMake Preset 和 Conan 依赖，不需要手动运行 `conan install` 或手动执行 Visual Studio 的环境脚本。

## tracy用法

1、cmake打开

option(HORIZON_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)
->
option(HORIZON_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)

2、编译启动 example

3、打开tracy-profiler.exe 点Connect

4、如果是一帧里调用多的看 火焰图
![tracy flamegraph](Image/tracy-flamegraph.png)

## Shader 资源使用与设计

> 注：这是当前最终的设计方案，仍在持续调整中。

Horizon 的着色器资源绑定遵循一套统一约定，核心原则是**全面走 bindless，不再保留非 bindless 路径**。所有资源按数据的生命周期与共享粒度划分到不同的下发通道。

### 数据下发通道

- **Per-object 数据走 Push Constant。**
  每个物体独有、且随 draw 变化的数据（如 model 矩阵、bindless 纹理索引等）直接放进 Push Constant，随 draw call 下发，避免频繁的 descriptor 更新。

- **Push Constant 塞不下时开 SSBO。**
  当 per-object 数据超出 Push Constant 的大小限制时，将数据整体放入一个 SSBO，Push Constant 里只保留一个 **id / 索引**，shader 通过该 id 到 SSBO 中取出对应物体的数据。

- **Per-pass 共享数据走 UBO + Push Descriptor。**
  每个 pass 内部共享、不随单个物体变化的数据（如 view / projection 矩阵、光照参数等）放在 UBO 中，通过 **Push Descriptor** 下发，减少常规 descriptor set 的分配与绑定开销。

- **纹理 / buffer / image 全部 bindless。**
  所有纹理、storage buffer、storage image 均通过 bindless 表访问，shader 侧仅持有索引。已彻底移除非 bindless 的绑定路径。

### Descriptor Set 约定

Set 0–2 为 Horizon 保留的 bindless 集，普通 UBO 一律放在 set 3：

| Set   | 用途                                         |
| ----- | -------------------------------------------- |
| set 0 | bindless 纹理表（combined image sampler）    |
| set 1 | bindless storage buffer                      |
| set 2 | bindless storage image                       |
| set 3 | per-pass 共享 UBO（经 Push Descriptor 下发） |

同一个 Push Constant 块在 vertex / fragment 等各阶段之间共享，布局必须严格一致。