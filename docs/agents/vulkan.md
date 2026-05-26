# Horizon Vulkan Context
<!-- AGENT_DOCS_VULKAN_ZH_CN_SHA256: f39c632b023eaec4e4cd420d8188865bfcb82ffcf6203b606561f183c6e1772c -->

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

## Include Boundaries

- Internal headers exposing Vulkan types may include `<volk.h>` directly.
- Internal headers exposing VMA types may include `<vk_mem_alloc.h>` directly.
- Windows `HANDLE` should only appear in internal interfaces that truly expose it.
- `VOLK_IMPLEMENTATION` must never appear in a header.
- `VMA_IMPLEMENTATION` must appear in exactly one `.cpp`.
- Do not create a catch-all Vulkan utility header that centralizes every dependency.

## Public API Boundary

Do not leak Vulkan, Windows, VMA, or third-party implementation details into `include/` to fix internal implementation issues.
