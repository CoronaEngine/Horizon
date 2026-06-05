# Horizon Vulkan Context
<!-- AGENT_DOCS_VULKAN_ZH_CN_SHA256: 34a2788b68a5c4f4aa221c6f5c74ecc08be81416822a0b5ddd1ea74c08aeed11 -->

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

## Vulkan Validation Layers

- Lower-case Vulkan backend validation is controlled by `HORIZON_ENABLE_VALIDATION`; do not gate it on the historical or incorrect `CORONA_ENGINE_DEBUG`. CMake debug configurations define `CABBAGE_ENGINE_DEBUG`, so using the wrong macro can silently compile validation out or leave it inactive.
- When `HardwareContext::create_instance()` enables `VK_LAYER_KHRONOS_validation`, also request `VK_EXT_debug_utils` and `VK_EXT_validation_features`; to expose as many errors as possible, the debug path should enable GPU-assisted validation, reserve binding slot, best practices, debug printf, and synchronization validation. This all-on mode is expected to be slow.
- Filter instance extensions by enumerating both global extensions and extensions exposed by requested layers. `VK_EXT_validation_features` may be exposed by `VK_LAYER_KHRONOS_validation`, so a global-only filter can incorrectly disable extended validation.
- To verify validation is actually active, run `HorizonTests.exe hardware_context.` and check the log for `Khronos Validation Layer Active` with `Current Enables` listing the required validation features.
- For Vulkan compatibility or validation-layer issues in debug validation builds, inspect `horizon-vulkan-diagnostics.txt` first; `HORIZON_VULKAN_DIAGNOSTICS_PATH` may override the path. It should report the loader/API version, validation feature status, requested/enabled/missing instance / device capabilities, selected or skipped physical devices, `VULKAN VALIDATION` / `HORIZON VALIDATION` / `VK_ERROR` records, and readable object names.
- Keep the diagnostics system internally owned by the backend; do not expose Vulkan diagnostic details through `include/`. When adding Vulkan objects or failure paths, add debug object names and failing `VkResult` call-site / resource context so the txt can identify compatibility, barrier / layout, descriptor, submit / present sync, or lifetime issues directly.

## Bindless And Descriptor Layout

- The lower-case Vulkan backend has only one fixed global bindless ABI: set 0 binding 0 is the combined image sampler runtime array, set 1 binding 0 is the storage buffer runtime array, and set 2 binding 0 is the storage image runtime array.
- UBOs and ordinary descriptors are not part of a fixed set convention; create descriptor set layouts and write descriptors from shader-reflected set/binding values. Even if a shader generator emits UBOs at set 3 in bindless mode, the backend should consume that as reflection data, not as backend ABI.
- Pipeline layouts must use exact Vulkan set indices. If there are gaps between bindless sets 0-2 and reflected descriptor sets, fill them with empty descriptor set layouts, and bind descriptor sets at their actual set indices.
- Bindless resource binding writes global descriptor array indices into push-constant handle fields; non-bindless or ordinary reflected descriptors use per-dispatch / per-draw transient descriptor sets.
- One `HardwareImage` may be used as both a sampled image and a storage image; sampled and storage bindless descriptor indices must be cached separately instead of sharing one image bindless index.
- When enabling bindless, check the full descriptor indexing feature chain: runtime descriptor arrays, partially bound descriptors, variable descriptor count, update-after-bind, and the matching sampled image / storage buffer / storage image non-uniform indexing and update-after-bind support.

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
- Use `using` aliases sparingly across the project; in resource wrapper or resource pool refactors, especially do not use aliases to hide core types. Prefer explicit types, especially for public resource handles, `ResourceStore<Resource, Releaser>`, and concurrent resource pool interfaces.
- In the lower-case Vulkan backend, native buffer creation/destruction belongs to `ResourceManager`: it owns the per-device `VmaAllocator`, derives `VkBufferUsageFlags` / VMA allocation from `HardwareBufferDesc`, and pairs cleanup in `destroy_buffer(BufferWrap&)`; `ResourcePool` only maintains `ResourceStore` slots, tokens, and `BufferReleaser` delegation.
- The `HardwareBuffer` wrapper should only connect public objects to `ResourceBridge` / `ResourceStore` tokens; do not restore the old wrapper-local `bufferID`, `globalBufferStorages`, handwritten ref-counts, or extra locks.
- Keep the buffer creation chain split in the NVRHI style: `src/hardware_wrapper/validation` handles descriptor/public API validation, `ResourceManager` handles Vulkan/VMA object creation and memory choice, and state tracking, descriptor binding validation, upload/write/copy paths stay in later usage stages.
- The `HardwareBuffer` constructor is not an upload scheduler: it may only synchronously handle initial upload data for host-visible mapped memory. Device-local / `CpuAccessMode::None` uploads go through explicit `HardwareBuffer::upload(...) -> CommandBatch`, using a staging buffer, copy command, and `keep_alive` lifetime retention; do not hide executor submit, wait, or GPU copy inside resource constructors.
- In the lower-case Vulkan backend, native image creation/destruction also belongs to `ResourceManager`: it owns VMA allocation, derives usage / format / aspect / image view state, handles external import/export and sampled descriptors, and pairs `VkImageView` / VMA allocation cleanup in `destroy_image(ImageWrap&)`; `ResourcePool` only maintains `ResourceStore` slots, tokens, and `ImageReleaser` delegation.
- The `HardwareImage` wrapper should only connect public objects and layer/mip/subresource views to `ResourceBridge` / `ResourceStore` tokens; subresource views share the same token. For host linear image I/O, omitted `row_pitch` / `slice_pitch` means tightly packed caller data, and copies must walk `vkGetImageSubresourceLayout` rowPitch / depthPitch instead of doing a raw `memcpy` over the allocation.
- Keep `HardwareImage` host byte-I/O format blocks, mip extents, row/slice pitch, and overflow math centralized in `src/hardware_wrapper/image_format_layout.h`; do not duplicate format tables or hand-written multiplication across wrapper and validation layers. `src/hardware_wrapper/validation` owns descriptor, CPU access, subresource, unsupported depth/stencil, and caller pitch/size checks; mapped-allocation copies and VMA flush/invalidate stay in the wrapper / `ResourceManager`.

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

