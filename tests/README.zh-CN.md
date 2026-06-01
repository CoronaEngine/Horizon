# Horizon 测试入口

这是 `tests/` 目录的中文源文档。英文默认入口是 `README.md`，两者应一起更新。

这组 README 是测试说明文档，不属于 `.agents` / `docs/agents` 同步机制，也不应该做成 skill。新增或删除测试模块、测试用例覆盖意图发生变化时，同步更新这两个文件；只调整测试内部实现而覆盖意图不变时，不必改文档。

英文文件顶部的 `TESTS_README_ZH_CN_SHA256` 保存中文源文件的 SHA256，用于手动识别英文文档是否过期；它有意不接入 `tools/sync-agents.ps1`。

## 运行方式

构建测试目标：

```powershell
cmake --build --preset msvc-debug --target HorizonTests
```

查看测试清单和覆盖说明：

```powershell
build\ninja-msvc\tests\Debug\HorizonTests.exe --list
```

运行全部测试：

```powershell
build\ninja-msvc\tests\Debug\HorizonTests.exe
```

通过 CTest 运行：

```powershell
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure
```

Windows/MSVC 命令行验证时，优先先进入 Visual Studio Developer Command Prompt。

## 测试框架结构

`HorizonTests` 是当前测试目录的统一入口。它和 `examples/main.cpp` 的思路类似：一个可执行文件集中运行多个测试用例，但每个测试用例都必须带稳定名称和说明，方便新人先看懂它测什么。

- `tests/main.cpp`：收集测试、处理 `--list`、按名称前缀过滤、运行测试并汇总结果。
- `tests/test_registry.h`：定义 `TestCase`、`TestResult` 和各测试模块的收集函数声明。
- `tests/CMakeLists.txt`：定义 `HorizonTests` 目标，并把 CTest 的整组跳过返回码设为 `77`。
- `tests/vulkan/*.cpp`：当前 lower-case Vulkan backend 的测试模块。

`TestCase::name` 用于命令行过滤、CTest 输出和日志搜索；`TestCase::description` 用于解释覆盖意图。`HorizonTests.exe --list` 会打印所有测试名称和说明。

## 当前测试模块

### `tests/vulkan/test_hardware_context.cpp`

验证 lower-case Vulkan backend 的 `HardwareContext` lazy 初始化、真实 Vulkan 环境门禁，以及全局入口是否共享同一个 lazy singleton。

这个文件会先做 Vulkan 环境预检：

- `volkInitialize()` 必须成功。
- Vulkan loader 需要报告至少 `VK_API_VERSION_1_4`。
- 能创建临时 `VkInstance`。
- 能枚举物理设备。
- 至少存在一个 Vulkan 1.4-capable physical device。

环境不满足时，相关用例返回 `TestResult::skip(...)`，而不是把缺少 Vulkan 环境当作测试失败。

覆盖用例：

- `hardware_context.lazy_construction`
  - 构造局部 `HardwareContext` 不应创建 `VkInstance`。
  - 构造局部 `HardwareContext` 不应枚举 Vulkan 设备。
  - 通过测试专用 `HardwareContextTestAccess` 检查内部 lazy 状态，避免把测试探针暴露成生产接口。

- `hardware_context.local_lifecycle`
  - 新建局部 `HardwareContext` 时 instance 和 devices 都应保持未加载。
  - 调用 `instance()` 时按需创建 `VkInstance`，但不应顺带枚举设备。
  - 调用 `devices()` / `main_device()` 时按需枚举设备、创建每个 `DeviceContext`，并选出主设备。
  - 每个 `DeviceContext` 应包含有效的 `DeviceManager`。
  - `DeviceManager` 应保留选中的 `VkPhysicalDevice`、创建 `VkDevice`、记录 queue family，并暴露至少一个可用于 transfer work 的 queue。

- `hardware_context.concurrent_access`
  - 多个线程同时调用同一个局部 `HardwareContext` 的 `instance()`、`devices()` 和 `main_device()` 时，应发布同一个 `VkInstance`。
  - `devices()` 应返回持有 `shared_ptr` 的设备快照，所有线程都应看到一致的设备数量和同一个 main device。

- `hardware_context.global_entrypoints`
  - `hardware_context()` 应返回稳定的全局 singleton。
  - `vulkan_instance()` 应通过同一个 singleton 创建并返回 `VkInstance`。
  - `all_devices()` 应通过同一个 singleton 加载并返回设备列表快照。
  - `main_device_context()`、`resource_manager()`、`device_manager()` 应来自同一个被选中的 main device。

这个模块不测试渲染、swapchain、真实 present、资源分配策略或命令编码；它只保护 Vulkan 上下文和设备初始化入口的生命周期边界。

### `tests/vulkan/test_hardware_buffer.cpp`

验证 lower-case Vulkan backend 的 `HardwareBuffer` 创建、host-mapped 读写、copy / move 句柄生命周期、范围校验和并发访问边界。

这个文件使用和 `test_hardware_context.cpp` 相同的 Vulkan 环境预检；环境不满足时返回 `TestResult::skip(...)`。测试会创建真实 Vulkan buffer 和 VMA allocation，因此需要可用的 Vulkan 1.4 设备。

覆盖用例：

- `hardware_buffer.create_upload_read_write`
  - `HardwareBuffer::storage()` 应创建有效 buffer。
  - element size、element count 和 byte size 应保留描述符语义。
  - `CpuAccessMode::ReadWrite` buffer 应暴露 host mapped memory。
  - 初始 upload data 应能 read back。
  - typed element range write 和 single element write 应只修改指定区间。

