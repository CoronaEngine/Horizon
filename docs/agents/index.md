# Horizon Agent Context Index
<!-- AGENT_DOCS_INDEX_ZH_CN_SHA256: a075847831247a53cd51b9596437ba29dc8b7284d0d3277d25b24cc8746699e2 -->

This directory contains vendor-neutral AI context packs. Any agent can read them; they are not Codex-specific.

Chinese sources are human-maintained:

- `AGENTS.zh-CN.md`
- `docs/agents/zh-CN/*.md`
- `docs/tasks/zh-CN/*.md`
- `.agents/skills/*/SKILL.zh-CN.md`

Start with root `AGENTS.md`, then load only the relevant context pack or task note:

- `build.md`: CMake presets, build targets, validation commands.
- `git.md`: `=gc` and `=gh` publication workflow, commit and PR conventions.
- `formatting.md`: clang-format and style rules.
- `vulkan.md`: Vulkan backend boundaries and safety rules.
- `helicon.md`: shader DSL, codegen, compiler, and reflection context.
- `push-constants.md`: push constant reflection and runtime consumer path.
- `docs/tasks/*.md`: short task checklists, reproduction steps, and validation recipes.

Shared project skills:

- `.agents/skills/horizon-workflow/SKILL.md`: repository workflow, project command routing, and context routing.
- `.agents/skills/agent-project-system/SKILL.md`: project state document system, long-task recovery, evidence loops, and failed explorations.

Keep these files stable and focused. Prefer adding a new small context pack over growing `AGENTS.md`.

Do not create `.agents/skills/*/zh-CN/SKILL.md` as a Chinese source; many agents discover every `SKILL.md` and may load it as a duplicate skill.
