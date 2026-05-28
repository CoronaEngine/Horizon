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

## 资源生命周期重构

- 重构公共 resource wrapper 时，优先采用 handle 语义：wrapper 复制/移动只共享 `ResourceHandle` / `IResourceRef`，不要重新引入每个 wrapper 一套 `atomic<uintptr_t>` ID、手写 ref-count 和锁。
- wrapper 只保留自身视图或 API 状态；底层 Vulkan/VMA 对象、pool/storage 槽位和销毁策略留在内部实现。
- `include/` 中的资源生命周期抽象必须保持后端无关，不要为了接入 Storage、ResourcePool 或 Vulkan 实现而暴露 Vulkan、VMA、Windows 或第三方类型。
- GPU 延迟释放应由提交后的 command/executor keep-alive 持有 control block，直到 fence/timeline 完成；不要把裸 `uintptr_t` ID 当作所有权或 GPU 生命周期保证。
- 资源生命周期命名保持简洁统一：公共句柄层使用 `IResourceRef`、`ResourceHandle`、`ResourceBridge`；资源池层使用 `ResourceStore<Resource, Releaser>`、`Slot`、`Read`、`Write`、`Handle`、`Token`；释放策略使用 `*Releaser`，不要使用 `*Destroy` 或 `DestroyPolicy`。
- 资源 wrapper / resource pool 重构中不要用 `using` 别名隐藏核心类型；项目偏好显式类型，尤其是公共资源句柄和并发资源池接口。

## Include 边界

- 暴露 Vulkan 类型的内部 header 可以直接 include `<volk.h>`。
- 暴露 VMA 类型的内部 header 可以直接 include `<vk_mem_alloc.h>`。
- Windows `HANDLE` 只应出现在确实暴露该类型的内部接口里。
- `VOLK_IMPLEMENTATION` 不能出现在 header。
- `VMA_IMPLEMENTATION` 只能出现在一个 `.cpp`。
- 不要创建集中所有 Vulkan 依赖的万能工具头。

## 公共 API 边界

不要为了修内部实现，把 Vulkan、Windows、VMA 或第三方实现细节泄露到 `include/`。
