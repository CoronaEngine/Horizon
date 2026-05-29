# Vulkan Backend 任务说明

## 目录范围

- 编辑前确认当前权威后端目录。
- 除非任务明确要求跨两个 tree 迁移或清理，否则不要在一个任务中混用 `src/HardwareWrapperVulkan` 和 `src/hardware_wrapper_vulkan`。

## Include 边界

- 只在确实需要暴露相关类型的内部接口中放置 Vulkan、VMA 和 Windows include。
- 不要把 Vulkan、VMA、Windows 或实现专属细节泄漏到公共 API header。

## 验证

```powershell
.\tools\dev.ps1 build Horizon
```
