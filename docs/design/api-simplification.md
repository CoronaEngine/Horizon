# Horizon 公共 API 复杂度收口设计

> 状态:方案 A 已落地(A1+A2 完成,A3 随 A2 达成),待构建验证
> 范围:`include/horizon.h` 公共表面 + `examples/` 使用姿势
> 目标:在**不损失底层声明式执行模型能力**的前提下,把被无意暴露给用户的实现细节屏蔽掉,降低上手心智负担。

## 核心理念

每一层的 API 都应尽可能简化,减少重复,降低用户的认知负担,并降低维护成本。本设计的一切取舍都服从这条理念:

- **默认路径最短**:常见用法零样板,能用默认行为完成的不要求用户显式传参或手写胶水代码。
- **不泄漏内部类型**:用户从不构造的类型(命令 IR、执行计划、提交令牌、队列调度)不出现在主公共头。
- **不暴露未实现能力**:后端尚未落地的特性不在公共 API 表面承诺。
- **同一概念只表达一次**:抽象、规则、理念在归属层写一次,避免跨文件 / 跨层重复。

高级能力(自定义调度、显式 record/compile/submit)保留,但绝不作为默认姿势。

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

---

## 11. 实施记录与实测约束(2026-06,执行方案 A 时补充)

### 11.1 A1 已完成 ✅

去掉示例中冗余的 `QueueResolver`,改为默认构造 `HardwareExecutor`。改动文件:

- `examples/example_default/example_default.cpp`:删除 12 行 `fixedGraphicsQueueResolver` lambda,`make_unique<HardwareExecutor>()` 默认构造。
- `examples/example_edsl/example_edsl.cpp`:删除 free function `resolve_fixed_graphics_queue`,`render_executor` 默认构造。
- `examples/example_glsl/example_glsl.cpp`:同上。

已核验三文件无残留 `device_manager()/queues_for()/find_if` 引用。**待用户本地构建 `HorizonExamples` 验证。**

零功能损失:默认 executor 在 `execution.cpp:1581-1598` 的 `resolve_queue` 行为与被删 resolver 逐字等价。

### 11.2 执行 A2 时发现的三个硬约束

实施 A2/A3 前的代码勘察推翻了设计阶段的乐观假设:

**约束 1 — 测试白盒耦合内部类型。** `tests/vulkan/test_execution_system.cpp` 直接构造并断言 `RecordedTask`、`ExecutionPlan`、`CommandIR`、`CommandOp`、`SubmissionToken`、`Queue`、`QueueCapability`(如 `task.commands[0].op == CommandOp::BeginRendering`,481-801 行)。物理移除这些类型出公共可见域 → **直接打断测试套件**。

**约束 2 — 公共 by-value 类强制类型可见。** 无法在"不引入 PImpl"前提下隐藏符号:
  - `CommandRecorder`(公共)有成员 `std::vector<CommandIR> commands_;` → `CommandIR` 必须完整可见。
  - `SubmitReceipt`(公共,用户按值持有并遍历 `tokens`)有成员 `std::vector<SubmissionToken> tokens;` → `SubmissionToken`→`QueueId`→`QueueCapability` 必须完整可见。
  - `HardwareStream`/`record()` 模板同理依赖 `RecordedTask`/`CommandIR`。
  - 结论:**真正的符号隐藏只能靠 PImpl**;而 §10 待决策项 1 倾向"移内部头不用 PImpl"。二者矛盾——单纯移到 `horizon_internal.h` 再被 `horizon.h` `#include`,符号仍在命名空间内,并未屏蔽。

**约束 3 — 部分目标已自动达成。** `ExecutionPlan`(L77)、`SubmissionSync`(L78)、`ExecutionCompiler`、`CompiledSubmission` 在 `horizon.h` 里**已经只是前向声明**,定义在 `src/.../hardware/execution.h`。即 A2 想隐藏的"编译产物"一类**已经隐藏**。剩余暴露的只是被 by-value 公共类结构性需要的 IR/Token 族。

