# Deadlock Freedom Task Notes
<!-- TASK_DOCS_DEADLOCK_FREEDOM_ZH_CN_SHA256: 66f8d34580cc18d3ae64e97369bcb5c1b4abe890621fbc8aaaec5599fd5d9fe5 -->

## Goal

- Horizon must support multithreaded concurrent callers; this task establishes an auditable and repeatable deadlock-freedom guarantee for the current public API, lower-case Vulkan backend, examples, and tests.
- "Absolutely no deadlocks" cannot be claimed from one stress run alone; it requires design constraints, lock-order auditing, blocking-point auditing, runtime stress validation, and regression tests.
- Any new public API, backend object, shared state, background worker, queue / present / resource lifetime path must state its concurrency boundary and wait strategy.

## Deadlock Definition

- A deadlock is a cyclic wait between threads. Mutexes, shared mutexes, condition variables, atomic waits, future waits, joins, GPU queue idle waits, fence waits, device idle waits, and blocking OS calls all count as wait edges.
- Waiting for GPU work, window events, worker threads, conditions, futures, file / network I/O, or another high-level subsystem while holding a lock is a deadlock risk by default unless it has a clear proof and test coverage.
- Vulkan host synchronization is included in the audit: submit / present for one `VkQueue` must be serialized by `Queue`; other objects must not bypass `Queue` for queue-level Vulkan API calls.
- Long stalls, failure to exit after window close, tests that require manual process kills, and infinite shutdown / destructor waits are treated as deadlocks or livelocks until proven otherwise.

## Design Rules

- Every shared object must choose one concurrency strategy: immutable snapshots, single owner-thread serialization, atomic state, explicit mutexes, or an external contract that clearly forbids cross-thread access.
- Maintain a repository-wide lock order: low-level resource storage / diagnostics / queue internal locks must not call back into high-level executor / display / public facade code; high-level locks must not be held across blocking lower-level waits.
- Do not call user callbacks, submit GPU work, `vkQueueWaitIdle`, `vkDeviceWaitIdle`, `join()`, `wait()`, unbounded condition waits, file / network upload, window message pumps, or functions that may re-enter Horizon while holding a mutex.
- `Queue` only owns host-access serialization for one `VkQueue`, timelines, in-flight command buffers, and retirement. Scheduling policy, cross-device synchronization, and resource allocation policy must stay out of `Queue`.
- `DisplayManager` only owns surface / swapchain / present state. Present queue access must go through `Queue::present`; do not call `vkQueuePresentKHR` directly from display code.
- Shutdown must stop external worker / render threads before releasing `ResourceManager`, `DeviceManager`, and the instance. Destructors must not wait for threads that can still call back into the object being destroyed.
- Diagnostics / crash reporting / breadcrumbs must not take complex locks or perform network upload in crash handlers or hot render paths. Collection and upload must be decoupled.

## Audit Scope

- Public API and facades: `include/`, `HardwareContext`, `HardwareExecutor`, `HardwareBuffer`, `HardwareImage`, and `HardwareDisplayer`.
- Vulkan backend: `Queue`, `DeviceManager`, `ResourceManager`, `ResourcePool`, `DisplayManager`, executor / compiler / encoder under `src/hardware_wrapper_vulkan/`.
- Shared validation / diagnostics: `src/hardware_wrapper/validation`, `src/hardware_wrapper/diagnostics.*`, and the `horizon-vulkan-diagnostics.txt` write path.
- Examples and tests: multi-window, multi-render-thread, window close, swapchain resize, present skipped / out-of-date, submit failure, resource teardown, host upload / readback.
- Do not audit `src/HardwareWrapperVulkan/`, `src/HardwareWrapper/`, `third-party/`, or `modules/` by default unless the task explicitly names them or the current compiled target depends on them.

## Implementation Steps

- P0 Inventory synchronization primitives: use `rg` to list mutexes, shared mutexes, condition variables, atomic waits, futures, joins, waits, queue/device idle waits, and blocking I/O, grouped by subsystem.
- P1 Draw the lock / wait graph: record each lock owner, protected data, functions allowed while held, functions forbidden while held, and external objects it may wait on.
- P2 Fix obvious anti-patterns: waiting while locked, reversed lock ordering, destructor joins where workers can call back, `DisplayManager` bypassing `Queue` present, and high-level scheduling inside `Queue`.
- P3 Add tests: no-GPU fake queue coverage for submit / present / retirement / failure; real Vulkan smoke for multithreaded submit + present + shutdown; window close and resource release must have timeouts.
- P4 Add a stress entrypoint: fixed seed, thread count, duration, maximum wait time, and failure output with thread ids, recent breadcrumbs, last submit token, and diagnostics path.
- P5 Freeze acceptance: tests must never wait forever; every long wait has a timeout, log, and failure path; new locks or wait points must update the lock / wait graph.

## Validation Commands

Static inventory:

```powershell
rg -n "std::(mutex|shared_mutex|recursive_mutex|condition_variable)|atomic<|\\.wait\\(|\\.notify_|join\\(|vkQueueWaitIdle|vkDeviceWaitIdle|vkWaitForFences|future<|std::async|std::jthread|std::thread" include src tests examples
```

Docs and script changes:

```powershell
git diff --check
.\tools\sync-agents.ps1 -Check
```

C++ changes:

```powershell
uv run --frozen python tools/dev.py build Horizon
cmake --build --preset msvc-debug --target HorizonTests
ctest --test-dir build/ninja-msvc -C Debug -R "HorizonTests|deadlock|concurrent|queue|present|shutdown" --output-on-failure
```

Long-running stress validation must use explicit timeout wrappers; failure must not appear as an indefinitely hung test process.

## Acceptance

- A current-code lock / wait graph covers all Horizon-owned locks and major blocking points.
- Every wait-while-locked point is removed, or has a clear local proof, timeout strategy, failure log, and test coverage.
- Multithreaded submit / present / resource create / upload / readback / shutdown stress tests finish within bounded time in both Debug validation and non-validation configurations.
- Window close, swapchain resize, present skipped / out-of-date, submit failure, and device teardown paths do not wait forever.
- Diagnostics can leave the last queue token, thread ids, important breadcrumbs, and `horizon-vulkan-diagnostics.txt` path for suspected deadlocks.
- Any new lock, wait, worker thread, condition, queue idle, or device idle call must update this task's audit material or the matching focused document.

## Non-Goals

- Do not treat one passing stress run as proof of absolute deadlock freedom.
- Do not avoid deadlocks by serializing everything through one global coarse lock.
- Do not expose Vulkan / Windows / backend details in public APIs to explain internal locks.
- Do not refactor historical mirror trees in this task unless they are used by the current compiled target or explicitly requested by the user.

## Related Entrypoints

- Vulkan concurrency rules: `docs/agents/vulkan.md`.
- Vulkan backend task notes: `docs/tasks/vulkan-backend.md`.
- Runtime diagnostics and crash reporting: `docs/tasks/runtime-diagnostics-reporting.md`.
- Example visible-window and runtime smoke: `docs/tasks/examples-new-api-visible-window.md`.
