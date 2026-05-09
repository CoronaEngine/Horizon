# Corona Framework Module Guide

### 0. Quick Start (Minimum)

Minimal usage flow: initialize `KernelContext` → use services → `shutdown()`.

```cpp
#include "corona/kernel/core/kernel_context.h"

int main() {
  auto& kernel = Corona::Kernel::KernelContext::instance();
  if (!kernel.initialize()) {
    return 1;
  }

  // ... use kernel.event_bus()/event_stream()/system_manager()/virtual_file_system() etc.

  kernel.shutdown();
  return 0;
}
```

Windows (recommended via presets):

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug
```

### 1. Kernel Module (`src/kernel`)
- **KernelContext** (`src/kernel/core/kernel_context.cpp`): singleton that wires logger, event bus, event streams, plugin manager, virtual file system (VFS), and system manager. Always call `KernelContext::instance().initialize()` before using services and `shutdown()` when the application terminates.
- **Logger** (`src/kernel/core/logger.cpp`): provides synchronous console output and asynchronous file sinks. Register additional sinks during initialization to direct logs to custom destinations.
- **Event Bus** (`include/corona/kernel/event/i_event_bus.h`, `src/kernel/event/event_bus.cpp`): type-safe synchronous publish/subscribe. Handlers are copied outside the lock before invocation, so avoid capturing large state by value. Typical usage:
  ```cpp
  auto bus = KernelContext::instance().event_bus();
  auto token = bus->subscribe<MyEvent>([](const MyEvent& evt) { /* handle */ });
  bus->publish(MyEvent{...});
  ```
  Keep `SubscribeToken` instances alive to retain subscriptions; release explicitly for deterministic teardown.
- **SystemBase & SystemManager** (`include/corona/kernel/system/system_base.h`, `src/kernel/system/system_manager.cpp`): build multi-threaded systems. Derive from `SystemBase`, override lifecycle hooks (`on_initialize`, `on_update`, `on_shutdown`), and register via `SystemManager::register_system<YourSystem>()`. Priority determines scheduling order during `initialize_all()`. Systems start on dedicated threads when `start_all()` is invoked.
- **Virtual File System** (`src/kernel/core/vfs.cpp`): normalizes virtual paths and dispatches to PAL file systems. Mount physical roots with identifiers and resolve via `virtual_file_system()->open("root:/path.txt").`
- **PluginManager** (`include/corona/kernel/core/i_plugin_manager.h`, `src/kernel/core/plugin_manager.cpp`): loads platform dynamic libraries exporting `create_plugin`/`destroy_plugin`. Use `load_plugin()` to load, `initialize_all()` to initialize, and `shutdown_all()` to teardown.

  Export ABI (use `extern "C"` to avoid name mangling, and ensure symbols are exported):

  ```cpp
  #include "corona/kernel/core/i_plugin_manager.h"

  extern "C" {
      Corona::Kernel::IPlugin* create_plugin();
      void destroy_plugin(Corona::Kernel::IPlugin* plugin);
  }
  ```

### 2. Platform Abstraction Layer (`src/pal`)
- **StdFileSystem** (`src/pal/common/file_system.cpp`): default PAL filesystem. Implements open/read/write against the OS APIs while honoring VFS expectations (normalized paths, error propagation).
- **Dynamic Library Interfaces**: `IDynamicLibrary` under `include/corona/pal/i_dynamic_library.h`. Windows implementation is `WinDynamicLibrary` (`src/pal/platform/windows/win_dynamic_library.cpp`). Linux/macOS stubs currently throw `std::runtime_error`; guard usage with platform checks when shipping cross-platform plugins.
- Extend PAL by adding new platform-specific directories and wiring them in `src/pal/CMakeLists.txt`. Keep platform decisions centralized to prevent kernel modules from importing OS headers directly.

### 3. Memory Utilities (`src/kernel/memory`)
- Provides allocators and containers optimized for the engine (e.g., cache-aligned allocator, lock-free queues). Public headers live under `include/corona/kernel/memory/` and implementations under `src/kernel/memory/`.
- Tests such as `tests/kernel/cache_aligned_allocator_test.cpp` and `lockfree_queue_test.cpp` demonstrate expected semantics and edge cases.
- Integrate with systems by including relevant headers; allocators often expose STL-compatible interfaces (`allocate`, `deallocate`, traits).

### 4. Examples (`examples/`)
- **01_event_bus** through **07_storage** show incremental usage: from basic pub/sub to full game loop.
- Index:
  - `01_event_bus`: synchronous publish/subscribe via event bus.
  - `02_system_lifecycle`: system lifecycle and `SystemManager`.
  - `03_event_stream`: async event streams and backpressure.
  - `04_vfs`: VFS mounting and I/O.
  - `05_multithreading`: concurrency usage patterns.
  - `06_game_loop`: full loop with multiple systems.
  - `07_storage`: storage usage.
- `06_game_loop/main.cpp` demonstrates coordinating multiple systems, subscribing to event streams, and driving the main loop via `SystemManager`.
- Build specific samples with `cmake --build build --target <sample> --config <cfg>` and run from `build/examples/<cfg>/`.

### 5. Testing (`tests/`)
- Each feature has dedicated tests (single-threaded and multi-threaded). Use them to understand correct behavior and concurrency guarantees.
- Run `ctest -C Debug` after builds or execute individual binaries (e.g., `build/tests/Debug/kernel_event_stream_test.exe`) for focused debugging.

Note: test executable names come from `tests/CMakeLists.txt` via `corona_add_test(<target> <src...>)`; the produced binary name is the `<target>`.

### 6. Concurrency & Error Handling Patterns
- Copy shared handler collections while holding mutexes, then release the lock before invoking callbacks (pattern used in event bus and event streams).
- Systems must implement safe shutdown; ensure `on_shutdown` stops background work and releases resources.
- Prefer `std::atomic` for shared flags and guard complex state with `std::mutex` or `std::shared_mutex` as seen in existing modules.

### 7. Extending the Framework
1. Add new systems or services by extending kernel interfaces; wire them through `KernelContext` initialization to expose consistently.
2. Introduce platform features in PAL, then surface through kernel abstractions (e.g., new file system backend or network layer).
3. Write tests mirroring existing patterns, especially multi-thread stress tests, to validate concurrency and performance.
4. Document new behavior in `doc/` and add example usage where appropriate.

### 8. Third-party Dependencies (CMake)

- `cmake/CoronaThirdParty.cmake` fetches `quill` (v11.0.1) via `FetchContent`.
- `cmake/CoronaTbbConfig.cmake` defaults `TBB_DIR` to `third_party/win/oneapi-tbb-2022.3.0` and copies TBB runtime artifacts to target output directories post-build.
