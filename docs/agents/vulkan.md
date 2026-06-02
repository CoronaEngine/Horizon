# Horizon Vulkan Context
<!-- AGENT_DOCS_VULKAN_ZH_CN_SHA256: cf56aaa345fa0d0bd027c28edcd936524e0ca54e5ccd0da24f132874b7346541 -->

Load this file only for Vulkan backend, resource manager, pipeline, queue, descriptor, barrier, or platform include work.

## Directory Rules

This repo contains historical mirrored directories:

- `src/hardware_wrapper_vulkan/`
- `src/HardwareWrapperVulkan/`
- `src/hardware_wrapper/`
- `src/HardwareWrapper/`

Default to the tree currently compiled by `src/CMakeLists.txt` unless the user explicitly names another path.

Do not refactor both mirrored trees in one task unless the task is explicitly a migration/deletion.

## Backend Risk Areas

Be careful with:

- Vulkan object lifetime.
- VMA allocation lifetime.
- Descriptor sets and descriptor pools.
- Pipeline layouts.
- Push constant ranges.
- Command buffer submission.
- Queue family selection.
- Image layouts and memory barriers.
- Swapchain and display logic.

## Concurrency and Compute Direction

- Backend design assumes multithreaded callers. Public APIs, backend facades, resource pools, descriptor / pipeline caches, and execution entrypoints must not rely on a hidden "single caller thread" premise.
- Mutable shared state needs a clear strategy: explicit locks, atomics, owner-thread serialization, or immutable snapshots. If a cross-thread access boundary is unclear, narrow ownership first and add tests instead of relying on call-order convention.
- All host access to a single `VkQueue` must be serialized inside `Queue`; this includes both `vkQueueSubmit2` and `vkQueuePresentKHR`. Multi-window / multi-render-thread paths may share one present queue, so `DisplayManager` and examples must not bypass `Queue` for queue-level Vulkan API calls.
- Higher-level record / compile / submit / retire work may progress concurrently, but scheduling policy stays in `HardwareExecutor` / compiler, not in `Queue`.
- Compute / dispatch is a first-class execution path alongside graphics / present. Express `QueueCapability::Compute`, storage resource access, no-swapchain workflows, and future compute graphs through typed IR, device masks, queue capability, and explicit resource access.
- As the project evolves toward a compute framework, public `include/` types should stay backend-neutral and not graphics-only; do not bake render pass, swapchain, or present assumptions into generic resource and execution abstractions.

## Device / Queue Boundaries

- `HardwareContext::create_devices()` enumerates and filters physical devices, creates each `DeviceContext`, and wires `DeviceManager` / `ResourceManager`; do not accumulate logical-device, queue-family, or queue-submit policy there.
- The lower-case `HardwareContext` may be exposed as a global publishing entry, but it should remain lightweight and lazy: the constructor only prepares configuration, while `VkInstance` / device creation stays behind `std::once_flag` / `std::call_once` and is triggered by accessors; `devices()` / `all_devices()` return `shared_ptr<DeviceContext>` snapshots instead of exposing the internal container by reference.
- If mirroring the historical camel-case backend's global `HardwareContext`, do not copy its eager Vulkan initialization; also avoid touching GPU state from other global object constructors, where static initialization order can bite.
- During `HardwareContext` shutdown, stop external worker/render threads first, then release in `ResourceManager::shutdown()`, `DeviceManager::shutdown()`, `vkDestroyInstance` order; do not restore the old `cleanUpResourceManager()` / `cleanUpDeviceManager()` names.
- Lower-case main-device selection reads `DeviceManager::properties().properties.deviceType` / `deviceName`; do not use the historical backend's `getFeaturesUtils()` path. Keep a discrete-GPU-first priority helper local to `hardware_context.cpp`.
- `ResourceManager` depends on an initialized `DeviceManager` for instance / physical device / logical device access; `HardwareContext` only wires initialization and must not own buffer allocation policy.
- `DeviceManager` handles per-device initialization after receiving a `VkInstance` / `VkPhysicalDevice`: filter device extensions, enable the feature chain, create `VkDevice`, snapshot queue families, and create `Queue` wrappers.
- `DeviceManager` does not own cross-GPU sync policy, resource allocation policy, execution DAG scheduling, or delayed-release queues; keep those in `HardwareExecutor`, resource manager / pool, compiler / encoder, and `Queue` timeline retirement respectively.
- One Vulkan queue family can serve graphics, compute, and transfer. `QueueCapability` lookup tables may provide fallback, but do not treat a queue's primary capability as exclusive hardware capability.
- If `DeviceManager` initialization inputs keep growing, prefer a plain-data `DeviceCreateDesc`; do not put instance/debug layers, validation messenger setup, or global context policy into the device-create payload.

