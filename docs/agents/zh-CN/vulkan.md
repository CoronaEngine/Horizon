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

## Executor / Queue 重构

- 新指令系统的外部手感可以是 `stream << ... << commit()`，但内部路径固定为 `Stream` facade -> `CommandRecorder` typed IR -> `ExecutionCompiler`/submission plan -> Vulkan command encoder -> `Queue` submit；不要让 recorder 或 visitor 直接执行 Vulkan 命令。
- 命令对象使用值语义或 small shared-state；不要返回可能悬挂的 raw `CommandRecordVulkan*`。需要延长生命周期时使用 `SubmissionKeepAlive`、`keep_alive(shared_ptr<T>)` 或 host callback。
- 借鉴 ocarina 指令系统时，只移植 value command、batch 和 keep-alive 的书写手感，不移植 command pool、raw `Command*`、visitor 直接执行模式；Horizon 的 command object 应是薄值对象，暴露 payload，并能擦除为 `StreamCommand` 后记录到 `CommandRecorder` typed IR。
- `CommandBatch` 可以接收这些 value command 并保持顺序；host 侧生命周期保留使用 `keep_alive(shared_ptr<T>)` 或 `keep_alive(copyable values...)`，最终都进入 `SubmissionKeepAlive`，不要让 command pool 承担 GPU 延迟销毁。
- Command IR payload 应保持明确：copy、dispatch、begin/end rendering、draw indexed、present、host callback、keep alive；每条 IR 带 `DeviceMask`、`QueueCapability`、资源访问和 feature requirements。
- Present 是 execution graph 的一类节点，不是 executor commit 后的附加步骤；swapchain `OUT_OF_DATE` / `SUBOPTIMAL` 等状态通过 `SubmitReceipt` / present result 返回，Vulkan submit 失败继续抛异常。
- `DeviceMask` v1 只表示显式目标设备和复制提交，不做自动负载均衡；资源不在目标设备且无法导入或复制时，应在 compile 阶段报错。
- lower-case Vulkan 后端的 `CommandRecorder` 只记录抽象 IR、资源引用、访问模式、队列能力、feature requirement 和 device mask；录制阶段不要创建 descriptor set、pipeline、VkCommandBuffer 或 Vulkan/VMA 资源。
- `ExecutionCompiler` 负责把 IR 编译为 per `{device, queue}` 的提交 DAG / plan，并在这里做 barrier 规划、MGPU 分区、present 展开和跨设备同步决策；descriptor/pipeline 查找、rendering info 和实际 `VkCommandBuffer` 填充属于 Vulkan encoder。
- `Queue` 只封装单个 `VkQueue` 的职责：串行化 `vkQueueSubmit2`、维护 timeline semaphore、command buffer pool、in-flight tracked buffers 和 retire；不要把调度策略、跨 GPU 同步策略或资源分配策略放进 Queue。
- `TrackedCommandBuffer` 持有 `SubmissionKeepAlive` 和资源 control block 强引用，直到 Queue 的 timeline 到达提交值；retire 时清空 keep-alive 并把 command buffer 归还到池。
- `HardwareExecutor` 编排 record/compile/submit、DAG 顺序、错误策略和 `CrossDeviceSync`；不要在 executor 内部再维护一套延迟释放队列。
- Timeline semaphore 是默认完成信号；只有后端或平台限制需要 fallback 时才使用 per-submit fence。
- `VK_KHR_deferred_host_operations` 只用于把支持该扩展的昂贵 host-side Vulkan 操作拆到线程池，不能当作 GPU 提交、资源生命周期或延迟销毁机制。
- 无 GPU 单测优先注入 fake queue / fake timeline，验证 submit、retire、keep-alive 释放、跨 queue token 依赖和失败路径；真实 Vulkan smoke 继续放在 `HorizonTests`。

## Include 边界

- 暴露 Vulkan 类型的内部 header 可以直接 include `<volk.h>`。
- 暴露 VMA 类型的内部 header 可以直接 include `<vk_mem_alloc.h>`。
- Windows `HANDLE` 只应出现在确实暴露该类型的内部接口里。
- `VOLK_IMPLEMENTATION` 不能出现在 header。
- `VMA_IMPLEMENTATION` 只能出现在一个 `.cpp`。
- 不要创建集中所有 Vulkan 依赖的万能工具头。

## 公共 API 边界

不要为了修内部实现，把 Vulkan、Windows、VMA 或第三方实现细节泄露到 `include/`。
