# 无死锁保证任务说明

## 目标

- Horizon 必须支持多线程并发调用；本任务目标是在当前 public API、lower-case Vulkan backend、示例和测试路径中建立可审计、可回归的无死锁保证。
- “绝对不会死锁”不能只靠一次压力测试宣称完成；必须同时满足设计约束、锁顺序审计、阻塞点审计、运行时压力验证和回归测试。
- 任何新增 public API、backend 对象、shared state、background worker、queue / present / resource lifetime 路径，都必须说明并发边界和等待策略。

## 死锁定义

- 线程之间形成互相等待环：mutex / shared_mutex / condition_variable / atomic wait / future wait / join / GPU queue idle / fence wait / device idle / blocking OS call 都算等待边。
- 持有锁时等待 GPU、窗口事件、worker 线程、condition、future、文件 / 网络 I/O 或另一个高层 subsystem，默认视为死锁风险，除非有明确证明和测试覆盖。
- Vulkan host synchronization 也纳入审计：同一个 `VkQueue` 的 submit / present 只能由 `Queue` 串行化，其他对象不得绕过 `Queue` 调用 queue-level Vulkan API。
- 长时间卡住、窗口关闭后无法退出、测试需要人工杀进程、shutdown / destructor 无限等待，都按死锁或 livelock 处理，直到证明不是。

## 设计规则

- 每个共享对象必须选择一种并发策略：不可变快照、单 owner thread 串行化、原子状态、明确 mutex，或明确禁止跨线程访问的外部 contract。
- 建立全仓库 lock order：低层资源存储 / diagnostics / queue 内部锁不能反向调用高层 executor / display / public facade；高层锁不能在持有时进入会阻塞的低层等待。
- 禁止在持有 mutex 时调用用户回调、提交 GPU work、`vkQueueWaitIdle`、`vkDeviceWaitIdle`、`join()`、`wait()`、条件变量无限等待、文件 / 网络上传、窗口消息泵或可能重入 Horizon 的函数。
- `Queue` 只负责单个 `VkQueue` 的 host access 序列化、timeline、in-flight command buffer 和 retirement；调度策略、跨设备同步和资源分配策略不得塞进 `Queue`。
- `DisplayManager` 只管理 surface / swapchain / present 状态；present queue access 必须走 `Queue::present`，不能自己直接调用 `vkQueuePresentKHR`。
- shutdown 顺序必须先停止外部 worker / render threads，再释放 `ResourceManager`、`DeviceManager` 和 instance；析构路径不得等待仍可能回调进本对象的线程。
- diagnostics / crash reporting / breadcrumbs 不得在崩溃 handler 或渲染热路径中拿复杂锁或执行网络上传；收集和上传必须解耦。

## 审计范围

- Public API 和 facade：`include/`、`HardwareContext`、`HardwareExecutor`、`HardwareBuffer`、`HardwareImage`、`HardwareDisplayer`。
- Vulkan backend：`src/hardware_wrapper_vulkan/` 下的 `Queue`、`DeviceManager`、`ResourceManager`、`ResourcePool`、`DisplayManager`、executor / compiler / encoder。
- Shared validation / diagnostics：`src/hardware_wrapper/validation`、`src/hardware_wrapper/diagnostics.*`、`horizon-vulkan-diagnostics.txt` 写入路径。
- Examples 和 tests：多窗口、多 render thread、窗口关闭、swapchain resize、present skipped / out-of-date、submit failure、resource teardown、host upload / readback。
- 不要默认审计 `src/HardwareWrapperVulkan/`、`src/HardwareWrapper/`、`third-party/` 或 `modules/`，除非任务明确指定或当前编译目标实际依赖。

## 实施步骤

- P0 清点同步原语：用 `rg` 列出 mutex、shared_mutex、condition_variable、atomic wait、future、join、wait、queue/device idle、blocking I/O，并按 subsystem 建表。
- P1 画 lock / wait graph：记录每个锁的 owner、保护数据、允许持锁调用的函数、禁止持锁调用的函数、可能等待的外部对象。
- P2 修掉明显反模式：持锁等待、锁顺序反转、析构中 join 但 worker 可能回调、DisplayManager 绕过 Queue present、Queue 里塞高层调度。
- P3 增加测试：无 GPU fake queue 测 submit / present / retirement / failure；真实 Vulkan smoke 测多线程 submit + present + shutdown；窗口关闭和资源释放必须有超时。
- P4 增加压力入口：固定 seed、线程数、运行时长、最大等待时间、失败时输出 thread ids、recent breadcrumbs、last submit token、diagnostics path。
- P5 固化验收：测试不得无限等待；所有长等待都有超时、日志和失败路径；新增锁或等待点必须更新 lock / wait graph。

## 验证命令

静态清点：

```powershell
rg -n "std::(mutex|shared_mutex|recursive_mutex|condition_variable)|atomic<|\\.wait\\(|\\.notify_|join\\(|vkQueueWaitIdle|vkDeviceWaitIdle|vkWaitForFences|future<|std::async|std::jthread|std::thread" include src tests examples
```

文档和脚本变更：

```powershell
git diff --check
.\tools\sync-agents.ps1 -Check
```

C++ 变更：

```powershell
.\tools\dev.ps1 build Horizon
cmake --build --preset msvc-debug --target HorizonTests
ctest --test-dir build/ninja-msvc -C Debug -R "HorizonTests|deadlock|concurrent|queue|present|shutdown" --output-on-failure
```

长时间压力验证应使用明确超时包装；不能让失败表现为测试进程无限挂起。

## 验收标准

- 有一份当前代码的 lock / wait graph，覆盖所有 Horizon-owned locks 和主要阻塞点。
- 每个持锁等待点都被移除，或有明确局部证明、超时策略、失败日志和测试覆盖。
- 多线程 submit / present / resource create / upload / readback / shutdown 压力测试在 Debug validation 和非 validation 配置下都能在限定时间内完成。
- 窗口关闭、swapchain resize、present skipped / out-of-date、submit failure、device teardown 路径不会无限等待。
- diagnostics 能在疑似死锁时留下最后的 queue token、线程 id、重要 breadcrumbs 和 `horizon-vulkan-diagnostics.txt` 路径。
- 新增锁、等待、worker thread、condition、queue idle 或 device idle 调用时，必须同步更新本任务的审计资料或对应专项文档。

## 非目标

- 不把“跑过一次压力测试”当成绝对无死锁证明。
- 不为了规避死锁而把所有调用粗暴串行化到一个全局大锁。
- 不在 public API 中暴露 Vulkan / Windows / backend 细节来解释内部锁。
- 不在当前任务中重构历史 mirror tree，除非它被当前编译目标使用或用户明确要求。

## 相关入口

- Vulkan 并发规则：`docs/agents/zh-CN/vulkan.md`。
- Vulkan backend 任务说明：`docs/tasks/zh-CN/vulkan-backend.md`。
- 运行时诊断和崩溃上报：`docs/tasks/zh-CN/runtime-diagnostics-reporting.md`。
- 示例可见窗口和运行时 smoke：`docs/tasks/zh-CN/examples-new-api-visible-window.md`。
