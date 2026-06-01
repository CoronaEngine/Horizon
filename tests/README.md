# Horizon Test Entry
<!-- TESTS_README_ZH_CN_SHA256: 58e6c349b7fbdc87e72aae756000b6bde9ad2ad833aaf6e7647b259f0b9238e2 -->

Chinese source: `README.zh-CN.md`. Keep this English default entry in sync with that file.

These README files document the test suite. They are not part of the `.agents` / `docs/agents` synchronization mechanism, and they should not become a skill. Update both files when adding or removing a test module, or when the intended coverage of a test changes. Implementation-only test edits do not need README changes.

The `TESTS_README_ZH_CN_SHA256` marker stores the SHA256 of the Chinese source for manual staleness checks. It is intentionally not wired into `tools/sync-agents.ps1`.

## Running Tests

Build the test target:

```powershell
cmake --build --preset msvc-debug --target HorizonTests
```

List tests and their coverage descriptions:

```powershell
build\ninja-msvc\tests\Debug\HorizonTests.exe --list
```

Run all tests:

```powershell
build\ninja-msvc\tests\Debug\HorizonTests.exe
```

Run through CTest:

```powershell
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure
```

On Windows/MSVC command-line validation, prefer entering the Visual Studio Developer Command Prompt first.

## Test Harness

`HorizonTests` is the unified entrypoint for the current test directory. It is similar in shape to `examples/main.cpp`: one executable runs multiple test cases, and every test case carries a stable name plus a readable description so newcomers can see what it covers before reading implementation details.

- `tests/main.cpp`: collects tests, handles `--list`, filters by name prefix, runs tests, and prints the summary.
- `tests/test_registry.h`: defines `TestCase`, `TestResult`, and each test module collection function declaration.
- `tests/CMakeLists.txt`: defines the `HorizonTests` target and maps the all-skipped return code to CTest skip code `77`.
- `tests/vulkan/*.cpp`: current lower-case Vulkan backend test modules.

`TestCase::name` is used for command-line filtering, CTest output, and log search. `TestCase::description` explains the intended coverage. `HorizonTests.exe --list` prints all test names and descriptions.

## Current Modules

### `tests/vulkan/test_hardware_context.cpp`

Validates lower-case Vulkan `HardwareContext` lazy initialization, real Vulkan environment gating, and whether global helper entrypoints share one lazy singleton.

This file runs a Vulkan environment precheck first:

- `volkInitialize()` must succeed.
- The Vulkan loader must report at least `VK_API_VERSION_1_4`.
- A temporary `VkInstance` must be creatable.
- Physical devices must be enumerable.
- At least one physical device must be Vulkan 1.4-capable.

When the environment is unavailable, the related cases return `TestResult::skip(...)` instead of treating missing Vulkan support as a test failure.

Covered cases:

- `hardware_context.lazy_construction`
  - Constructing a local `HardwareContext` must not create a `VkInstance`.
  - Constructing a local `HardwareContext` must not enumerate Vulkan devices.
  - The test uses `HardwareContextTestAccess` to inspect lazy state without exposing test probes as production API.

- `hardware_context.local_lifecycle`
  - A new local `HardwareContext` starts with instance and devices unloaded.
  - Calling `instance()` creates `VkInstance` on demand, without enumerating devices as a side effect.
  - Calling `devices()` / `main_device()` enumerates devices, creates each `DeviceContext`, and selects a main device on demand.
  - Every `DeviceContext` contains a valid `DeviceManager`.
  - `DeviceManager` keeps the selected `VkPhysicalDevice`, creates a `VkDevice`, records queue families, and exposes at least one queue usable for transfer work.

- `hardware_context.concurrent_access`
  - Multiple threads calling `instance()`, `devices()`, and `main_device()` on the same local `HardwareContext` must publish the same `VkInstance`.
  - `devices()` returns a device snapshot that holds `shared_ptr` ownership, and all threads should observe a consistent device count and the same main device.

- `hardware_context.global_entrypoints`
  - `hardware_context()` returns a stable global singleton.
  - `vulkan_instance()` creates and returns `VkInstance` through the same singleton.
  - `all_devices()` loads and returns a device snapshot through the same singleton.
  - `main_device_context()`, `resource_manager()`, and `device_manager()` come from the selected main device.

This module does not test rendering, swapchains, real present, resource allocation policy, or command encoding. It protects Vulkan context and device initialization entrypoint lifetime boundaries.

### `tests/vulkan/test_hardware_buffer.cpp`

Validates lower-case Vulkan `HardwareBuffer` creation, host-mapped read/write, copy / move handle lifetime, range validation, and concurrent access boundaries.

This file uses the same Vulkan environment precheck as `test_hardware_context.cpp`. When the environment is unavailable, cases return `TestResult::skip(...)`. The tests create real Vulkan buffers and VMA allocations, so they require an available Vulkan 1.4 device.

Covered cases:

- `hardware_buffer.create_upload_read_write`
  - `HardwareBuffer::storage()` creates a valid buffer.
  - Element size, element count, and byte size preserve descriptor semantics.
  - A `CpuAccessMode::ReadWrite` buffer exposes host mapped memory.
  - Initial upload data can be read back.
  - Typed element range writes and single element writes update only the requested range.

