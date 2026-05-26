# Horizon Agent Guide
<!-- AGENTS_ZH_CN_SHA256: fafb4b3da86b6383be60297d299963811731efa0c6ce40ef6908a7871b1d8482 -->

> This file is the English AI-facing project context for Horizon.
> The human-maintained source is `AGENTS.zh-CN.md`.
> When `AGENTS.zh-CN.md` changes, update this file to keep both versions semantically aligned.

## 1. Project Purpose

Horizon is a C++20 Vulkan-based graphics hardware abstraction layer. It provides APIs and implementation layers for resources, commands, pipelines, and shader workflows.

The project is still under active iteration and refactoring. When an AI assistant modifies this repository, it must prioritize:

- Preserving existing build entry points.
- Keeping changes tightly scoped.
- Avoiding confusion between duplicated directory trees.
- Attaching a clear verification command to each meaningful change.
- Reading the actual implementation before making architectural assumptions.

## 2. Main Directories

- `include/`: Public API surface. Changes here affect user code and must be handled carefully.
- `src/`: Main Horizon implementation.
- `src/Helicon/`: Shader DSL, AST, code generation, compilation, and reflection logic.
- `src/hardware_wrapper_vulkan/`: The Vulkan backend tree currently compiled by CMake.
- `src/HardwareWrapperVulkan/`: Historical or parallel Vulkan backend tree. Do not edit it unless the task explicitly points there.
- `src/hardware_wrapper/` and `src/HardwareWrapper/`: Hardware abstraction wrapper layers with historical casing duplication.
- `tools/`: Tooling and scripts, including `ShaderCompileScripts` and formatting helpers.
- `examples/`: Example programs and shader assets.
- `cmake/`: CMake helper modules.
- `modules/`: Embedded modules, especially `modules/ocarina` and `modules/corona`.
- `third-party/`: Third-party libraries and binaries. Avoid unrelated edits.
- `docs/`: Project documentation.
- `tests/`: Test area. It may still be incomplete; new tests should prefer this tree.

## 3. Duplicate Directory Rules

This repository contains historical duplicate trees with different casing, including:

- `src/hardware_wrapper_vulkan/`
- `src/HardwareWrapperVulkan/`
- `src/hardware_wrapper/`
- `src/HardwareWrapper/`

Before editing, the assistant must confirm which tree the task refers to.

Default rules:

- Build-related decisions should follow the current `src/CMakeLists.txt`.
- If the user explicitly names a camel-case path, inspect the camel-case tree first.
- If the user does not name a path, inspect the tree currently compiled by CMake first.
- Do not refactor both mirrored trees in a single change unless the task explicitly asks for migration or deletion.

## 4. Build Commands

Recommended Windows / MSVC / Ninja workflow:

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target HorizonExamples
```

Library-only verification:

```powershell
cmake --build --preset msvc-debug --target Horizon
```

Shader compiler, reflection, or generation changes:

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
```

Example, asset loading, or rendering-path changes:

```powershell
cmake --build --preset msvc-debug --target HorizonExamples
```

## 5. Formatting Rules

The project uses `.clang-format`.

Known style signals:

- C++ uses 4-space indentation.
- Pointer and reference markers bind to the type side, for example `int* value` and `auto& device`.
- Namespace bodies are indented.
- Short inline functions may stay on one line.
- Namespace closing comments are not required.
- Do not format third-party code unless the task explicitly requires it.

Format check example:

```powershell
clang-format --style=file --dry-run --Werror src/hardware_wrapper_vulkan/hardware_context.h
```

Project formatting script:

```powershell
.\tools\code-format.ps1 -Check
.\tools\code-format.ps1
```

Do not perform broad unrelated formatting in normal feature or bug-fix tasks.

## 6. CMake Rules

- Root `CMakeLists.txt` owns project-level options, dependencies, and subdirectory wiring.
- `src/CMakeLists.txt` defines the `Horizon` static library.
- `tools/CMakeLists.txt` defines `ShaderCompileScripts`.
- `examples/CMakeLists.txt` defines `HorizonExamples`.
- `include/` is the public include surface.
- `src/` implementation details should remain private.
- Do not expose Vulkan, VMA, Windows, or implementation-only types through public headers unless the public API truly requires them.

