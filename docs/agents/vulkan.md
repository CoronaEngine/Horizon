# Horizon Vulkan Context
<!-- AGENT_DOCS_VULKAN_ZH_CN_SHA256: 73774ad89e6c9e3947eeeaf003f327b185334e8fa9edf0ce2ca166b2880e7100 -->

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

## Resource Lifetime Refactor

- When refactoring public resource wrappers, prefer handle semantics: wrapper copy/move should only share `ResourceHandle` / `IResourceRef`; do not reintroduce per-wrapper `atomic<uintptr_t>` IDs, handwritten ref-counts, and locks.
- Wrappers should keep only their view or API state; underlying Vulkan/VMA objects, pool/storage slots, and destruction policies belong in internal implementation.
- Resource lifetime abstractions in `include/` must remain backend-neutral; do not expose Vulkan, VMA, Windows, or third-party types just to connect Storage, ResourcePool, or Vulkan implementation details.
- GPU deferred release should be handled by command/executor keep-alives that hold the control block until the fence/timeline completes; raw `uintptr_t` IDs are not ownership or GPU-lifetime guarantees.
- Keep resource lifetime naming concise and consistent: public handle layer uses `IResourceRef`, `ResourceHandle`, and `ResourceBridge`; resource pool layer uses `ResourceStore<Resource, Releaser>`, `Slot`, `Read`, `Write`, `Handle`, and `Token`; release policies use `*Releaser`, not `*Destroy` or `DestroyPolicy`.
- Do not use `using` aliases to hide core types in resource wrapper or resource pool refactors; this project prefers explicit types, especially for public resource handles and concurrent resource pool interfaces.

## Executor / Queue Refactor

- In the lower-case Vulkan backend, `CommandRecorder` records only abstract IR, resource references, access modes, queue capabilities, feature requirements, and device masks; recording must not create descriptor sets, pipelines, VkCommandBuffers, or Vulkan/VMA resources.
- `ExecutionCompiler` turns IR into per `{device, queue}` submission batches, and owns resource allocation, descriptor/pipeline cache lookup, barrier planning, MGPU partitioning, and actual command buffer filling.
- `Queue` should only wrap one `VkQueue`: serialize `vkQueueSubmit2`, maintain the timeline semaphore, command buffer pool, in-flight tracked buffers, and retirement; do not put scheduling policy, cross-GPU sync policy, or resource allocation policy inside Queue.
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
- Do not create a catch-all Vulkan utility header that centralizes every dependency.

## Public API Boundary

Do not leak Vulkan, Windows, VMA, or third-party implementation details into `include/` to fix internal implementation issues.
