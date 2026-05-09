# Corona Framework Plugin Guide (Authoring / Loading / Lifecycle)

This document describes the current plugin ABI and loading flow, grounded in the code:

- Interface: `include/corona/kernel/core/i_plugin_manager.h`
- Implementation: `src/kernel/core/plugin_manager.cpp`
- Dynamic library loading: `include/corona/pal/i_dynamic_library.h`, `src/pal/platform/windows/win_dynamic_library.cpp`

> Note: This guide does not provide a full “build a plugin DLL with CMake” template (to avoid unverified build scaffolding).

## 1. Plugin Interface

Plugins implement `Corona::Kernel::IPlugin`:

- `bool initialize()`
- `void shutdown()`
- `PluginInfo get_info() const`

The framework uses `PluginInfo::name` as the plugin identifier.

## 2. Export ABI (Required)

A plugin dynamic library must export two symbols:

- `create_plugin`
- `destroy_plugin`

The expected signatures match `src/kernel/core/plugin_manager.cpp`:

```cpp
#include "corona/kernel/core/i_plugin_manager.h"

extern "C" {
    Corona::Kernel::IPlugin* create_plugin();
    void destroy_plugin(Corona::Kernel::IPlugin* plugin);
}
```

Recommendations:

- Use `extern "C"` to avoid name mangling (otherwise `get_symbol("create_plugin")` may fail).
- Ensure the symbols are exported (on Windows this is typically done via `__declspec(dllexport)` or a `.def` file; how you do this depends on your build system).

## 3. Lifecycle (Framework Side)

Current `PluginManager` flow:

1. `load_plugin(path)`
   - Loads the dynamic library
   - Resolves `create_plugin` / `destroy_plugin`
   - Calls `create_plugin()` to obtain an `IPlugin*`
   - Calls `plugin->get_info()` and stores it keyed by `info.name`
   - Wraps the raw pointer into `shared_ptr<IPlugin>` with a custom deleter calling `destroy_plugin(plugin)`

2. `initialize_all()`
   - Calls `plugin->initialize()` for each not-yet-initialized plugin
   - Dependency ordering is **not implemented yet** (there is a `TODO` in code)

3. `shutdown_all()`
   - Calls `plugin->shutdown()` for initialized plugins and marks them uninitialized

4. `unload_plugin(name)`
   - Calls `shutdown()` first if initialized
   - Erases the entry; releasing the `shared_ptr` triggers `destroy_plugin`

## 4. Loading Plugins in an App (Example)

```cpp
#include "corona/kernel/core/kernel_context.h"

int main() {
    auto& kernel = Corona::Kernel::KernelContext::instance();
    if (!kernel.initialize()) return 1;

    auto* pm = kernel.plugin_manager();

    if (!pm->load_plugin("plugins/my_plugin.dll")) {
        kernel.shutdown();
        return 1;
    }

    if (!pm->initialize_all()) {
        pm->shutdown_all();
        kernel.shutdown();
        return 1;
    }

    // ... use plugins

    pm->shutdown_all();
    kernel.shutdown();
    return 0;
}
```

## 5. Path and Encoding Notes (Windows)

The Windows loader uses `LoadLibraryW` and converts the provided path from UTF-8 to a wide string (see `src/pal/platform/windows/win_dynamic_library.cpp`).

Recommendations:

- Pass UTF-8 paths
- Prefer absolute paths or ensure the process working directory is correct

## 6. Cross-platform Status

Currently the repo only implements `IDynamicLibrary` on Windows (factory function `create_dynamic_library()` exists under `_WIN32`). Other platforms do not have an implementation yet.

As a result:

- Plugin loading is currently Windows-only unless PAL implementations are added.

## 7. Troubleshooting

- `load_plugin()` returns false
  - Wrong DLL path / working directory
  - Missing exports (`create_plugin`/`destroy_plugin`) or name mangling
  - `create_plugin()` returned nullptr

- `initialize_all()` returns false
  - A plugin’s `initialize()` failed (log details from inside the plugin)

- Plugin name collisions
  - Ensure `PluginInfo::name` is unique