- `hardware_buffer.copy_move_lifetime`
  - Copy construction, move construction, and copy assignment share one resource id.
  - Resetting the original wrapper keeps the underlying resource alive through copy / survivor wrappers.
  - After the last `HardwareBuffer` wrapper resets, the `ResourceBridge` token is released.

- `hardware_buffer.range_and_mapping`
  - In validation `Throw` mode, out-of-range host writes throw `std::invalid_argument`.
  - With validation disabled, out-of-range host reads / writes return `false` instead of writing past bounds.
  - A `CpuAccessMode::None` buffer can create a real Vulkan buffer but does not expose mapped host memory.

- `hardware_buffer.concurrent_disjoint_io`
  - Multiple threads can copy one `HardwareBuffer` wrapper and share one resource id.
  - Multiple threads can write non-overlapping host-mapped ranges and read back their own ranges.
  - After all threads complete, reading from the original wrapper returns the expected full data.

This module does not test GPU command-buffer copy, staging upload, descriptor binding, pipeline use, external memory import/export, or real GPU barriers. Those belong in later encoder / descriptor / execution smoke tests.

### `tests/vulkan/test_execution_system.cpp`

Validates the no-GPU path of the lower-case Vulkan execution stack: fake queues, timeline retirement, keep-alive lifetimes, command IR recording, compile plans, the stream facade, present receipts, and concurrency boundaries.

These tests mostly use injectable `Queue` / `HardwareExecutor` paths and do not require a local Vulkan device. Real Vulkan smoke coverage remains in `test_hardware_context.cpp`.

Covered cases:

- `execution.keep_alive_retirement`: submitted resources stay alive until their timeline completion value retires the command buffer.
- `execution.partial_timeline_retirement`: only submissions with completed timeline values retire; newer in-flight work remains alive.
- `execution.command_buffer_pool_reuse`: retired command buffers are reused before allocating new ones and receive a new recording id.
- `execution.submit_auto_command_buffer`: `Queue::submit()` creates and tracks a command buffer when the caller did not acquire one first.
- `execution.submit_failure_keeps_resources`: injected submit failure leaves keep-alive and command buffer ownership in the caller submission.
- `execution.recorder_compiler_ir`: `CommandRecorder` records only abstract IR, and `ExecutionCompiler` collects queue requirements, keep-alives, and resource hazards.
- `execution.hardware_executor_injected_queue`: `HardwareExecutor` submits compiled work through an injected queue resolver.
- `execution.stream_facade_commit`: `HardwareStream` accepts ocarina-style commands and commits them through executor queues.
- `execution.stream_batch_order`: `CommandBatch` and `HardwareStream` preserve typed IR order before compile.
- `execution.ocarina_value_commands`: value command objects erase into `CommandBatch` / `HardwareStream`.
- `execution.compiler_dag_order`: non-contiguous queue batches are not incorrectly merged across queue types, and resource reuse creates explicit DAG dependencies.
- `execution.host_callback_retire`: host callbacks are retained by command buffer keep-alives and run when the timeline retires.
- `execution.present_receipt`: present nodes submit through the present queue and report status through `SubmitReceipt`.
- `execution.cross_device_present`: cross-device present can record a CPU bridge fallback, while ordinary cross-device resource hazards fail without explicit sync.
- `execution.parallel_record_and_submit`: independent recorders can close concurrently, and `Queue` serializes timeline increments for parallel fake submissions.

This module does not fill real `VkCommandBuffer` objects, create pipelines or descriptors, or verify actual GPU execution results. It protects execution planning and submission lifetimes, not the Vulkan encoder.

## Adding Tests

- Do not define `main()` in test modules; the unified entrypoint is `tests/main.cpp`.
- A new test module returns `std::vector<TestCase>` and declares its collection function in `tests/test_registry.h`.
- Append the new module in `tests/main.cpp` inside `collect_tests()`.
- Add the new test source file to `tests/CMakeLists.txt`.
- Every `TestCase` must provide a stable `name` and a newcomer-readable `description`.
- Use English/ASCII for `name` so command-line filtering, CTest output, and log search remain stable.
- `description`, failure messages, and skip reasons may use Chinese; the test target uses `horizon_add_test()` to fix MSVC source and execution charsets to UTF-8.
- Throw `std::runtime_error` for ordinary failures; return `TestResult::skip(...)` when the environment is unavailable.
- Do not make production APIs larger just for tests. When internal state must be inspected, prefer a test-only `friend` access shim.
- When adding, deleting, or renaming test modules, update both `README.zh-CN.md` and `README.md`.

## Skip Rules

Individual cases can return `TestResult::skip(...)` when the current machine lacks a required environment. The current runner behavior is:

- If any case fails, the executable returns failure.
- If no cases fail and at least one case passes, the executable returns success.
- If every selected case is skipped, the executable returns `77`.

`tests/CMakeLists.txt` configures `77` as CTest `SKIP_RETURN_CODE`, so machines without Vulkan support can report an explicit skip instead of a false failure.
