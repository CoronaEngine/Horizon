# Horizon Vulkan Context
<!-- AGENT_DOCS_VULKAN_ZH_CN_SHA256: 899e9d8e6571ced96f3c613fc9b4be317fa3c7206577ada1d047397c0bc145b8 -->

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

## Include Boundaries

- Internal headers exposing Vulkan types may include `<volk.h>` directly.
- Internal headers exposing VMA types may include `<vk_mem_alloc.h>` directly.
- Windows `HANDLE` should only appear in internal interfaces that truly expose it.
- `VOLK_IMPLEMENTATION` must never appear in a header.
- `VMA_IMPLEMENTATION` must appear in exactly one `.cpp`.
- Do not create a catch-all Vulkan utility header that centralizes every dependency.

## Public API Boundary

Do not leak Vulkan, Windows, VMA, or third-party implementation details into `include/` to fix internal implementation issues.
