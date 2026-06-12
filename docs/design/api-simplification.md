# Horizon 公共 API 复杂度收口设计

> 状态:设计提案(未实现)
> 范围:`include/horizon.h` 公共表面 + `examples/` 使用姿势
> 目标:在**不损失底层声明式执行模型能力**的前提下,把被无意暴露给用户的实现细节屏蔽掉,降低上手心智负担。

---

## 0. 背景:新版重写引入的复杂度

新版(`hardware_wrapper*`,`Corona::Horizon`)相对老版(`HardwareWrapper*`,全局命名空间)做了"推倒重写",核心是把执行模型从**命令式**改成**声明式三段:`record → compile → submit`**:

```
record(fn) → RecordedTask(vector<CommandIR>)
           → ExecutionCompiler::compile() → ExecutionPlan(多 submission 依赖图)
           → VulkanCommandEncoder::encode() → vkCmd*
           → Queue::submit() → SubmissionToken
```

这个模型本身是新版的核心价值(编译器式自动屏障/timeline 同步)。问题不在模型,而在 **`horizon.h` 把模型的全部内部齿轮都摊在了公共头里**——用户从不构造 `CommandIR`/`ExecutionPlan`/`SubmissionToken`/`QueueId`,却必须在公共头里看到它们。

---

## 1. 三类复杂度的判定

收口的前提是分清哪些该砍、哪些该包、哪些必须留。判定依据是**真实用户代码**(`examples/example_default.cpp`)实际碰了什么。

| 概念 | 类别 | 判据 |
|---|---|---|
| `record / compile / submit` 三段式 | 🟢 本质 | 编译器式屏障分析的根基;但**不必三个都 public** |
| `CommandIR` / `CommandPayload` / `CommandOp` | 🔴 泄漏 | 用户从不构造,纯内部 IR |
| `ExecutionPlan` / `CompiledSubmission` / `ExecutionCompiler` | 🔴 泄漏 | 编译产物,用户从不直接持有 |
| `SubmissionToken` / `SubmissionSync` / `QueueId` / `QueueCapability` | 🔴 泄漏 | 同步内部表示,公共头暴露但用户不碰 |
| `QueueResolver` lambda | 🔴 泄漏 | 示例手写的 resolver 与默认行为**逐字相同**(见 §2) |
| `DeviceMask devices = {}`(每命令尾参) | 🔴 泄漏 | 单设备永远默认值;多设备又未实现 |
| `stream() << ....command_batch() << commit()` | 🟡 附带 | 流式好,但 `.command_batch()` 是仪式 |
| `RecordedTask` / `StreamCommand` / `CommandBatch` | 🟡 附带 | 中间类型,可隐藏在便捷重载后 |
| `store_storage_descriptor()` 手动描述符 ID | 🟡 附带 | GLSL 路径要手动管;EDSL 已自动 |
| `SubmitReceipt` + `wait(receipt)` | 🟢 本质 | render→display 跨线程握手真需要;但可更友好 |
| `RayTracingPipeline` / mesh 管线 | 🔴 泄漏 | API 暴露但后端是空壳 stub |

**结论**:用户真正需要直接接触的类型不到公共头暴露量的 1/4。

---

## 2. 关键证据:QueueResolver 是纯噪音

`example_default.cpp` 为每个 executor 手写了 12 行队列解析 lambda:

```cpp
auto fixedGraphicsQueueResolver = [](H::DeviceId device, H::QueueCapability) -> H::Queue& {
    if (device.value != 0)
        throw std::logic_error("... supports only the main device.");
    const std::vector<H::Queue*>& queues = H::device_manager().queues_for(H::QueueCapability::Graphics);
    const auto found = std::find_if(queues.begin(), queues.end(),
        [](const H::Queue* q) { return q != nullptr; });
    if (found == queues.end())
        throw std::runtime_error("... could not resolve a graphics queue.");
    return **found;
};
renderExecutors.push_back(std::make_unique<H::HardwareExecutor>(fixedGraphicsQueueResolver));
```

而默认构造的 executor 在 `src/hardware_wrapper_vulkan/hardware/execution.cpp:1581-1598` 已经做了**等价**的事:

```cpp
const auto resolve_queue = [this](DeviceId device, QueueCapability capability) -> Queue& {
    if (queue_resolver_) return queue_resolver_(device, capability);
    if (device.value != 0) throw std::logic_error("... resolves only the main device.");
    Queue* queue = main_device_context().device_manager.queue_for(capability);
    if (queue == nullptr) throw std::runtime_error("...");
    return *queue;
};
```