- `hardware_buffer.copy_move_lifetime`
  - copy 构造、move 构造和 copy 赋值应共享同一个 resource id。
  - reset 原始 wrapper 后，copy / survivor wrapper 应继续保持底层资源有效。
  - 最后一个 `HardwareBuffer` wrapper reset 后，`ResourceBridge` token 应释放。

- `hardware_buffer.range_and_mapping`
  - validation `Throw` 模式下，越界 host write 应抛出 `std::invalid_argument`。
  - validation 关闭时，越界 host read / write 应返回 `false`，不写越界内存。
  - `CpuAccessMode::None` buffer 应能创建真实 Vulkan buffer，但不暴露 mapped host memory。

- `hardware_buffer.concurrent_disjoint_io`
  - 多个线程可以复制同一个 `HardwareBuffer` wrapper，并共享同一个 resource id。
  - 多个线程写入互不重叠的 host-mapped 区间后，每个线程能读回自己的区间。
  - 全部线程完成后，从原始 wrapper 读回的完整数据应匹配预期。

这个模块不测试 GPU command buffer copy、staging upload、descriptor 绑定、pipeline 使用、external memory import/export 或真实 GPU barrier；这些应放在后续 encoder / descriptor / execution smoke 测试中。

### `tests/vulkan/test_execution_system.cpp`

验证 lower-case Vulkan execution stack 的无 GPU 路径：fake queue、timeline retirement、keep-alive 生命周期、命令 IR 记录、编译计划、stream facade、present receipt 和并发边界。

这些测试主要使用可注入的 `Queue` / `HardwareExecutor` 路径，不要求本机存在可用 Vulkan 设备。真实 Vulkan smoke 仍由 `test_hardware_context.cpp` 负责。

覆盖用例：

- `execution.keep_alive_retirement`：提交资源在 timeline 完成前必须继续存活，command buffer retire 后释放。
- `execution.partial_timeline_retirement`：只 retire 已完成 timeline value 的提交，更新的 in-flight work 必须保留。
- `execution.command_buffer_pool_reuse`：已 retire 的 command buffer 应优先复用，并获得新的 recording id。
- `execution.submit_auto_command_buffer`：调用者没有预先 acquire command buffer 时，`Queue::submit()` 应自动创建并跟踪一个。
- `execution.submit_failure_keeps_resources`：注入提交失败时，keep-alive 和 command buffer 所有权留在调用者提交对象里。
- `execution.recorder_compiler_ir`：`CommandRecorder` 只记录抽象 IR，`ExecutionCompiler` 收集 queue 需求、keep-alive 和资源 hazard。
- `execution.hardware_executor_injected_queue`：`HardwareExecutor` 应通过注入的 queue resolver 提交编译后的 work。
- `execution.stream_facade_commit`：`HardwareStream` 应接受 ocarina-style 命令并通过 executor queues commit。
- `execution.stream_batch_order`：`CommandBatch` 和 `HardwareStream` 在 compile 前保持 typed IR 顺序。
- `execution.ocarina_value_commands`：value command 对象能 erase 进 `CommandBatch` / `HardwareStream`。
- `execution.compiler_dag_order`：非连续 queue batch 不能跨不同 queue 类型错误合并，资源复用需要显式 DAG dependency。
- `execution.host_callback_retire`：host callback 由 command buffer keep-alive 持有，并在 timeline retire 时执行。
- `execution.present_receipt`：present node 通过 present queue 提交，并在 `SubmitReceipt` 中报告 present 状态。
- `execution.cross_device_present`：跨设备 present 可记录 CPU bridge fallback，普通跨设备资源 hazard 在没有显式同步时应失败。
- `execution.parallel_record_and_submit`：独立 recorders 可以并发 close，`Queue` 必须串行化并发 fake submissions 的 timeline 增量。

这个模块不填充真实 `VkCommandBuffer`，不创建 pipeline / descriptor，也不验证实际 GPU 执行结果。它保护 execution 计划和提交生命周期，而不是 Vulkan encoder。

## 新增测试约定

- 不要在测试模块里定义 `main()`；统一入口是 `tests/main.cpp`。
- 新测试模块返回 `std::vector<TestCase>`，并在 `tests/test_registry.h` 声明收集函数。
- 在 `tests/main.cpp` 的 `collect_tests()` 中追加新模块。
- 在 `tests/CMakeLists.txt` 中加入新的测试源文件。
- 每个 `TestCase` 必须填写稳定的 `name` 和面向新人可读的 `description`。
- `name` 使用英文/ASCII，保持命令行过滤、CTest 输出和日志搜索稳定。
- `description`、失败信息和 skip 原因可以使用中文；测试目标会通过 `horizon_add_test()` 固定 MSVC 的 UTF-8 源码和执行字符集。
- 普通失败直接抛出 `std::runtime_error`；环境缺失时返回 `TestResult::skip(...)`。
- 不要为了测试把生产接口变胖；确实需要检查内部状态时，优先使用测试专用 `friend` access shim。
- 新增、删除或重命名测试模块时，同步更新 `README.zh-CN.md` 和 `README.md`。

## Skip 规则

`HorizonTests` 中单个用例可以返回 `TestResult::skip(...)` 表示当前机器缺少必要环境。当前 runner 的规则是：

- 存在失败用例时，返回失败。
- 没有失败且至少有一个通过用例时，返回成功。
- 所有运行到的用例都跳过时，返回 `77`。

`tests/CMakeLists.txt` 把 `77` 配置为 CTest 的 `SKIP_RETURN_CODE`，因此没有 Vulkan 环境的机器可以明确显示 skip，而不是误报失败。
