# Public API Layering and Simplification Philosophy

<!-- AGENT_DOCS_API_LAYERING_ZH_CN_SHA256: c916ebd07c1d13f4abba665cb8cfe1cb9da24b8110ffecbda56ce787ce92bfd8 -->

> `docs/agents/zh-CN/api-layering.md` is the Chinese source. Edit it first, then sync this file.

## 1. Core Philosophy

Every layer's API should be as simple as possible, minimize duplication, lower the user's cognitive load, and reduce maintenance cost. Four operating rules:

- **Shortest default path**: zero boilerplate for common usage. If a default behavior does the job, do not force users to pass arguments explicitly or hand-write glue code.
- **No leaked internal types**: types users never construct (command IR, execution plan, submission tokens, queue scheduling) do not appear in the main public header; push them into an internal/advanced header.
- **No unimplemented capabilities exposed**: features the backend has not landed (multi-device, ray-tracing/mesh pipelines) are not promised on the public API surface.
- **Express each concept once**: abstractions, rules, and philosophy live in their owning layer, written once, avoiding cross-file/cross-layer duplication.

Advanced power (custom queue scheduling, explicit record/compile/submit) stays available but is never the default posture.

## 2. Public Header Layering

- `include/horizon.h`: main user entry. Resources (`HardwareBuffer`/`HardwareImage`), pipelines (`ComputePipeline`/`RasterizerPipeline`), command facades, `*Desc` factories.
- `include/horizon_execution.h`: execution / command IR layer (internal / advanced). `QueueCapability`, `DeviceMask`, `CommandIR`, `CommandPayload`, `RecordedTask`, `SubmissionToken`, `SubmitReceipt`, `SubmissionKeepAlive`, `StreamCommand`, `CommandBatch`, `CommandRecorder`, etc. Included automatically by `horizon.h`; users do not include it separately.

Layering rationale: `horizon_execution.h` depends only on `format.h` / `resource.h`, not on `HardwareBuffer` / pipelines — a self-contained lower layer; resources / pipelines / command facades depend on it.

## 3. Default Executor Posture

A default-constructed `HardwareExecutor` already resolves a queue with the requested capability on the main device (see `resolve_queue` in `src/hardware_wrapper_vulkan/hardware/execution.cpp`). Examples and user code default to `HardwareExecutor executor;` directly; do **not** hand-write a `QueueResolver` that duplicates the default behavior. Pass a resolver only for fixed / custom queue scheduling.

## 4. Pipeline Desc And Auto Binding Posture

`ComputePipelineDesc` / `RasterizerPipelineDesc` are the only public default-state / create-info containers. Do not introduce a parallel `*Config` type merely for optional default-state overrides; users should default-construct a desc and change only the fields they need to override.

The default generated-shader path should stay short:

```cpp
Corona::Horizon::RasterizerPipelineDesc desc;
desc.depth_stencil = colorOnlyDepthStencil();

Corona::Horizon::RasterizerPipeline rasterizer(default_vert_glsl, default_frag_glsl, desc);
rasterizer.outColor = finalOutputImages[threadIndex];
```

The EDSL path uses the same posture:

```cpp
Corona::Horizon::RasterizerPipeline rasterizer(vsLambda, fsLambda, desc);
```

Pipeline constructors fill shader modules, reflected resources, auto-bind metadata, and generated binding members from the shader / EDSL compile result. `Desc` may carry shader fields internally, but regular users should not hand-write `PipelineShaderDesc::from_slang_module`, `from_edsl`, or index-style binding on the default path; reserve full desc construction for lower-level / advanced entry points.

## 5. Landed Simplifications (Plan A)

- **A1**: examples drop the redundant `QueueResolver` and use default construction (`example_default` / `example_edsl` / `example_glsl`).
- **A2**: execution / command IR types are extracted from `horizon.h` into `horizon_execution.h`, freeing the main header from IR noise.
- **A3**: `CommandRecorder`'s `DeviceMask` signatures moved into the internal header with A2, so the main goal is reached as a side effect; the remaining `DeviceMask` on command facades / factory functions are default-valued trailing parameters, not forced on users, and are left as-is for now.
- **A4**: typed pipeline wrappers keep generated binding member access while runtime bases own common execution / resource-binding logic; examples use `RasterizerPipeline(shader, shader, desc)` plus member-field binding instead of hand-written shader descs or index bindings.

Full design and decision log: `docs/design/api-simplification.md`.

## 6. Validation

```powershell
.\tools\dev.ps1 build HorizonExamples
```

Docs-only changes need no build; changing public headers requires a build.
