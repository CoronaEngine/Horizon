# Helicon Slang Module Transition Handoff
<!-- TASK_DOCS_HELICON_SLANG_MODULE_TRANSITION_ZH_CN_SHA256: 53fc5d36b4bdd81a48fa1ac44166494a162dd72fc614978381bb8f20c27734a1 -->

This file records the handoff state for the current uncommitted Helicon WIP on 2026-06-08. It is not a completed design document; if the diff has changed, trust a fresh `git status --short --branch` and `git diff`.

## Current Facts

- Goal: move the Helicon shader compilation path from old SPIR-V link / SPIRV-Cross reflection toward Slang modules, module dependencies, Slang reflection, and generated bindings that agree with runtime consumers.
- Scope: mainly `src/Helicon/Compiler/`, `src/Helicon/Codegen/`, and `tools/main.cpp`. The dirty tree also contains a Vulkan API version change in `src/hardware_wrapper_vulkan/hardware_context.cpp`; decide separately whether that belongs with the Helicon task.
- Current state:
  - `src/Helicon/Compiler/ShaderCommon.h` / `.cpp` are new untracked files containing `ShaderLanguage`, `ShaderStage`, IR reflection types, `SlangModule`, and `SlangCompileArgs*`.
  - `ShaderCodeCompiler` has moved from `CompilerOption::spvLinkBinary` toward `CompilerOption::slangModules`; `ShaderCodeModule` can hold `SlangModule*`; `ShaderResources` now includes entry point / numthreads metadata.
  - AST parse output and `EmbeddedShaderStructure` now use `sourceModule` instead of `sourceSpv`; `SlangGenerator` emits `import ...;` for module deps.
  - `ComputePipelineObject` / `RasterizedPipelineObject` started passing parsed Slang module deps into the compiler.
  - `tools/main.cpp` now emits a `SlangModule` literal and uses Slang reflection to generate function proxies, structs, binding keys, and `Bindings<P>`.
  - Current codegraph search can see the new `ShaderCommon` and Slang module symbols; `.codegraph/` is still a local index and must not be committed. Refresh it only when later symbol-flow or impact analysis needs it.

## Top Next Action

- `TD-001` `[ready]`: do a source-level migration closure pass first, not compile validation. Make `ShaderCommon.*` ownership, CMake inclusion, module deps, reflection outputs, and generated binding consumers self-consistent before build acceptance.

## Active Items

- `TD-001` `[ready]`: fix the current source-level high-risk points.
  - `ShaderLanguageConverter::getCompileResult` writes binary targets and then `continue`s, so it skips reflection for binary targets; `ShaderCodeCompiler` later reads `result.reflections[...]` per target.
  - `RasterizedPipelineObject::compile` uses `auto& vertSlangModules = compilerOption.slangModules`, then swaps with fragment deps; prefer independent vector snapshots for VS / FS deps.
  - `tools/main.cpp::procParam` gets `param` in the loop but still emits `param0`.
  - `tools/main.cpp` checks `bindingBlockNames.contains(reflection->getName())` when generating structs; this may need `child->getName()`.
  - Descriptor `set` / `binding` metadata was removed or no longer passed through `BindingKey` / `BoundField` / `AutoBindEntry`; confirm all runtime consumers no longer need it, or restore equivalent metadata.
  - Some `FunctionProxy` paths commented out `Ast::AST::addGlobalStatement(node)`; verify module imports still generate declarations, ignored-return calls, and generated bindings correctly.
- `TD-002` `[ready]`: run validation only after the source-level issues are closed. Windows/MSVC builds must run inside the Visual Studio developer environment; do not treat missing standard library headers from plain PowerShell as a code result.
- `TD-003` `[ready]`: refresh codegraph only when the next step needs call chains, symbol flow, or impact analysis. Inspecting the current diff does not require refreshing it.

## Evidence

- `EV-001`: current dirty scope includes Helicon compiler/codegen, `tools/main.cpp`, `src/hardware_wrapper_vulkan/hardware_context.cpp`, and untracked `src/Helicon/Compiler/ShaderCommon.cpp` / `.h`.
- `EV-002`: codegraph status/search can see `ShaderCommon`, `SlangModule`, `slangCompilerWithModules`, `slangModuleReflectShaderResource`, and `collectSlangReflection`.
- `EV-003`: the user explicitly said this pass does not need compile validation and is expected not to build cleanly. Unless the user asks for validation or commit readiness, do not start from a build.

## Failed Explorations

- `EXP-001`:
  - Motivation: quickly validate with a `ShaderCompileScripts` build.
  - Tried: ran `uv run --frozen python tools/dev.py build ShaderCompileScripts`.
  - Result: MSVC dependency scanning failed to find standard headers such as `string`, `functional`, `type_traits`, and `inttypes.h`.
  - Rejection reason: the shell lacked the VS developer environment and `INCLUDE` was unset; this is not useful code evidence.
  - Retry condition: only during acceptance, wrap the build with `cmd.exe /d /s /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 && ..."`.
- `EXP-002`:
  - Motivation: user only asked to confirm codegraph / diff state.
  - Tried: moved into compile validation.
  - Result: user interrupted and clarified that this pass does not need compile validation.
  - Rejection reason: confirming the diff or whether codegraph needs updating is read-only by default, not build acceptance.
  - Retry condition: user explicitly asks for validation, publication readiness, a commit, or implementation has reached acceptance.

## Validation

For doc-only changes:

```powershell
.\tools\sync-agents.ps1 -Check
git diff --check
```

After the Helicon implementation is closed:

```powershell
cmd.exe /d /s /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target ShaderCompileScripts"
cmd.exe /d /s /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 && cmake --build --preset msvc-debug --target Horizon"
```