After CMake changes, run at least:

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon
```

## 7. Public API Rules

The main public API files are:

- `include/Horizon.h`
- `include/HardwareCommands.h`

When changing public API, explicitly consider:

- Whether user code must change.
- Whether resource binding behavior changes.
- Whether pipeline invocation behavior changes.
- Whether generated shader binding structures are affected.
- Whether examples must be updated.

Do not leak Vulkan types, Windows types, or third-party implementation details into `include/` to fix internal implementation issues.

## 8. Vulkan Backend Rules

Vulkan backend changes must handle these areas carefully:

- Vulkan object lifetime.
- VMA allocation lifetime.
- Descriptor sets and descriptor pools.
- Pipeline layouts.
- Push constant ranges.
- Command buffer submission.
- Queue family selection.
- Image layouts and memory barriers.
- Swapchain and display logic.

Include-boundary rules:

- Internal headers exposing Vulkan types may include `<volk.h>` directly.
- Internal headers exposing VMA types may include `<vk_mem_alloc.h>` directly.
- Windows `HANDLE` should only appear in internal interfaces that truly expose that type.
- `VOLK_IMPLEMENTATION` must never appear in a header.
- `VMA_IMPLEMENTATION` must appear in exactly one `.cpp` implementation file.
- Do not create a new catch-all utility header that centralizes every Vulkan dependency.

## 9. Shader / Helicon Rules

`src/Helicon/` owns shader DSL, AST, code generation, compilation, and reflection.

Important paths include:

- `src/Helicon/Compiler/ShaderCodeCompiler.h`
- `src/Helicon/Compiler/ShaderCodeCompiler.cpp`
- `src/Helicon/Compiler/ShaderLanguageConverter.h`
- `src/Helicon/Compiler/ShaderLanguageConverter.cpp`
- `src/Helicon/Codegen/`
- `tools/main.cpp`

When changing shader reflection, trace the full consumer chain:

1. Where reflection data is produced.
2. Which fields are stored in `ShaderResources`.
3. How `tools/main.cpp` generates binding code.
4. How `include/Horizon.h` turns user assignments into runtime writes.
5. How the Vulkan pipeline uses the data to create layouts or write push constants.

## 10. Push Constant Rules

The effective source for push constant layout is SPIR-V reflection, especially `spirv-cross`.

Do not assume Slang reflection provides complete push constant information.

The Vulkan side currently cares about:

- Push constant block size.
- Member name.
- Member byte offset.
- Member type size.
- Bind type.
- Whether resource-like values are represented as 32-bit or 64-bit handles.

For push constant changes, inspect:

- `ShaderResources::pushConstantSize`
- `ShaderResources::pushConstantName`
- `ShaderBindInfo::byteOffset`
- `ShaderBindInfo::typeSize`
- `ShaderBindInfo::bindType`
- Generated binding structs.
- Compute/rasterizer pipeline push constant storage and `vkCmdPushConstants` calls.

## 11. Tests and Verification

Each change should run at least one relevant verification command.

Minimum build verification:

```powershell
cmake --build --preset msvc-debug --target Horizon
```

Tools or shader compiler changes:

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
```

Example changes:

```powershell
cmake --build --preset msvc-debug --target HorizonExamples
```

Formatting or include-boundary changes:

```powershell
.\tools\code-format.ps1 -Check
cmake --build --preset msvc-debug --target Horizon
```

If the test system is available:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

If verification cannot be run, the assistant must state why.

## 12. Git and Change Scope

The assistant must:

- Never revert user changes unless explicitly asked.
- Never run `git reset --hard`.
- Never delete files it does not understand.
- Avoid unrelated include reordering, formatting, or renaming.
- Check `git status --short --branch` before editing.
- Keep each task scoped to relevant files.
- Ignore unrelated existing worktree changes instead of reverting them.

Commit message prefixes may include:

- `docs: ...`
- `build: ...`
- `tools: ...`
- `test: ...`
- `refactor: ...`
- `fix: ...`

## 13. AI Working Method

At the start of a task, the assistant should:

