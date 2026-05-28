# Vulkan Backend Task Notes

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