## Resource Lifetime Refactor

- When refactoring public resource wrappers, prefer handle semantics: wrapper copy/move should only share `ResourceHandle` / `IResourceRef`; do not reintroduce per-wrapper `atomic<uintptr_t>` IDs, handwritten ref-counts, and locks.
- Wrappers should keep only their view or API state; underlying Vulkan/VMA objects, pool/storage slots, and destruction policies belong in internal implementation.
- Resource lifetime abstractions in `include/` must remain backend-neutral; do not expose Vulkan, VMA, Windows, or third-party types just to connect Storage, ResourcePool, or Vulkan implementation details.
- GPU deferred release should be handled by command/executor keep-alives that hold the control block until the fence/timeline completes; raw `uintptr_t` IDs are not ownership or GPU-lifetime guarantees.
- Keep resource lifetime naming concise and consistent: public handle layer uses `IResourceRef`, `ResourceHandle`, and `ResourceBridge`; resource pool layer uses `ResourceStore<Resource, Releaser>`, `Slot`, `Read`, `Write`, `Handle`, and `Token`; release policies use `*Releaser`, not `*Destroy` or `DestroyPolicy`.
- Do not use `using` aliases to hide core types in resource wrapper or resource pool refactors; this project prefers explicit types, especially for public resource handles and concurrent resource pool interfaces.
- In the lower-case Vulkan backend, native buffer creation/destruction belongs to `ResourceManager`: it owns the per-device `VmaAllocator`, derives `VkBufferUsageFlags` / VMA allocation from `HardwareBufferDesc`, and pairs cleanup in `destroy_buffer(BufferWrap&)`; `ResourcePool` only maintains `ResourceStore` slots, tokens, and `BufferReleaser` delegation.
- The `HardwareBuffer` wrapper should only connect public objects to `ResourceBridge` / `ResourceStore` tokens; do not restore the old wrapper-local `bufferID`, `globalBufferStorages`, handwritten ref-counts, or extra locks.
- Keep the buffer creation chain split in the NVRHI style: `src/hardware_wrapper/validation` handles descriptor/public API validation, `ResourceManager` handles Vulkan/VMA object creation and memory choice, and state tracking, descriptor binding validation, upload/write/copy paths stay in later usage stages.
- In the lower-case Vulkan backend, native image creation/destruction also belongs to `ResourceManager`: it owns VMA allocation, derives usage / format / aspect / image view state, handles external import/export and sampled descriptors, and pairs `VkImageView` / VMA allocation cleanup in `destroy_image(ImageWrap&)`; `ResourcePool` only maintains `ResourceStore` slots, tokens, and `ImageReleaser` delegation.
- The `HardwareImage` wrapper should only connect public objects and layer/mip/subresource views to `ResourceBridge` / `ResourceStore` tokens; subresource views share the same token. For host linear image I/O, omitted `row_pitch` / `slice_pitch` means tightly packed caller data, and copies must walk `vkGetImageSubresourceLayout` rowPitch / depthPitch instead of doing a raw `memcpy` over the allocation.

## Executor / Queue Refactor