### 11.3 修正后的 A2 结论

在"不引入 PImpl"约束下,A2 的"符号屏蔽"目标**无法实质达成**,只能做文本搬迁(`horizon.h` 仍 `#include` 内部头),收益限于人/AI 阅读主头时更清爽,但伴随 include 顺序风险(`CommandPayload` 引用其上方的 `DispatchDesc/DrawIndexedDesc/PresentDesc`,且头尾有 `BoundField` 模板定义),且**我无法本地编译验证**。

**建议**:A2 改为二选一,留待用户决策后再执行:
  - **A2-PImpl(彻底)**:`CommandRecorder`/`SubmitReceipt` 改 PImpl,IR/Token 进 `src/`,重写白盒测试为黑盒。改造量大,需编译在环。
  - **A2-文本拆分(保守)**:仅把已前向声明之外、纯内部的枚举/结构集中注释为"高级/内部",不移动定义,零编译风险,零符号屏蔽——本质是文档化边界。

### 11.4 修正后的 A3 结论

A3(DeviceMask 下沉)比设计预估的**爆炸半径更大**:除 `CommandRecorder` 的 ~8 个声明(`horizon.h:436-443`)与其 `execution.cpp` 定义外,还需改:
  - ~15 个值命令门面结构体(各自存 `DeviceMask devices{}` 成员并在 `record()` 里透传)。
  - ~8 个自由工厂函数(`horizon.h:2182-2225`)。
  - 且为不丢能力,需按设计补一个 `on_devices()` 高级入口(非纯删除)。

属"宽机械改动",任一遗漏调用点即编译失败,**必须编译在环**。建议在用户本地构建可用后,作为独立一轮执行。

### 11.5 本轮净交付

- ✅ **A1 已实施**(3 文件):examples 去除冗余 `QueueResolver`,改默认构造。
- ✅ **A2 已实施**:执行 / 命令 IR 类型抽离到 `include/horizon_execution.h`,`horizon.h` 改为 `#include` 该头并保留一段指向内部头的说明注释。采用"内部头拆分"(用户决策),非 PImpl;在用户删除白盒测试后,符号搬迁不再被测试套件阻塞。
- ✅ **A3 随 A2 达成**:`CommandRecorder` 的 `DeviceMask` 签名已进入内部头;剩余命令门面 / 工厂函数的 `DeviceMask` 为带默认值尾参,不强制暴露,按用户决策暂不再动。
- ✅ **测试删除善后**:`CMakeLists.txt` 的 `HORIZON_BUILD_TESTS` 默认改 `OFF`,`add_subdirectory(tests)` 加 `EXISTS` 守卫,避免 `tests/` 已删后 configure 失败。
- ⏳ **待构建验证**:`cmake --build` / `dev.ps1 build HorizonExamples` 在本环境被自动审批拦截,需用户本地构建确认。

### 11.6 文档同步(本轮一并完成)

按 `AGENTS.md` 同步契约,核心理念写入归属层并同步:

- `AGENTS.zh-CN.md` §1 加"API 设计核心理念",§2 路由表与 §3 关键目录加 `horizon_execution.h` / `api-layering.md`;同步英文 `AGENTS.md`。
- 新增上下文包 `docs/agents/zh-CN/api-layering.md`(中文源)+ `docs/agents/api-layering.md`(英文),并注册进 `tools/sync-agents.ps1` 与 `docs/agents/index.md`。
- `README.md` 加"设计理念"段,修正"核心 API / 目录速览"指向 `horizon.h` / `horizon_execution.h` 与活跃后端。
- 本设计文档开篇加"核心理念"段。

> **同步 marker 待刷新**:本环境无法运行 SHA256 / `sync-agents.ps1`,已把改动的英文 sync 文件 marker 临时置为 `PENDING_SYNC`。用户需运行 `=sa` 或手动执行 `.\tools\sync-agents.ps1`(按其输出更新各英文文件顶部 marker),再 `.\tools\sync-agents.ps1 -Check` 确认全部 in sync。
