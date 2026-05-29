# Vulkan Backend Task Notes
<!-- TASK_DOCS_VULKAN_BACKEND_ZH_CN_SHA256: 29dde2111de0a73bf411c6214d9cb63952249dc484f428b0951acdf6f90ddd54 -->

## Directory Scope

- Confirm the current authoritative backend directory before editing.
- Do not mix `src/HardwareWrapperVulkan` and `src/hardware_wrapper_vulkan` in one task unless the task explicitly asks for migration or cleanup across both trees.

## Include Boundaries

- Put Vulkan, VMA, and Windows includes only in internal interfaces that need to expose those related types.
- Do not leak Vulkan, VMA, Windows, or implementation-only details into public API headers.

## Validation

```powershell
.\tools\dev.ps1 build Horizon
```
