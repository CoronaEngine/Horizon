# Vulkan Backend Task Notes
<!-- TASK_DOCS_VULKAN_BACKEND_ZH_CN_SHA256: 79de6557ce7132ad72d279f06612655e86c244611c6cd5f365d45ce031adc33f -->

## Directory Scope

- Confirm the current authoritative backend directory before editing.
- Do not mix `src/HardwareWrapperVulkan` and `src/hardware_wrapper_vulkan` in one task unless the task explicitly asks for migration or cleanup across both trees.

## Include Boundaries

- Put Vulkan, VMA, and Windows includes only in internal interfaces that need to expose those related types.
- Do not leak Vulkan, VMA, Windows, or implementation-only details into public API headers.

## Device / Queue Validation

- Prefer no-GPU tests for fake `Queue`: partial timeline retirement, command buffer pool reuse, ownership retention after submit failure, and auto-created tracked command buffers when callers did not acquire first.
- Keep real Vulkan smoke coverage in `HorizonTests`: `HardwareContext::devices()` should trigger `DeviceManager` to create `VkDevice`, record queue families, and expose a queue usable for transfer work.
- When reviewing `DeviceManager`, first check `VkDeviceQueueCreateInfo::queueCount`, extension filtering, feature-chain intersection, and queue capability fallback; these issues often need real Vulkan smoke to catch.

## Validation

```powershell
.\tools\dev.ps1 build Horizon
cmake --build --preset msvc-debug --target HorizonTests
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure
```
