# Horizon Vulkan 上下文

仅在处理 Vulkan 后端、resource manager、pipeline、queue、descriptor、barrier 或平台 include 时加载。

## 目录规则

仓库存在历史镜像目录：

- `src/hardware_wrapper_vulkan/`
- `src/HardwareWrapperVulkan/`
- `src/hardware_wrapper/`
- `src/HardwareWrapper/`

默认以当前 `src/CMakeLists.txt` 实际编译的目录为准，除非用户明确指定其他路径。

不要在一个任务里同时重构两套镜像目录，除非任务明确是迁移或删除。

## 后端风险点

谨慎处理：

- Vulkan 对象生命周期。
- VMA allocation 生命周期。
- Descriptor set 和 descriptor pool。
- Pipeline layout。
- Push constant range。
- Command buffer 提交。
- Queue family 选择。
- Image layout 和 memory barrier。
- Swapchain 和 display 逻辑。

## Include 边界

- 暴露 Vulkan 类型的内部 header 可以直接 include `<volk.h>`。
- 暴露 VMA 类型的内部 header 可以直接 include `<vk_mem_alloc.h>`。
- Windows `HANDLE` 只应出现在确实暴露该类型的内部接口里。
- `VOLK_IMPLEMENTATION` 不能出现在 header。
- `VMA_IMPLEMENTATION` 只能出现在一个 `.cpp`。
- 不要创建集中所有 Vulkan 依赖的万能工具头。

## 公共 API 边界

不要为了修内部实现，把 Vulkan、Windows、VMA 或第三方实现细节泄露到 `include/`。
