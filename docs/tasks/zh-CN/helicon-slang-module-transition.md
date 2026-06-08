# Helicon Slang Module 迁移接手状态

本文件记录 2026-06-08 当前未提交 Helicon WIP 的接手状态。它不是完成设计说明；如果实际 diff 已变化，以新的 `git status --short --branch` 和 `git diff` 为准。

## 当前事实

- Goal: 把 Helicon shader 编译链从旧的 SPIR-V link / SPIRV-Cross 反射路径，迁移到 Slang module、module deps、Slang reflection 和 generated binding 一致工作的路径。
- Scope: 主要涉及 `src/Helicon/Compiler/`、`src/Helicon/Codegen/` 和 `tools/main.cpp`。当前 dirty tree 还包含 `src/hardware_wrapper_vulkan/hardware_context.cpp` 的 Vulkan API 版本改动，应单独判断是否属于同一任务。
- Current state:
  - `src/Helicon/Compiler/ShaderCommon.h` / `.cpp` 是新增未跟踪文件，承载 `ShaderLanguage`、`ShaderStage`、IR reflection 类型、`SlangModule` 和 `SlangCompileArgs*`。
  - `ShaderCodeCompiler` 已从 `CompilerOption::spvLinkBinary` 转向 `CompilerOption::slangModules`，`ShaderCodeModule` variant 支持 `SlangModule*`，`ShaderResources` 增加 entry point / numthreads 信息。
  - AST parse output 和 `EmbeddedShaderStructure` 已从 `sourceSpv` 转为 `sourceModule`，`SlangGenerator` 会按 module deps 生成 `import ...;`。
  - `ComputePipelineObject` / `RasterizedPipelineObject` 已开始把 parse output 中的 Slang module deps 传给 compiler。
  - `tools/main.cpp` 已改为生成 `SlangModule` literal，并通过 Slang reflection 生成函数代理、struct、binding key 和 `Bindings<P>`。
  - 当前 codegraph 能识别新 `ShaderCommon` 和 Slang module 相关符号；`.codegraph/` 仍是本地索引，不提交。只有后续需要符号流 / 影响面分析时才需要刷新。

## Top Next Action

- `TD-001` `[ready]`: 先做源码级迁移收口，不要从编译验证开始。把 `ShaderCommon.*` 的归属、CMake 纳入、module deps、reflection 产物和 generated binding 消费链整理到自洽，再进入 build 验收。

## Active Items

- `TD-001` `[ready]`: 修正当前源码级高风险点。
  - `ShaderLanguageConverter::getCompileResult` 中 binary target 分支在写入 `binaryTargets` 后直接 `continue`，会跳过 reflection；而 `ShaderCodeCompiler` 后续按每个 target 读取 `result.reflections[...]`。
  - `RasterizedPipelineObject::compile` 中 `auto& vertSlangModules = compilerOption.slangModules` 让 VS deps 直接引用原 vector，再和 fragment deps `swap`，容易混淆 VS / FS deps。优先改成独立 vector 快照。
  - `tools/main.cpp::procParam` 的多参数循环取得了 `param`，但输出仍使用 `param0`。
  - `tools/main.cpp` 生成 struct 时用 `bindingBlockNames.contains(reflection->getName())` 判断父 reflection 名称，可能应按 `child->getName()` 判断。
  - `BindingKey` / `BoundField` / `AutoBindEntry` 中 descriptor `set` / `binding` 被移除或不再传递，继续前要确认所有 runtime consumer 不需要该元数据，或恢复等价信息。
  - `FunctionProxy` 中部分 `Ast::AST::addGlobalStatement(node)` 被注释，继续前要确认 module import 后函数声明、未使用返回值调用和 generated binding 仍会正确生成。
- `TD-002` `[ready]`: 源码级问题收口后，再运行验证。Windows/MSVC 构建必须通过 Visual Studio developer environment；不要在普通 PowerShell 里把标准库头找不到误判成代码问题。
- `TD-003` `[ready]`: 只有当后续需要调用链、符号流或影响面分析时，才刷新 codegraph；单纯查看当前 diff 不要求刷新。

## Evidence

- `EV-001`: 当前 dirty scope 包括 Helicon compiler/codegen、`tools/main.cpp`、`src/hardware_wrapper_vulkan/hardware_context.cpp`，以及未跟踪 `src/Helicon/Compiler/ShaderCommon.cpp` / `.h`。
- `EV-002`: codegraph status/search 能看到 `ShaderCommon`、`SlangModule`、`slangCompilerWithModules`、`slangModuleReflectShaderResource` 和 `collectSlangReflection`。
- `EV-003`: 用户明确说明“这次不用做编译验证，应该跑不通”。后续接手时，除非用户明确要求验证或准备提交，不要把编译作为第一步。

## Failed Explorations

- `EXP-001`:
  - Motivation: 想用 `ShaderCompileScripts` build 快速确认改动。
  - Tried: 在普通 PowerShell 里运行 `.\tools\dev.ps1 build ShaderCompileScripts`。
  - Result: MSVC 扫描依赖时报 `string`、`functional`、`type_traits`、`inttypes.h` 等标准头找不到。
  - Rejection reason: 当前 shell 没有 VS developer environment，`INCLUDE` 未设置；这不是有效代码结论。
  - Retry condition: 只有进入验收阶段时，用 `cmd.exe /d /s /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 && ..."` 包住构建命令。
- `EXP-002`:
  - Motivation: 用户只要求确认 codegraph/diff。
  - Tried: 进入编译验证。
  - Result: 用户中断并说明这次不用编译验证。
  - Rejection reason: “确认 diff / codegraph 是否更新”默认是只读核对，不是 build 验收。
  - Retry condition: 用户明确要求验证、发布检查、提交，或实现已收口到验收阶段。

## Validation

文档修改后只需要：

```powershell
.\tools\sync-agents.ps1 -Check
git diff --check
```

Helicon 实现收口后再运行：

```powershell
cmd.exe /d /s /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target ShaderCompileScripts"
cmd.exe /d /s /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target Horizon"
```
