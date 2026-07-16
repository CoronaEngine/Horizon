# 公共 API 分层与简化理念

本文件是 `docs/agents/api-layering.md` 的中文源。修改后先改这里，再同步英文文件。

## 1. 核心理念

每一层的 API 都应尽可能简化，减少重复，降低用户的认知负担，并降低维护成本。四条落地准则：

- **默认路径最短**：常见用法零样板。能用默认行为完成的，不要求用户显式传参或手写胶水代码。
- **不泄漏内部类型**：用户从不构造的类型（命令 IR、执行计划、提交令牌、队列调度）不出现在主公共头，下沉到内部 / 高级头。
- **不暴露未实现能力**：后端尚未落地的特性（多设备、光追 / 网格管线）不在公共 API 表面承诺。
- **同一概念只表达一次**：抽象、规则、理念都在归属层写一次，避免跨文件 / 跨层重复。

高级能力（自定义队列调度、显式 record/compile/submit 三段式）保留，但绝不作为默认姿势。

## 2. 公共头分层

- `include/horizon.h`：用户主入口。资源（`HardwareBuffer`/`HardwareImage`）、管线（`ComputePipeline`/`RasterizerPipeline`）、命令门面、`*Desc` 工厂。
- `include/horizon_execution.h`：执行 / 命令 IR 层（内部 / 高级）。`QueueCapability`、`DeviceMask`、`CommandIR`、`CommandPayload`、`RecordedTask`、`SubmissionToken`、`SubmitReceipt`、`SubmissionKeepAlive`、`StreamCommand`、`CommandBatch`、`CommandRecorder` 等。由 `horizon.h` 自动包含，用户无需单独引入。

分层依据：`horizon_execution.h` 只依赖 `format.h` / `resource.h`，不依赖 `HardwareBuffer` / 管线，是自包含的下层；资源 / 管线 / 命令门面依赖它。

## 3. 默认执行器姿势

默认构造的 `HardwareExecutor` 已自动解析主设备上对应能力的队列（见 `src/hardware_wrapper_vulkan/hardware/execution.cpp` 的 `resolve_queue`）。示例和用户代码默认直接 `HardwareExecutor executor;`，**不要**手写与默认行为等价的 `QueueResolver`。仅在需要固定 / 自定义队列调度时才传入 resolver。

## 4. Pipeline Desc 与自动绑定姿势

`ComputePipelineDesc` / `RasterizerPipelineDesc` 是公共 API 中唯一的默认状态 / create-info 容器。不要再为了“可选覆盖默认状态”引入并行的 `*Config` 类型；用户只需要默认构造 desc，并改动确实需要覆盖的字段。

Typed shader 和生成代码路径的默认写法应保持短路径：

```cpp
Corona::Horizon::RasterizerPipelineDesc desc;
desc.depth_stencil = colorOnlyDepthStencil();

Corona::Horizon::RasterizerPipeline rasterizer(default_vert_glsl, default_frag_glsl, desc);
rasterizer.outColor = finalOutputImages[threadIndex];
```

EDSL 路径也使用同一个姿势：

```cpp
Corona::Horizon::RasterizerPipeline rasterizer(vsLambda, fsLambda, desc);
```

管线构造函数内部负责根据 shader / EDSL 编译结果填充 shader module、反射资源、自动绑定元数据和 generated binding 成员。`Desc` 可以在内部携带 shader 字段，但普通用户默认不手写 `PipelineShaderDesc::from_slang_module`、`from_edsl` 或索引式绑定；只有低级 / 高级路径才显式构造完整 desc。

## 5. 已落地的简化（方案 A）

- **A1**：示例去掉冗余 `QueueResolver`，改默认构造（`example_default` / `example_edsl` / `example_glsl`）。
- **A2**：执行 / 命令 IR 类型从 `horizon.h` 抽离到 `horizon_execution.h`，主头从 IR 噪音中解脱。
- **A3**：`CommandRecorder` 的 `DeviceMask` 签名随 A2 进入内部头，主要目标顺带达成；剩余命令门面 / 工厂函数的 `DeviceMask` 均为带默认值尾参，不强制暴露，暂不再动。
- **A4**：typed pipeline wrapper 保留 generated binding 成员访问，runtime base 承载公共执行 / 资源绑定逻辑；示例使用 `RasterizerPipeline(shader, shader, desc)` 和成员字段绑定，避免手写 shader desc 或索引绑定。

设计全文与决策记录见 `docs/design/api-simplification.md`。

## 6. 验证

```powershell
uv run --frozen python tools/dev.py build HorizonExamples
```

纯文档改动无需构建；改动公共头后必须构建验证。
