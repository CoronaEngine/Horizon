# Corona Framework 架构概览（高层）

本文是对当前仓库代码的高层导览，旨在帮助快速定位“入口在哪里、模块如何协作、从哪里扩展”。

## 1. 目录结构

- `include/`：对外公开接口头文件
- `src/kernel/`：Kernel 实现（核心服务、事件、系统、VFS、插件等）
- `src/pal/`：PAL（Platform Abstraction Layer）平台抽象实现
- `examples/`：从基础到完整主循环的示例（01~07）
- `tests/`：单元测试与多线程稳定性测试

## 2. 入口：KernelContext

核心入口为 `Corona::Kernel::KernelContext`（`include/corona/kernel/core/kernel_context.h`）。

- 通过 `KernelContext::instance()` 获取单例
- 必须先 `initialize()` 再使用服务
- 程序结束时调用 `shutdown()` 释放资源

`KernelContext` 内部持有并提供访问器：

- `event_bus()`：同步事件总线（即时调用）
- `event_stream()`：事件流（队列消息）
- `vfs()`：虚拟文件系统
- `plugin_manager()`：插件管理器
- `system_manager()`：系统管理器

## 3. 事件系统

### 3.1 事件总线（EventBus）

- 接口：`include/corona/kernel/event/i_event_bus.h`
- 实现：`src/kernel/event/event_bus.cpp`

特点：

- 类型安全的同步发布/订阅
- 发布时会在锁内复制订阅者列表，锁外调用回调，避免长时间持锁

### 3.2 事件流（EventStream）

- 接口/实现：`include/corona/kernel/event/i_event_stream.h`

特点：

- 每个订阅者独立队列
- 支持背压策略 `BackpressurePolicy`（阻塞、丢弃最旧、丢弃最新）
- 通过 `EventSubscription<T>` 以拉取/等待方式消费

## 4. 系统（Systems）与系统管理器

- 基类：`include/corona/kernel/system/system_base.h`
- 管理器：`include/corona/kernel/system/i_system_manager.h`、实现位于 `src/kernel/system/system_manager.cpp`

约定：

- 系统实现 `ISystem` 接口（常用直接继承 `SystemBase`）
- `SystemBase` 提供：线程生命周期、FPS 节流、暂停/恢复、性能统计
- `SystemManager` 负责：注册系统、按优先级初始化、统一 start/pause/resume/stop

示例参考：`examples/06_game_loop/main.cpp`。

## 5. 虚拟文件系统（VFS）

- 接口：`include/corona/kernel/core/i_vfs.h`
- 实现：`src/kernel/core/vfs.cpp`

VFS 负责：

- 将虚拟路径前缀映射到物理路径（mount points）
- 统一路径处理后，委托给 PAL 文件系统执行真实 I/O

## 6. 平台抽象层（PAL）

PAL 的目标是隔离平台差异，Kernel 通过 PAL 接口完成文件系统、动态库等系统能力。

- 文件系统接口：`include/corona/pal/i_file_system.h`
- Windows 动态库加载：`include/corona/pal/i_dynamic_library.h`、`src/pal/platform/windows/win_dynamic_library.cpp`

当前现状：

- 动态库加载（插件）侧：Windows 已实现；其他平台尚未提供实现。

## 7. 插件（PluginManager）

- 接口：`include/corona/kernel/core/i_plugin_manager.h`
- 实现：`src/kernel/core/plugin_manager.cpp`

要点：

- 插件动态库需要导出 `create_plugin`/`destroy_plugin`
- `load_plugin()` 负责加载动态库、创建插件对象、按 `PluginInfo::name` 注册
- `initialize_all()` 逐个 `initialize()`（当前尚未按依赖排序）
- `shutdown_all()` 统一 teardown

更详细内容见：`doc/plugin_guide_cn.md`。

## 8. 示例与测试

- 示例：`examples/`（01~07），目标名见 `examples/CMakeLists.txt`
- 测试：`tests/`，目标名见 `tests/CMakeLists.txt`

建议的阅读顺序：

1. `examples/01_event_bus`（同步事件）
2. `examples/03_event_stream`（异步事件流）
3. `examples/02_system_lifecycle`（系统生命周期）
4. `examples/06_game_loop`（整体联动）

## 9. 依赖（简述）

- 日志库 `quill` 通过 CMake `FetchContent` 拉取（`cmake/CoronaThirdParty.cmake`）
- TBB 默认来自仓库内 `third_party/win/oneapi-tbb-2022.3.0`，并在构建后复制运行时（`cmake/CoronaTbbConfig.cmake`）