即:**示例手写的 resolver 完全可以删掉,改为默认构造**。这是最廉价、最该先做的收口。`QueueResolver` 构造入口保留(供未来真正的多队列负载控制),但绝不该是示例的默认姿势。

---

## 3. 用户真正需要的最小心智模型

从真实代码提炼,用户只做 5 件事:

1. **建资源**:`HardwareBuffer::vertex(data)` / `HardwareImage(desc)`
2. **建管线**:`ComputePipeline(...)` / `RasterizerPipeline(...)`(EDSL 或 SPIR-V)
3. **绑定 + 录制**:`rasterizer[key] = value` / `rasterizer.record(idx, vtx)`
4. **提交**:`executor << pipeline << commit`
5. **跨线程握手**:render 线程产出 → display 线程等待后 present

IR / Plan / Token / QueueResolver / DeviceMask 都不在这 5 件事里。

---

## 4. 方案 A:消除泄漏复杂度(零功能损失)

### A1. 示例去掉 QueueResolver,默认构造

```cpp
// 之前(泄漏):12 行 lambda
auto fixedGraphicsQueueResolver = [](H::DeviceId, H::QueueCapability) -> H::Queue& { ... };
renderExecutors.push_back(std::make_unique<H::HardwareExecutor>(fixedGraphicsQueueResolver));

// 之后:默认构造即可,行为等价
renderExecutors.push_back(std::make_unique<H::HardwareExecutor>());
```

- 改动面:仅 `examples/`,不动公共头。
- 风险:极低。
- 收益:立刻消除最刺眼的泄漏,且为后续示例树立"默认即正确"的姿势。

### A2. 内部 IR / Token 类型移出公共头

把用户从不构造的类型从 `horizon.h` 移到内部头(`src/` 下)或前向声明 + PImpl:

- 完全内部化:`CommandIR`、`CommandPayload`、`CommandOp`、`CompiledSubmission`、`ExecutionPlan`、`ExecutionCompiler`、`SubmissionToken`、`SubmissionSync`、`QueueId`、`QueueCapability`、`SubmissionDependency`、`CrossDeviceDependency`。
- 公共头仅保留用户真正调用其成员的类型:`SubmitReceipt`(只暴露 `wait`/`empty`/`presents` 等)、`HardwareExecutor`、`HardwareStream`、`HardwareDisplayer`、资源/管线/`*Desc`。

实现手段:`HardwareExecutor` 的 `compile()`/`submit()`/`ExecutionPlan` 重载是**高级入口**,可拆到独立的 `horizon_advanced.h`,主头只留 `commit`/`stream`/`record`/`wait`。

- 改动面:公共头瘦身约 40%(目前 2280 行)。
- 风险:中。需要把现暴露成员改为 PImpl 或移头,可能影响测试中直接构造 IR 的代码(`close_for_tests` 等)。

### A3. DeviceMask 参数下沉

单设备阶段,把每个 `CommandRecorder` 方法和命令工厂尾巴上的 `DeviceMask devices = {}` 从默认重载里拿掉,改为一个显式高级入口:

```cpp
// 之前:每个签名都拖一个用户永远不填的尾参
void dispatch(ShaderRef, DispatchDesc, DeviceMask devices = {});

// 之后:默认签名干净;多设备走显式入口
void dispatch(ShaderRef, DispatchDesc);
ScopedDeviceMask on_devices(DeviceMask);   // RAII,作用域内的命令带上 mask
```

- 风险:低。多设备本就未实现,现在拿掉等于把"未兑现的承诺"从签名里收起。
- 收益:所有命令签名视觉清爽。

---

## 5. 方案 B:便捷门面(在 A 之上)

把三段式与 `.command_batch()` 仪式包进直观重载——**底层仍是声明式 IR,只是给回老版的人机工学**。

### 现状
```cpp
latestRenderReceipts[i] = renderExecutors[i]->stream()
    << rasterizer(1920,1080).command_batch()
    << computer(1920/8,1080/8,1).command_batch()
    << H::commit();
```

### 门面后
```cpp
latestRenderReceipts[i] = (*renderExecutors[i])
    << rasterizer(1920, 1080)
    << computer(1920/8, 1080/8, 1)
    << H::submit;            // 隐式 commit
```

实现要点:

