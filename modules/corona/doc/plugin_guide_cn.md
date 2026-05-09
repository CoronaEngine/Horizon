# Corona Framework 插件指南（编写 / 加载 / 生命周期）

本文描述框架当前的插件 ABI 与加载流程，内容以代码实现为准：

- 接口：`include/corona/kernel/core/i_plugin_manager.h`
- 实现：`src/kernel/core/plugin_manager.cpp`
- 动态库加载：`include/corona/pal/i_dynamic_library.h`、`src/pal/platform/windows/win_dynamic_library.cpp`

> 说明：本文不包含“如何用 CMake 构建插件 DLL”的完整工程模板（避免提供未经验证的构建方案）。

## 1. 插件接口

插件需要实现 `Corona::Kernel::IPlugin`：

- `bool initialize()`：加载后初始化
- `void shutdown()`：关闭并释放资源
- `PluginInfo get_info() const`：返回插件元数据（至少包含 `name`）

框架侧通过 `PluginInfo::name` 作为插件唯一标识。

## 2. 导出 ABI（必须）

插件动态库必须导出两个符号：

- `create_plugin`
- `destroy_plugin`

其签名与框架实现一致（见 `src/kernel/core/plugin_manager.cpp`）：

```cpp
#include "corona/kernel/core/i_plugin_manager.h"

extern "C" {
    Corona::Kernel::IPlugin* create_plugin();
    void destroy_plugin(Corona::Kernel::IPlugin* plugin);
}
```

建议：

- 使用 `extern "C"` 避免名称修饰导致 `get_symbol("create_plugin")` 失败。
- 确保符号被导出（Windows 下通常需要 `__declspec(dllexport)` 或 `.def` 文件；具体由你的构建系统决定）。

## 3. 生命周期（框架侧）

框架当前的 `PluginManager` 行为：

1. `load_plugin(path)`
   - 加载动态库
   - 查找 `create_plugin`/`destroy_plugin`
   - 调用 `create_plugin()` 得到 `IPlugin*`
   - 调用 `plugin->get_info()` 并以 `info.name` 存入表
   - 用 `shared_ptr<IPlugin>` + 自定义 deleter 绑定 `destroy_plugin(plugin)`

2. `initialize_all()`
   - 对尚未初始化的插件依次调用 `plugin->initialize()`
   - 当前实现 **尚未**按依赖排序（源码中有 `TODO`）

3. `shutdown_all()`
   - 对已初始化插件调用 `plugin->shutdown()` 并标记为未初始化

4. `unload_plugin(name)`
   - 若已初始化则先 `shutdown()`
   - 从表中移除（shared_ptr 释放时触发 `destroy_plugin`）

> 注意：当前 `PluginManager` 析构时会自动调用 `shutdown_all()`。

## 4. 在应用中加载插件（示例）

```cpp
#include "corona/kernel/core/kernel_context.h"

int main() {
    auto& kernel = Corona::Kernel::KernelContext::instance();
    if (!kernel.initialize()) return 1;

    auto* pm = kernel.plugin_manager();

    // 例：加载一个 DLL
    if (!pm->load_plugin("plugins/my_plugin.dll")) {
        kernel.shutdown();
        return 1;
    }

    if (!pm->initialize_all()) {
        pm->shutdown_all();
        kernel.shutdown();
        return 1;
    }

    // ... 使用插件

    pm->shutdown_all();
    kernel.shutdown();
    return 0;
}
```

## 5. 路径与编码注意事项（Windows）

Windows 动态库加载实现使用 `LoadLibraryW`，并将传入路径按 UTF-8 转换为宽字符串（见 `src/pal/platform/windows/win_dynamic_library.cpp`）。

建议：

- 传入 UTF-8 编码的路径字符串
- 优先使用绝对路径或确保进程工作目录正确

## 6. 跨平台现状

当前仓库只实现了 Windows 的 `IDynamicLibrary` 工厂函数 `create_dynamic_library()`；其他平台尚未提供实现。

因此：

- 插件加载在非 Windows 平台暂不可用（除非你补齐 PAL 层实现）。

## 7. 常见故障排查

- `load_plugin()` 返回 false
  - DLL 路径不对 / 工作目录不对
  - 未导出 `create_plugin`/`destroy_plugin` 或被 C++ 名称修饰
  - `create_plugin()` 返回了空指针

- `initialize_all()` 返回 false
  - 某个插件的 `initialize()` 失败（建议在插件内使用框架日志输出原因）

- 插件 `name` 冲突
  - `PluginInfo::name` 重复会导致后加载的插件插入失败/覆盖风险（建议确保唯一命名）
