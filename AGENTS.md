# Horizon Agent Entry
<!-- AGENTS_ZH_CN_SHA256: 594018cce8b706fadcd9de3e1539c187ce7864f1d3e78d7fe3347ece8b4444ef -->

> `AGENTS.zh-CN.md` is the Chinese source for the root AI entry.
> Other Chinese sources live in `docs/agents/zh-CN/` and `.agents/skills/horizon-workflow/SKILL.zh-CN.md`.
> English files are the default AI entrypoints and must stay aligned with Chinese sources.

## 1. Core Rules

Horizon is a C++20 Vulkan graphics hardware abstraction layer with public APIs, a Vulkan backend, Helicon shader/codegen/reflection, examples, and tooling.

When working in this repository:

- Read this file first, then load only the relevant `docs/agents/*.md` file.
- Keep task context narrow to avoid attention dilution.
- Check `git status --short --branch` before editing.
- Never revert user changes unless explicitly asked.
- Avoid unrelated edits under `third-party/`, `modules/`, or historical mirror trees.
- Report verification commands and results for meaningful changes.

## 2. Context Router

Load only what the task needs:

- Build, CMake, presets: `docs/agents/build.md`
- Local commits, GitHub publishing, commits, PRs: `docs/agents/git.md`
- Formatting and clang-format: `docs/agents/formatting.md`
- Vulkan backend, VOLK/VMA, descriptors, barriers: `docs/agents/vulkan.md`
- Helicon, shader DSL, codegen, reflection: `docs/agents/helicon.md`
- Push constant layout, binding fields, runtime consumer chain: `docs/agents/push-constants.md`

Shared project agent skill:

- `.agents/skills/horizon-workflow/SKILL.md`

The skill is plain Markdown and vendor-neutral; it is not Codex-specific.

## 3. Key Paths

- `include/`: public API; edit carefully.
- `src/`: main Horizon implementation.
- `src/hardware_wrapper_vulkan/`: Vulkan backend currently compiled by CMake.
- `src/HardwareWrapperVulkan/`: historical/parallel Vulkan backend; edit only when explicitly requested.
- `src/Helicon/`: shader DSL, AST, codegen, compiler, reflection.
- `tools/`: tool programs and scripts.
- `examples/`: example programs and shader assets.
- `docs/agents/`: on-demand AI context packs.
- `.agents/skills/`: shared project agent skills.

## 4. Default Validation

After changing agent docs or skills, check sync:

```powershell
.\tools\sync-agents.ps1 -Check
```

For C++, CMake, tooling, or example changes, choose the relevant build:

```powershell
cmake --build --preset msvc-debug --target Horizon
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target HorizonExamples
```

Docs-only changes usually do not require a CMake build.

## 5. Project Commands

These commands use the `=` prefix to avoid conflicts with slash commands and mention syntax.

### `=sa`

Sync all English agent files from their Chinese sources.

- Do not modify Chinese source files.
- Sync scope includes root `AGENTS.md`, `docs/agents/*.md`, and the project skill.
- Preserve the same section structure; English should stay short, direct, and AI-context friendly.
- Update sync markers near the top of matching English files.
- Run `.\tools\sync-agents.ps1 -Check`.

### `=ca`

Check whether all English agent files are synchronized with Chinese sources.

- Only run `.\tools\sync-agents.ps1 -Check`.
- Do not modify files.
- If stale, tell the user to run `=sa`.

### `=gc`

Check whether current changes are ready to publish to GitHub.

- Run `git status --short --branch`.
- Inspect changed files and relevant diffs.
- Run relevant validation.
- Do not stage, commit, push, or create a PR.
- If ready for a local commit, tell the user they can run `=cm`.
- Only mention `=gh` when the user explicitly needs a GitHub PR.

### `=cm`

Commit current intended changes to the current local branch only.

- Inspect scope and validation results first.
- Stage only files related to the current task; do not default to `git add .`.
- The commit must include a title and a Chinese body summary describing what changed, why it changed, and what was verified.
- Do not push, create a PR, or merge branches.
- Report branch, commit, and validation results.

### `=gh`

Commit current intended changes and publish them to a GitHub PR.

- Inspect scope and validation results first.
- Stage only files related to the current task; do not default to `git add .`.
- If uncommitted changes exist, commit them first using the `=cm` rules.
- Push the current branch to `origin`.
- Open a GitHub draft PR.
- Report branch, commit, PR URL, and validation results.

## 6. Sync Rule

Chinese files are the human-maintained sources. English files are the default AI entrypoints.

Sync relationships:

- `AGENTS.zh-CN.md` -> `AGENTS.md`
- `docs/agents/zh-CN/*.md` -> `docs/agents/*.md`
- `.agents/skills/horizon-workflow/SKILL.zh-CN.md` -> `.agents/skills/horizon-workflow/SKILL.md`

Whenever any Chinese source changes, update its matching English file:

- English should be clear, concise, and suitable as AI entry context.
- Preserve the same rule meanings; word-for-word translation is not required.
- The SHA256 marker near the top of the English file must match the Chinese source.
- If Chinese and English conflict, the Chinese file wins.
- Do not create `.agents/skills/*/zh-CN/SKILL.md` as a Chinese source; that can be discovered as a duplicate skill.
