# Corona Framework Architecture Overview (High-level)

This is a high-level tour of the current repository: where the entry points are, how modules collaborate, and where to extend.

## 1. Layout

- `include/`: public headers
- `src/kernel/`: kernel implementations (core services, events, systems, VFS, plugins)
- `src/pal/`: PAL (Platform Abstraction Layer)
- `examples/`: examples 01~07
- `tests/`: unit tests and multi-thread stability tests

## 2. Entry Point: KernelContext

The main entry is `Corona::Kernel::KernelContext` (`include/corona/kernel/core/kernel_context.h`).

- Use `KernelContext::instance()` to get the singleton
- Call `initialize()` before using any service
- Call `shutdown()` at program end

It provides accessors for:

- `event_bus()`
- `event_stream()`
- `vfs()`
- `plugin_manager()`
- `system_manager()`

## 3. Events

### 3.1 Event Bus

- Interface: `include/corona/kernel/event/i_event_bus.h`
- Implementation: `src/kernel/event/event_bus.cpp`

Properties:

- Type-safe synchronous publish/subscribe
- Copies handler lists under lock and invokes callbacks outside the lock

### 3.2 Event Streams

- Interface/implementation: `include/corona/kernel/event/i_event_stream.h`

Properties:

- Per-subscriber queues
- Backpressure via `BackpressurePolicy` (block / drop oldest / drop newest)
- Consumers use `EventSubscription<T>` to poll or wait

## 4. Systems and SystemManager

- Base class: `include/corona/kernel/system/system_base.h`
- Manager: `include/corona/kernel/system/i_system_manager.h` (impl: `src/kernel/system/system_manager.cpp`)

Conventions:

- Systems implement `ISystem` (typically by deriving from `SystemBase`)
- `SystemBase` manages a dedicated thread, FPS throttling, pause/resume, and basic perf stats
- `SystemManager` registers systems, initializes them by priority, and drives start/pause/resume/stop

See: `examples/06_game_loop/main.cpp`.

## 5. Virtual File System (VFS)

- Interface: `include/corona/kernel/core/i_vfs.h`
- Implementation: `src/kernel/core/vfs.cpp`

VFS:

- Maps virtual path prefixes to physical paths (mount points)
- Delegates actual I/O to PAL file system implementations

## 6. PAL (Platform Abstraction Layer)

PAL isolates platform-specific APIs behind interfaces.

- File system interface: `include/corona/pal/i_file_system.h`
- Windows dynamic library: `include/corona/pal/i_dynamic_library.h`, `src/pal/platform/windows/win_dynamic_library.cpp`

Current status:

- Dynamic library loading (and therefore plugins) is implemented on Windows only in this repo.

## 7. Plugins

- Interface: `include/corona/kernel/core/i_plugin_manager.h`
- Implementation: `src/kernel/core/plugin_manager.cpp`

Highlights:

- Plugin DLL exports `create_plugin` / `destroy_plugin`
- `load_plugin()` loads the library and registers the plugin by `PluginInfo::name`
- `initialize_all()` calls `initialize()` (dependency ordering is not implemented yet)
- `shutdown_all()` performs teardown

See: `doc/plugin_guide.md`.

## 8. Examples and Tests

- Examples: `examples/` (targets defined in `examples/CMakeLists.txt`)
- Tests: `tests/` (targets defined in `tests/CMakeLists.txt`)

Suggested reading order:

1. `examples/01_event_bus`
2. `examples/03_event_stream`
3. `examples/02_system_lifecycle`
4. `examples/06_game_loop`

## 9. Dependencies (brief)

- `quill` is fetched via CMake `FetchContent` (`cmake/CoronaThirdParty.cmake`)
- TBB defaults to `third_party/win/oneapi-tbb-2022.3.0` and runtime artifacts are copied post-build (`cmake/CoronaTbbConfig.cmake`)
