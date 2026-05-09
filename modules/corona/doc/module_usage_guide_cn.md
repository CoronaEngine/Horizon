# Corona Framework 模块指南

### 0. Quick Start（最小可运行）

最小使用流程：初始化 `KernelContext` → 使用服务 → `shutdown()`。

```cpp
#include "corona/kernel/core/kernel_context.h"

int main() {
  auto& kernel = Corona::Kernel::KernelContext::instance();
  if (!kernel.initialize()) {
    return 1;
  }

  // ... 使用 kernel.event_bus()/event_stream()/system_manager()/virtual_file_system() 等服务

  kernel.shutdown();
  return 0;
}
```

Windows（推荐使用 presets）：

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug
```

### 1. Kernel 模块（`src/kernel`）
- **KernelContext**（`src/kernel/core/kernel_context.cpp`）：单例入口，负责组装日志、事件总线、事件流、插件管理器、虚拟文件系统和系统管理器。必须在访问任何服务之前调用 `KernelContext::instance().initialize()`，结束时调用 `shutdown()` 做资源回收。
- **Logger**（`src/kernel/core/logger.cpp`）：提供同步控制台输出以及异步文件写入。可在初始化阶段注册自定义 sink，将日志导向游戏内 UI、网络或其他目标。
- **事件总线**（`include/corona/kernel/event/i_event_bus.h` 与 `src/kernel/event/event_bus.cpp`）：基于模板的同步发布/订阅。实现会在持锁状态下复制订阅者，再解锁后调用处理函数，避免长时间持有互斥量。典型用法：
  ```cpp
  auto bus = KernelContext::instance().event_bus();
  auto token = bus->subscribe<MyEvent>([](const MyEvent& evt) { /* 处理逻辑 */ });
  bus->publish(MyEvent{...});
  ```
  保持 `SubscribeToken` 有效以维持订阅；如需及时解绑，可主动销毁。
- **事件流**（`include/corona/kernel/event/i_event_stream.h`）：基于队列的异步消息管道，支持多种背压策略（丢弃旧消息、阻塞发布者等）。通过 `KernelContext::instance().event_stream()->get_stream<T>()` 获取流；发布端调用 `publish`，订阅端使用 `EventSubscription<T>` 轮询或注册回调。
- **SystemBase 与 SystemManager**（`include/corona/kernel/system/system_base.h`、`src/kernel/system/system_manager.cpp`）：用于构建多线程系统。继承 `SystemBase` 并重写 `on_initialize`、`on_update`、`on_shutdown`。使用 `SystemManager::register_system<YourSystem>()` 注册后，通过 `initialize_all()`、`start_all()` 控制生命周期；优先级决定初始化顺序。
- **虚拟文件系统**（`src/kernel/core/vfs.cpp`）：统一路径格式并委托至 PAL 文件系统。先挂载物理路径，再通过虚拟路径访问，如 `virtual_file_system()->open("root:/config.json")`。
- **PluginManager**（`include/corona/kernel/core/i_plugin_manager.h`、`src/kernel/core/plugin_manager.cpp`）：加载平台动态库，要求导出 `create_plugin`/`destroy_plugin`。使用 `load_plugin()` 加载，`initialize_all()` 初始化，结束时 `shutdown_all()`（按相反顺序关闭）。

  插件导出函数签名（建议使用 `extern "C"` 避免名称修饰，并确保符号被导出）：

  ```cpp
  #include "corona/kernel/core/i_plugin_manager.h"

  extern "C" {
      Corona::Kernel::IPlugin* create_plugin();
      void destroy_plugin(Corona::Kernel::IPlugin* plugin);
  }
  ```

### 2. 平台抽象层（`src/pal`）
- **StdFileSystem**（`src/pal/common/file_system.cpp`）：默认文件系统实现，封装操作系统 API 并兼顾 VFS 的路径规范和错误处理。
- **动态库接口**：接口定义在 `include/corona/pal/i_dynamic_library.h`。Windows 版本位于 `src/pal/platform/windows/win_dynamic_library.cpp`；Linux/macOS 当前仍为占位实现，会抛出 `std::runtime_error`。跨平台部署时需在调用前加平台判断。
- 扩展 PAL 时，在对应平台目录添加实现并在 `src/pal/CMakeLists.txt` 中注册。保持平台分支集中管理，避免核心模块直接引用系统头文件。

### 3. 内存工具（`src/kernel/memory`）
- 提供针对引擎优化的分配器与容器，如 Cache 对齐分配器、无锁队列等。公共头文件位于 `include/corona/kernel/memory/`，实现位于 `src/kernel/memory/`。
- 可参考 `tests/kernel/cache_aligned_allocator_test.cpp`、`lockfree_queue_test.cpp` 等测试了解用法与边界情况。
- 将这些分配器集成到系统中时，可作为 STL 兼容分配器使用，具备标准 `allocate`/`deallocate` 接口和 Traits 定义。

### 4. 示例程序（`examples/`）
- `01_event_bus` 至 `07_storage` 逐步展示框架能力，从基础事件到完整游戏循环。
- 示例索引：
  - `01_event_bus`：同步事件发布/订阅（事件总线）。
  - `02_system_lifecycle`：系统生命周期与 `SystemManager`。
  - `03_event_stream`：异步事件流与背压。
  - `04_vfs`：虚拟文件系统挂载与 I/O。
  - `05_multithreading`：多线程与并发使用示例。
  - `06_game_loop`：多系统协作的完整主循环。
  - `07_storage`：存储接口使用示例。
- `06_game_loop/main.cpp` 演示多个系统协同、事件流与系统管理器驱动主循环的完整流程。
- 构建示例命令：`cmake --build build --target <sample> --config <cfg>`，可执行文件位于 `build/examples/<cfg>/`。

### 5. 测试体系（`tests/`）
- 每个特性均有独立测试，包括多线程压力测试。阅读这些代码可快速了解预期行为及并发保障方案。
- 构建后运行 `ctest -C Debug`，或直接执行单个测试二进制（如 `build/tests/Debug/kernel_event_stream_test.exe`）进行定向调试。

补充：测试目标命名以 `tests/CMakeLists.txt` 中的 `corona_add_test(<target> <src...>)` 为准，生成的可执行文件名即 `<target>`。

### 6. 并发与错误处理约定
- 在持锁期间复制共享集合，随后释放锁再调用回调，是事件总线与事件流的通用模式。
- 系统需确保安全停机：在 `on_shutdown` 中停止后台任务并释放资源，避免崩溃或挂起。
- 共享标志位尽量使用 `std::atomic`；复杂状态结合 `std::mutex` 或 `std::shared_mutex`，可参考现有模块的实现。

### 7. 扩展框架的建议流程
1. 扩展内核接口或新增服务，记得在 `KernelContext` 初始化流程中注册，以供系统访问。
2. 若涉及平台差异，在 PAL 层新增实现并通过内核抽象暴露，例如自定义文件系统或网络模块。
3. 编写与现有风格一致的测试，特别是多线程压力测试，以验证性能与稳定性。
4. 将新增特性记录于 `doc/` 中，并在示例或 README 中补充使用说明。

### 8. 第三方依赖（CMake）

- `cmake/CoronaThirdParty.cmake` 通过 `FetchContent` 拉取 `quill`（v11.0.1）。
- `cmake/CoronaTbbConfig.cmake` 使用仓库内 `third_party/win/oneapi-tbb-2022.3.0` 作为默认 `TBB_DIR`，并在构建后自动复制 TBB 运行时到目标输出目录。
