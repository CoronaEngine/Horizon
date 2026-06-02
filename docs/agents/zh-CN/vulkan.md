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

## 并发和计算方向

- 后端设计默认面对多线程并发调用；公共 API、backend facade、resource pool、descriptor / pipeline cache 和 execution 入口都不要依赖“只有一个调用线程”的隐含前提。
- 可变共享状态必须选择清晰策略：显式锁、atomic、owner-thread 串行化或不可变快照。跨线程访问边界不清楚时，先收窄所有权，再补测试，不要靠调用顺序约定维持正确性。
- 单个 `VkQueue` 的 host access 必须在 `Queue` 内部统一串行化；这包括 `vkQueueSubmit2` 和 `vkQueuePresentKHR`。多窗口 / 多 render 线程可能共享同一个 present queue，不要让 `DisplayManager` 或示例线程绕过 `Queue` 直接调用 queue-level Vulkan API。
- 更高层的 record / compile / submit / retire 可以并发推进，但调度策略仍属于 `HardwareExecutor` / compiler，而不是塞进 `Queue`。
- compute / dispatch 是和 graphics / present 并列的一等执行路径。`QueueCapability::Compute`、storage 资源访问、无 swapchain 工作流和 future compute graph 都应通过 typed IR、device mask、queue capability 与显式资源访问表达。
- 面向未来计算框架演进时，`include/` 中的公共类型继续保持后端无关、非 graphics-only；不要为了当前渲染路径方便，把 render pass、swapchain 或 present 假设写进通用资源和执行抽象。

## Device / Queue 边界

- `HardwareContext::create_devices()` 负责枚举和过滤 physical device、创建每个 `DeviceContext`、接线 `DeviceManager` / `ResourceManager`，不要在这里累积 logical device、queue family 或 queue submit 策略。
- lower-case `HardwareContext` 可以作为全局发布入口，但仍应保持轻量和懒初始化：构造函数只准备配置，`VkInstance` / device 创建继续通过 `std::once_flag` / `std::call_once` 在访问时触发；`devices()` / `all_devices()` 返回 `shared_ptr<DeviceContext>` 快照，不要暴露内部容器引用。
- 如果模仿历史 camel-case 后端暴露全局 `HardwareContext`，不要把旧后端的 eager Vulkan 初始化搬过来；也不要在其他全局对象构造期间主动访问 GPU，以免触发静态初始化顺序问题。
- `HardwareContext` 销毁时应先停用外部 worker/render 线程，再按 `ResourceManager::shutdown()`、`DeviceManager::shutdown()`、`vkDestroyInstance` 的顺序释放；不要恢复旧后端的 `cleanUpResourceManager()` / `cleanUpDeviceManager()` 命名。
- lower-case 主设备选择读取 `DeviceManager::properties().properties.deviceType` / `deviceName`；不要使用历史后端的 `getFeaturesUtils()` 路径。独显优先级 helper 放在 `hardware_context.cpp` 本地即可。
- `ResourceManager` 依赖已经初始化的 `DeviceManager` 来取得 instance / physical device / logical device；`HardwareContext` 只负责接线初始化，不承载 buffer 分配策略。
- `DeviceManager` 负责给定 `VkInstance` / `VkPhysicalDevice` 后的 per-device 初始化：筛选 device extension、启用 feature chain、创建 `VkDevice`、记录 queue family 快照，并创建 `Queue` 包装。
- `DeviceManager` 不负责跨 GPU 同步策略、资源分配策略、execution DAG 调度或延迟释放队列；这些职责分别留给 `HardwareExecutor`、resource manager / pool、compiler / encoder 和 `Queue` 的 timeline retire。
- 同一个 Vulkan queue family 可能同时服务 graphics、compute、transfer。`QueueCapability` 查找表可以做 fallback，但不要把某个 queue 的 primary capability 当作排他的硬件能力。
- 后续如果 `DeviceManager` 的初始化参数继续增长，优先收敛为纯数据 `DeviceCreateDesc`；不要把 instance/debug layer、validation messenger 或全局上下文策略塞进 device-create payload。

## 资源生命周期重构