1. Read this file.
2. Check `git status --short --branch`.
3. Search relevant paths and symbols.
4. Understand the real call chain.
5. Make the smallest necessary change.
6. Run relevant verification.
7. Summarize the change, verification result, and remaining risk.

Final responses should usually include:

- What changed.
- Why it changed.
- Which verification commands ran.
- What remains unverified, if anything.
- A practical next step for the user.

## 14. Forbidden Actions

Do not:

- Modify `third-party/` without explicit task scope.
- Modify external or vendor-like code under `modules/` without explicit task scope.
- Leak Vulkan implementation details into public API.
- Mix changes across both casing variants of historical directories in one task.
- Substitute guesses for code search.
- Broadly format unrelated files.
- Add large abstractions without a verification path.
- Define `VOLK_IMPLEMENTATION` or `VMA_IMPLEMENTATION` in a header.
- Hide real lifetime or synchronization problems just to silence compiler errors.

## 15. Chinese-to-English Sync Rule

`AGENTS.zh-CN.md` is the human-maintained source file.

Whenever it changes, update `AGENTS.md`:

- Keep the same section structure.
- Use clear, direct English suitable for AI project context.
- Do not require word-for-word translation, but preserve the same rules.
- If Chinese adds a forbidden action, English must add it too.
- If Chinese updates a build command, English must update it too.
- If English conflicts with Chinese, the Chinese file wins.

## 16. Project Commands

When the user enters one of these project commands, the assistant must follow the command definition exactly.
These commands use the `=` prefix to avoid conflicts with built-in slash commands and mention syntax.

### `=sa`

`=sa` is short for "sync agents".

Update `AGENTS.md` from `AGENTS.zh-CN.md`.

Rules:

- Treat `AGENTS.zh-CN.md` as the only source of truth.
- Do not modify `AGENTS.zh-CN.md`.
- Keep `AGENTS.md` in English.
- Preserve the same section structure in both files.
- Preserve all paths, commands, warnings, constraints, and forbidden actions.
- Make the English concise, direct, and optimized as AI project context.
- Add or update the `AGENTS_ZH_CN_SHA256` marker near the top of `AGENTS.md`.
- Run `.\tools\sync-agents.ps1 -Check` after syncing.
- Do not modify unrelated files.

### `=ca`

`=ca` is short for "check agents".

Check whether `AGENTS.md` is synchronized with `AGENTS.zh-CN.md`.

Rules:

- Run `.\tools\sync-agents.ps1 -Check`.
- Do not modify any files.
- Report whether the files are synchronized.
- If they are not synchronized, tell the user to run `=sa`.

### `=gc`

`=gc` is short for "GitHub check" or "git check".

Check whether the current worktree is ready to publish to GitHub.

Rules:

- Run `git status --short --branch`.
- Inspect the changed files and relevant diffs.
- Run relevant validation commands, but do not stage, commit, push, or create a PR.
- If `AGENTS.md` and `AGENTS.zh-CN.md` exist, run `.\tools\sync-agents.ps1 -Check`.
- Docs-only changes usually do not require a CMake build.
- C++, CMake, tooling script, or example changes must run the matching build validation.
- Report which files would be included in the commit.
- Report which validation passed or failed.
- If the worktree is ready to publish, tell the user they can run `=gh`.

### `=gh`

`=gh` is short for "GitHub publish".

Publish the current intended changes to GitHub.

Rules:

- Run `git status --short --branch`.
- Inspect the changed files and relevant diffs before staging.
- If the worktree contains unrelated changes, ask the user which files belong in this PR.
- Stage only files related to the current task.
- Do not use `git add .` unless the entire worktree is clearly in scope.
- Run relevant validation before committing.
- If `AGENTS.md` and `AGENTS.zh-CN.md` exist, run `.\tools\sync-agents.ps1 -Check`.
- Docs-only changes usually do not require a CMake build.
- C++, CMake, tooling script, or example changes must run the matching build validation.
- Do not push if validation fails unless the user explicitly asks to continue.
- Commit with a concise conventional commit message.
- The commit message must include both a title and a body summary; write the body in Chinese and describe what changed, why it changed, and what was verified.
- Push the current branch to `origin`.
- Open a GitHub draft pull request.
- Report the branch name, commit hash, PR URL, and validation result.