1. 给 `HardwareStream::operator<<` 增加 `ComputePipeline&` / `RasterizerPipeline&` 重载,内部调用 `command_batch()`。或让 pipeline 提供 `operator StreamCommand()` 隐式转换。
2. `HardwareExecutor::operator<<` 直接返回一个 `HardwareStream`,免去显式 `.stream()`。
3. 保留显式 `.command_batch()` / `.stream()`(高级/测试用),只是不再是默认姿势。

- 改动面:公共头新增重载,不破坏现有写法。
- 收益:用户提交代码减半,且读起来与老版一样直观。

---

## 6. 方案 C:跨执行器握手语义化

`SubmitReceipt` + `wait` 的 render→display 跨线程握手是本质功能,但 `receipt` 作为中间对象需要用户理解。语义化封装:

### 现状
```cpp
displayExecutors[i].wait(latestRenderReceipts[i]);
displayExecutors[i].stream()
    << H::present(displayManager, finalOutputImages[i])
    << H::commit();
```

### 语义化后
```cpp
displayExecutors[i]
    .after(latestRenderReceipts[i])          // 或 .after(renderExecutors[i])
    .present(displayManager, finalOutputImages[i]);
```

实现要点:

- `HardwareExecutor::after(const SubmitReceipt&)` 返回一个携带等待依赖的轻量代理。
- 代理提供 `.present(displayer, image)`,内部完成 `stream() << present << commit()`。
- 仍可用 `last_receipt()` 让 display 线程拿到自己的 receipt,无需暴露 token。

- 改动面:公共头新增 `after()` + 代理类。
- 收益:并发握手模型自解释,降低"receipt 是什么"的认知成本。

---

## 7. 屏蔽前后对比(综合 A+B+C)

```cpp
// ========== 现状 ==========
auto resolver = [](H::DeviceId d, H::QueueCapability) -> H::Queue& { /* 12 行 */ };
auto exec = std::make_unique<H::HardwareExecutor>(resolver);
...
receipt = exec->stream()
    << rasterizer(1920,1080).command_batch()
    << computer(240,135,1).command_batch()
    << H::commit();
...
display.wait(receipt);
display.stream() << H::present(disp, img) << H::commit();

// ========== A+B+C 之后 ==========
auto exec = std::make_unique<H::HardwareExecutor>();          // A1
...
receipt = (*exec)                                             // B
    << rasterizer(1920, 1080)
    << computer(240, 135, 1)
    << H::submit;
...
display.after(receipt).present(disp, img);                   // C
```

公共头同时移除了 `CommandIR`/`ExecutionPlan`/`SubmissionToken`/`QueueId` 等用户不碰的类型(A2),命令签名去掉 `DeviceMask` 尾参(A3)。

---

## 8. 落地优先级与风险

| 优先级 | 动作 | 改动面 | 风险 | 收益 |
|---|---|---|---|---|
| **P0** | A1 示例去 QueueResolver | examples | 极低 | 消除最刺眼泄漏 |
| **P0** | A2 IR/Token 移出公共头 | 公共头 + PImpl | 中 | 公共头瘦身 ~40% |
| **P1** | B pipeline 直接流入 + 隐式 commit | 公共头新增重载 | 低 | 用户代码减半 |
| **P1** | A3 DeviceMask 参数下沉 | 公共头签名 | 低 | 签名清爽 |
| **P2** | C 跨执行器握手语义化 | 公共头新增 API | 低 | 并发模型自解释 |

---

## 9. 不做什么(明确边界)

- **不改底层执行模型**:`record→compile→submit`、`ExecutionCompiler` 的冒险分析、timeline 同步全部保留。本设计只动**公共表面**。
- **不删高级入口**:`QueueResolver` 构造、显式 `.compile()/.submit()/.command_batch()`、`DeviceMask` 高级路径全部以"高级 API"形式保留,只是不再是默认姿势。
- **不在本轮处理**:RayTracing/mesh 空壳 stub 的 API 暴露问题(属于"API 超前实现",应单独决策是实现还是暂时收起)。

---

## 10. 待决策项

1. A2 的内部化手段:**移到内部头** vs **PImpl**?前者简单但要确认无外部直接依赖(测试 `close_for_tests`);后者彻底但有一次性改造成本。
2. B 的隐式 commit 触发物:`H::submit` 标记 vs `executor.run(...)` 可变参函数?二选一统一风格。
3. C 的 `after()` 接受 `SubmitReceipt` 还是直接接受生产者 `HardwareExecutor&`?后者更省一个中间对象,但耦合更紧。