- 重构公共 resource wrapper 时，优先采用 handle 语义：wrapper 复制/移动只共享 `ResourceHandle` / `IResourceRef`，不要重新引入每个 wrapper 一套 `atomic<uintptr_t>` ID、手写 ref-count 和锁。
- wrapper 只保留自身视图或 API 状态；底层 Vulkan/VMA 对象、pool/storage 槽位和销毁策略留在内部实现。
- `include/` 中的资源生命周期抽象必须保持后端无关，不要为了接入 Storage、ResourcePool 或 Vulkan 实现而暴露 Vulkan、VMA、Windows 或第三方类型。
- GPU 延迟释放应由提交后的 command/executor keep-alive 持有 control block，直到 fence/timeline 完成；不要把裸 `uintptr_t` ID 当作所有权或 GPU 生命周期保证。
- 资源生命周期命名保持简洁统一：公共句柄层使用 `IResourceRef`、`ResourceHandle`、`ResourceBridge`；资源池层使用 `ResourceStore<Resource, Releaser>`、`Slot`、`Read`、`Write`、`Handle`、`Token`；释放策略使用 `*Releaser`，不要使用 `*Destroy` 或 `DestroyPolicy`。
- 资源 wrapper / resource pool 重构中不要用 `using` 别名隐藏核心类型；项目偏好显式类型，尤其是公共资源句柄和并发资源池接口。
- lower-case Vulkan 后端的 native buffer 创建/销毁由 `ResourceManager` 负责：持有 per-device `VmaAllocator`、根据 `HardwareBufferDesc` 选择 `VkBufferUsageFlags` / VMA allocation，并在 `destroy_buffer(BufferWrap&)` 成对释放；`ResourcePool` 只维护 `ResourceStore` 槽位、token 和 `BufferReleaser` 委托。
- `HardwareBuffer` wrapper 只把公共对象接到 `ResourceBridge` / `ResourceStore` token；不要恢复旧的 wrapper-local `bufferID`、`globalBufferStorages`、手写 ref-count 或额外锁。
- buffer 创建链路保持 NVRHI 风格的职责拆分：`src/hardware_wrapper/validation` 做描述符/公共 API 校验，`ResourceManager` 做 Vulkan/VMA 对象创建和内存选择，状态跟踪、descriptor 绑定校验、upload/write/copy 路径留给后续使用阶段。
- lower-case Vulkan 后端的 native image 创建/销毁同样由 `ResourceManager` 负责：管理 VMA allocation、推导 usage / format / aspect / image view、处理 external import/export 和 sampled descriptor，并在 `destroy_image(ImageWrap&)` 中成对释放 `VkImageView` 与 VMA allocation；`ResourcePool` 只维护 `ResourceStore` 槽位、token 和 `ImageReleaser` 委托。
- `HardwareImage` wrapper 只把公共对象和 layer/mip/subresource 视图接到 `ResourceBridge` / `ResourceStore` token；子资源视图共享同一个 token。host linear image I/O 中省略 `row_pitch` / `slice_pitch` 表示调用方数据紧密排列，实际拷贝必须通过 `vkGetImageSubresourceLayout` 的 rowPitch / depthPitch 分行分层处理，不要对整段 allocation 做裸 `memcpy`。

## Executor / Queue 重构

- 新指令系统的外部手感可以是 `stream << ... << commit()`，但内部路径固定为 `Stream` facade -> `CommandRecorder` typed IR -> `ExecutionCompiler`/submission plan -> Vulkan command encoder -> `Queue` submit；不要让 recorder 或 visitor 直接执行 Vulkan 命令。
- 命令对象使用值语义或 small shared-state；不要返回可能悬挂的 raw `CommandRecordVulkan*`。需要延长生命周期时使用 `SubmissionKeepAlive`、`keep_alive(shared_ptr<T>)` 或 host callback。
- 借鉴 ocarina 指令系统时，只移植 value command、batch 和 keep-alive 的书写手感，不移植 command pool、raw `Command*`、visitor 直接执行模式；Horizon 的 command object 应是薄值对象，暴露 payload，并能擦除为 `StreamCommand` 后记录到 `CommandRecorder` typed IR。
- value command 的类型名采用“操作动词 + 对象”的顺序并对齐 IR 语义，例如 `CopyBufferCommand`、`CopyBufferToImageCommand`；不要使用 `BufferCopyCommand`、`BufferToImageCommand` 这类 noun-first 或省略操作动词的 facade 名称。
- `CommandBatch` 可以接收这些 value command 并保持顺序；host 侧生命周期保留使用 `keep_alive(shared_ptr<T>)` 或 `keep_alive(copyable values...)`，最终都进入 `SubmissionKeepAlive`，不要让 command pool 承担 GPU 延迟销毁。
- Command IR payload 应保持明确：copy、dispatch、begin/end rendering、draw indexed、present、host callback、keep alive；每条 IR 带 `DeviceMask`、`QueueCapability`、资源访问和 feature requirements。
- Present 是 execution graph 的一类节点，不是 executor commit 后的附加步骤；swapchain `OUT_OF_DATE` / `SUBOPTIMAL` 等状态通过 `SubmitReceipt` / present result 返回，Vulkan submit 失败继续抛异常。
- `DeviceMask` v1 只表示显式目标设备和复制提交，不做自动负载均衡；资源不在目标设备且无法导入或复制时，应在 compile 阶段报错。
- lower-case Vulkan 后端的 `CommandRecorder` 只记录抽象 IR、资源引用、访问模式、队列能力、feature requirement 和 device mask；录制阶段不要创建 descriptor set、pipeline、VkCommandBuffer 或 Vulkan/VMA 资源。
- `ExecutionCompiler` 负责把 IR 编译为 per `{device, queue}` 的提交 DAG / plan，并在这里做 barrier 规划、MGPU 分区、present 展开和跨设备同步决策；descriptor/pipeline 查找、rendering info 和实际 `VkCommandBuffer` 填充属于 Vulkan encoder。
- `Queue` 只封装单个 `VkQueue` 的职责：串行化 submit / present 等 queue-level host access、维护 timeline semaphore、command buffer pool、in-flight tracked buffers 和 retire；不要把调度策略、跨 GPU 同步策略或资源分配策略放进 Queue。
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
- 同时接入 VOLK/VMA 动态加载路径的内部 header，如果需要无原型模式，必须在 include `<volk.h>` / `<vk_mem_alloc.h>` 之前定义 `VK_NO_PROTOTYPES`；不要让 VMA 自动声明 Vulkan 原型。
- 不要创建集中所有 Vulkan 依赖的万能工具头。

## 公共 API 边界

不要为了修内部实现，把 Vulkan、Windows、VMA 或第三方实现细节泄露到 `include/`。
