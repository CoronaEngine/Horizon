# Vulkan Backend Task Notes
<!-- TASK_DOCS_VULKAN_BACKEND_ZH_CN_SHA256: 11522c871e1a1eb24b80f9a787f7104d660d931eeb449fc06b1ed7b1dae16e21 -->

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

## CoronaEngine Lower-Case Migration

- CoronaEngine migrates to the current public entrypoint, `#include <horizon.h>`; do not add or restore `include/Horizon.h`.
- Old names map to the current lower-case API: `ImageFormat` -> `Format`, `ImageUsage` -> `ImageUsageFlags`, `BufferUsage` -> `BufferUsageFlags`, and `HardwareImageCreateInfo` -> `HardwareImageDesc`.
- `BufferUsageFlags` and `ImageUsageFlags` are composable bitmasks; mesh buffers may combine `Vertex | Index | Storage`, and images may combine `Sampled | Storage | TransferSrc | TransferDst`.
- The bindless ABI stays fixed: set 0 is the combined sampled image array, set 1 is the storage buffer array, and set 2 is the storage image array. `HardwareImage::store_descriptor()` is the sampled path; storage images use `store_storage_descriptor()`.
- Depth sampled images use the sampled bindless array; do not add a separate public depth descriptor API.
- `HardwareExecutor::wait(receipt)` / `wait(other)` are next-commit one-shot dependencies; present enters the execution graph through the `present(displayer, image)` value command.
- Device-local image uploads use `HardwareImage::upload(...)` to produce a staging copy batch; GPU-to-CPU screenshot paths copy image-to-buffer into a host-readable buffer.
- During the Windows SDL/native migration path, pass `HWND` to `HardwareDisplayer(void*)`; cross-platform surface descriptors remain future work.

## Validation

```powershell
.\tools\dev.ps1 build Horizon
cmake --build --preset msvc-debug --target HorizonTests
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure
```
