# Horizon Agent Entry
<!-- AGENTS_ZH_CN_SHA256: e8a3aa93b191fc53ff86ab4261e0fde703bb23c87008320f3e1aded3aa401bf0 -->

> `AGENTS.zh-CN.md` is the Chinese source for the root AI entry.
> Other Chinese sources live in `docs/agents/zh-CN/`, `docs/tasks/zh-CN/`, and `.agents/skills/horizon-workflow/SKILL.zh-CN.md`.
> English files are the default AI entrypoints and must stay aligned with Chinese sources.

## 1. Core Rules

Horizon is a C++20 Vulkan graphics hardware abstraction layer with public APIs, a Vulkan backend, Helicon shader/codegen/reflection, examples, and tooling. The project must support multithreaded concurrent use and is expected to evolve toward a compute framework.

When working in this repository:

- Read this file first, then load only the relevant `docs/agents/*.md` file.
- Keep task context narrow to avoid attention dilution.
- Check `git status --short --branch` before editing.
- Never revert user changes unless explicitly asked.
- Avoid unrelated edits under `third-party/`, `modules/`, or historical mirror trees.
- When adding public APIs, backend objects, or shared state, assume multithreaded callers; do not introduce implicit single-thread assumptions, and make ownership, synchronization boundaries, or immutable snapshot strategy explicit.
- Keep compute / dispatch as first-class execution paths; do not bury graphics / present special cases inside generic resource, execution, or public abstractions.
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
- `docs/tasks/`: short task checklists, reproduction steps, and validation recipes.
- `.agents/skills/`: shared project agent skills.

## 4. Default Validation

After changing agent docs or skills, check sync:

```powershell
.\tools\sync-agents.ps1 -Check
```

For C++, CMake, tooling, or example changes, choose the relevant build:

```powershell
.\tools\dev.ps1 build Horizon
.\tools\dev.ps1 build ShaderCompileScripts
.\tools\dev.ps1 build HorizonExamples
```

Docs-only changes usually do not require a CMake build.

## 5. Project Commands

These commands use the `=` prefix to avoid conflicts with slash commands and mention syntax.

### `=sa`

Sync all English agent files from their Chinese sources.

- Do not modify Chinese source files.
- Sync scope includes root `AGENTS.md`, `docs/agents/*.md`, `docs/tasks/*.md`, and the project skill.
- Preserve the same section structure; English should stay short, direct, and AI-context friendly.
- Update sync markers near the top of matching English files.
- Run `.\tools\sync-agents.ps1 -Check`.

### `=ca`

Check whether all English agent files are synchronized with Chinese sources.

- Only run `.\tools\sync-agents.ps1 -Check`.
- Do not modify files.
- If stale, tell the user to run `=sa`.

### `=ai`

Distill durable project context from this or recent AI conversations into the project AI materials.

- Run `git status --short --branch` first, and do not overwrite or revert existing user changes.
- First decide whether the material is worth preserving: it must reduce future misreads, shorten orientation, clarify forbidden actions, or lock in a validation entrypoint.
- Preserve only stable, reusable content: project rules, directory responsibilities, architecture decisions, naming/lifetime/concurrency conventions, validation workflows, and recurring user preferences in this repo.
- Do not preserve temporary guesses, one-off command output, unresolved debates, casual chat, secrets, or overly narrow implementation details.
- Choose the target by ownership: root rules go in `AGENTS.zh-CN.md`; long-lived focused context goes in `docs/agents/zh-CN/*.md`; short task checklists, reproduction steps, or validation recipes go in `docs/tasks/zh-CN/*.md`; shared workflow, commands, intent-recognition rules, or cross-agent behavior go in `.agents/skills/horizon-workflow/SKILL.zh-CN.md`.
- Confirm the evidence before writing: explicit user preferences, verified commands, current code facts, or settled designs. If evidence is weak, report candidates instead of turning them into rules.
- After changing a Chinese source, sync the matching English file and update the SHA256 marker.
- If nothing is certain or valuable enough to preserve, do not edit files; report candidates and why they were not preserved.
- Run `.\tools\sync-agents.ps1 -Check` and report the result.

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
- `docs/tasks/zh-CN/*.md` -> `docs/tasks/*.md`
- `.agents/skills/horizon-workflow/SKILL.zh-CN.md` -> `.agents/skills/horizon-workflow/SKILL.md`

Whenever any Chinese source changes, update its matching English file:

- English should be clear, concise, and suitable as AI entry context.
- Preserve the same rule meanings; word-for-word translation is not required.
- The SHA256 marker near the top of the English file must match the Chinese source.
- If Chinese and English conflict, the Chinese file wins.
- Do not create `.agents/skills/*/zh-CN/SKILL.md` as a Chinese source; that can be discovered as a duplicate skill.
