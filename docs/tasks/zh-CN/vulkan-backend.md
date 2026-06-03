# Vulkan Backend 任务说明

## 目录范围

- 编辑前确认当前权威后端目录。
- 除非任务明确要求跨两个 tree 迁移或清理，否则不要在一个任务中混用 `src/HardwareWrapperVulkan` 和 `src/hardware_wrapper_vulkan`。

## Include 边界

- 只在确实需要暴露相关类型的内部接口中放置 Vulkan、VMA 和 Windows include。
- 不要把 Vulkan、VMA、Windows 或实现专属细节泄漏到公共 API header。

## Device / Queue 验证

- 无 GPU 单测优先覆盖 fake `Queue`：partial timeline retire、command buffer pool 复用、submit 失败后所有权保留、未预先 acquire 时自动创建 tracked command buffer。
- 真实 Vulkan smoke 放在 `HorizonTests`：`HardwareContext::devices()` 应触发 `DeviceManager` 创建 `VkDevice`、记录 queue family，并暴露可用于 transfer 的 queue。
- 校对 `DeviceManager` 时优先检查 `VkDeviceQueueCreateInfo::queueCount`、extension 过滤、feature chain 交集和 queue capability fallback；这些错误通常要靠真实 Vulkan smoke 才能发现。

## 验证

```powershell
.\tools\dev.ps1 build Horizon
cmake --build --preset msvc-debug --target HorizonTests
ctest --test-dir build/ninja-msvc -C Debug -R HorizonTests --output-on-failure
```