- The public feel of the new command system may be `stream << ... << commit()`, but the internal path is fixed: `Stream` facade -> `CommandRecorder` typed IR -> `ExecutionCompiler` / submission plan -> Vulkan command encoder -> `Queue` submit; do not let recorder or visitor code execute Vulkan commands directly.
- Command objects should be value types or small shared-state objects; do not return raw `CommandRecordVulkan*` values that can dangle. Use `SubmissionKeepAlive`, `keep_alive(shared_ptr<T>)`, or host callbacks when lifetime must extend through a submission.
- When borrowing ocarina's command system, port the value-command, batch, and keep-alive ergonomics, not command pools, raw `Command*`, or visitor-driven direct execution; Horizon command objects should be thin value facades that expose payload and erase to `StreamCommand` before recording into `CommandRecorder` typed IR.
- Name value command types as verb + object and align them with IR semantics, such as `CopyBufferCommand` and `CopyBufferToImageCommand`; avoid noun-first or verb-omitted facade names such as `BufferCopyCommand` or `BufferToImageCommand`.
- `CommandBatch` may accept these value commands while preserving order; host-side lifetime retention should use `keep_alive(shared_ptr<T>)` or `keep_alive(copyable values...)`, both ending in `SubmissionKeepAlive`, and command pools must not own GPU delayed destruction.
- Command IR payloads should stay explicit: copy, dispatch, begin/end rendering, draw indexed, present, host callback, and keep alive; each IR entry carries `DeviceMask`, `QueueCapability`, resource access, and feature requirements.
- Present is an execution graph node, not an after-commit side step; swapchain `OUT_OF_DATE` / `SUBOPTIMAL` and related states return through `SubmitReceipt` / present results, while Vulkan submit failures still throw.
- `DeviceMask` v1 only means explicit target devices and replicated submissions, not automatic load balancing; if a resource is not on the target device and cannot be imported or copied, fail during compile.
- In the lower-case Vulkan backend, `CommandRecorder` records only abstract IR, resource references, access modes, queue capabilities, feature requirements, and device masks; recording must not create descriptor sets, pipelines, VkCommandBuffers, or Vulkan/VMA resources.
- `ExecutionCompiler` turns IR into a per `{device, queue}` submission DAG / plan, and owns barrier planning, MGPU partitioning, present expansion, and cross-device sync decisions; descriptor/pipeline lookup, rendering info, and actual `VkCommandBuffer` filling belong to the Vulkan encoder.
- `Queue` should only wrap one `VkQueue`: serialize queue-level host access such as submit / present, maintain the timeline semaphore, command buffer pool, in-flight tracked buffers, and retirement; do not put scheduling policy, cross-GPU sync policy, or resource allocation policy inside Queue.
- `TrackedCommandBuffer` holds `SubmissionKeepAlive` and strong references to resource control blocks until the Queue timeline reaches the submitted value; retirement clears keep-alives and returns the command buffer to the pool.
- `HardwareExecutor` orchestrates record/compile/submit, DAG order, error policy, and `CrossDeviceSync`; do not maintain another delayed-release queue inside the executor.
- Timeline semaphores are the default completion signal; use per-submit fences only when a backend or platform limitation needs a fallback.
- `VK_KHR_deferred_host_operations` is only for splitting supported expensive host-side Vulkan operations across worker threads; it is not a GPU submission, resource lifetime, or delayed-destruction mechanism.
- Prefer fake queue / fake timeline injection for no-GPU tests of submit, retirement, keep-alive release, cross-queue token dependencies, and failure paths; keep real Vulkan smoke tests in `HorizonTests`.

## Include Boundaries

- Internal headers exposing Vulkan types may include `<volk.h>` directly.
- Internal headers exposing VMA types may include `<vk_mem_alloc.h>` directly.
- Windows `HANDLE` should only appear in internal interfaces that truly expose it.
- `VOLK_IMPLEMENTATION` must never appear in a header.
- `VMA_IMPLEMENTATION` must appear in exactly one `.cpp`.
- Internal headers on the VOLK/VMA dynamic-loading path must define `VK_NO_PROTOTYPES` before including `<volk.h>` / `<vk_mem_alloc.h>` when no-prototype mode is needed; do not let VMA auto-declare Vulkan prototypes.
- Do not create a catch-all Vulkan utility header that centralizes every dependency.

## Public API Boundary

Do not leak Vulkan, Windows, VMA, or third-party implementation details into `include/` to fix internal implementation issues.
