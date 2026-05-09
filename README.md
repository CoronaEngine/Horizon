# Horizon

一个基于 Vulkan 的图形硬件抽象层，提供面向资源与管线的 C++ API，并集成 Helicon Shader DSL（AST + 代码生成 + 编译反射）。

## 当前状态

- 核心 API：`include/Horizon.h`、`include/HardwareCommands.h`
- 后端实现：`src/HardwareWrapperVulkan`
- 着色器工具链：`src/Helicon` + `tools/ShaderCompileScripts`
- 构建系统：CMake + CMake Presets + 内嵌模块/第三方依赖混合管理
- 现状：可开发，但仍处于稳定性迭代期

## 待办事项

- (紧急) shader 中不同的 textureSampler，分离的图像和采样器描述符
- 多线程内存泄漏、多线程死锁、多线程 crash
- 加载 gltf 模型材质，vulkan 内存屏障报错
- HardwareExecutor 默认选择不同的队列家族
- 移除 display 中的空命令提交
- 自动判断 image 的 layout
- 交换链逻辑错误
- 解决命令行的 Warning
- BUG: VMA 的 buffer 导入导出
- BUG: mutiview 初始化黑屏
- 图片导入导出：importImageMemory、exportImageMemory（VK_EXT_external_memory）
- bug: usd 模型导入概率性 vulkan 报错 device lost
- bug：intel 核显 内存屏障会寄

## 目录速览

- `include/`：对外 API
- `src/HardwareWrapper/`：对外对象包装层（RAII、引用计数与存储池）
- `src/HardwareWrapperVulkan/`：Vulkan 设备、资源、执行与显示
- `src/Helicon/`：DSL、AST、代码生成、编译与反射
- `examples/`：示例程序与 shader 资源
- `tools/`：`ShaderCompileScripts`、格式化脚本与统计工具
- `modules/corona/`：已内嵌的 Corona Framework（当前 Horizon 的基础依赖之一）
- `third-party/`：预置 `slang`、`dxc` 二进制与头库
- `docs/`：项目文档

## 构建要求

- CMake 4.0+
- Visual Studio 2022 或等效 MSVC 工具链（Windows 推荐）
- Vulkan SDK / Vulkan Runtime 环境
- 支持 C++20 的编译器

## 推荐构建方式（Windows）

更完整的 CMake 使用说明见 [docs/cmake-usage.md](docs/cmake-usage.md)。

当前仓库中的 Corona Framework 已直接纳入 `modules/corona/`，不再通过远程 `FetchContent` 拉取；其余第三方依赖仍可能在 configure 阶段由 CMake 自动获取。

### Ninja Multi-Config + MSVC（默认推荐）

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target HorizonExamples
```

### Visual Studio 2022 Generator

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug --target HorizonExamples
```

### 重要注意

- 所有 configure preset 共享同一个 `binaryDir=${sourceDir}/build`。
- 在 `ninja-msvc` 和 `vs2022` 之间切换前，请清理/重建 `build/`，避免生成器冲突。

## 示例程序

- 当前主示例入口为 `examples/main.cpp`（目标：`HorizonExamples`）。

## 最小 API 示例（与当前代码一致）

```cpp
HardwareExecutor executor;
ComputePipeline compute(embeddedSpirv);
compute(8, 8, 1);

executor << compute
         << executor.commit();
```

## 文档索引

- [docs/cmake-usage.md](docs/cmake-usage.md)：本项目 CMake 结构、preset、构建目标与常见问题说明

## 许可证

待补充。