## Swapchain / Display Lifetime

- `DisplayManager` owns the native-window Vulkan surface, swapchain, swapchain image wrappers, and present sync objects; on window close, surface lost, or out-of-date, destroy the swapchain first, then release the surface.
- Hidden, minimized, or temporary zero-client-area windows should report that present was `Skipped`; do not destroy the swapchain only because the window is temporarily non-drawable. In multi-window paths sharing one device / queue, tearing down a swapchain or waiting for full device idle at that moment can disturb other window threads' submissions.
- Before reusing a swapchain frame's image-available / render-finished binary semaphores, prove the previous submission token for that frame has completed and retire it. Do not assume frame-index rotation means the GPU has finished with those semaphores.
- Present submissions should put only GPU-used resource tokens into queue keep-alive. Do not let queue in-flight keep-alive strongly hold `DisplayManager` itself, because that can defer surface/swapchain destruction until after the native window is gone.
- Before destroying a swapchain, wait for the device or relevant queue to become idle and retire completed queue keep-alives so swapchain image wrappers / image views release before semaphores and `VkSwapchainKHR` are destroyed.
- GLFW example loops should re-check `glfwWindowShouldClose` after `glfwPollEvents()`; once a close event arrives, do not record or submit another frame containing present.

## Include Boundaries

- Internal headers exposing Vulkan types may include `<volk.h>` directly.
- Internal headers exposing VMA types may include `<vk_mem_alloc.h>` directly.
- Windows `HANDLE` should only appear in internal interfaces that truly expose it.
- `VOLK_IMPLEMENTATION` must never appear in a header.
- `VMA_IMPLEMENTATION` must appear in exactly one `.cpp`.
- Internal headers on the VOLK/VMA dynamic-loading path must define `VK_NO_PROTOTYPES` before including `<volk.h>` / `<vk_mem_alloc.h>` when no-prototype mode is needed; do not let VMA auto-declare Vulkan prototypes.
- Do not create a catch-all Vulkan utility header that centralizes every dependency.
- Lower-case implementation and tests should include the public API through the official `horizon.h` header; do not restore the transitional `horizon_refac.h` name or add a compatibility forwarding header.

## Public API Boundary

Do not leak Vulkan, Windows, VMA, or third-party implementation details into `include/` to fix internal implementation issues.
