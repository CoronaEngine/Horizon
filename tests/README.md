# Horizon 测试入口

`HorizonTests` 是当前测试目录的统一入口。它和 `examples/main.cpp` 的思路类似：一个可执行文件集中运行多个测试用例，但每个测试用例必须带名称和说明，方便新人先看懂它测什么。

## 运行方式

查看测试清单和覆盖说明：

```powershell
build\ninja-msvc\tests\Debug\HorizonTests.exe --list
```

运行测试：

```powershell
build\ninja-msvc\tests\Debug\HorizonTests.exe
```

通过 CTest 运行：

```powershell
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure
```

构建测试目标：

```powershell
cmake --build --preset msvc-debug --target HorizonTests
```

Windows/MSVC 命令行验证时，优先先进入 Visual Studio Developer Command Prompt。

## 当前覆盖

- `hardware_context.lazy_construction`：构造 `HardwareContext` 不应创建 Vulkan instance，也不应枚举设备。
- `hardware_context.local_lifecycle`：局部 `HardwareContext` 通过 `instance()` 按需创建 `VkInstance`，通过 `devices()` / `main_device()` 按需加载设备和主设备。
- `hardware_context.global_entrypoints`：全局入口 `hardware_context()`、`vulkan_instance()`、`all_devices()`、`resource_manager()`、`device_manager()` 共用同一个 lazy singleton。

Vulkan 环境不满足要求时，相关用例返回 skip。CTest 使用返回码 `77` 识别整组测试全部跳过的情况。

## 新增测试约定

- 不要在测试模块里定义 `main()`；统一入口是 `tests/main.cpp`。
- 新测试模块返回 `std::vector<TestCase>`，并在 `tests/test_registry.h` 声明收集函数。
- 每个 `TestCase` 必须填写稳定的 `name` 和面向新人可读的 `description`。
- 普通失败直接抛出 `std::runtime_error`；环境缺失时返回 `TestResult::skip(...)`。
- 不要为了测试把生产接口变胖；确实需要检查内部状态时，优先使用测试专用 `friend` access shim。
