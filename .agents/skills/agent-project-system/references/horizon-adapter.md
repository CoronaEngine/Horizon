# Horizon Adapter

Use this reference when `agent-project-system` is applied inside the Horizon repository.

## Principle

Horizon already has a repo-native AI framework:

- root `AGENTS.zh-CN.md` / `AGENTS.md`
- domain context packs in `docs/agents/zh-CN/*.md` / `docs/agents/*.md`
- task notes in `docs/tasks/zh-CN/*.md` / `docs/tasks/*.md`
- shared skills in `.agents/skills/*/SKILL.zh-CN.md` / `SKILL.md`
- synchronization checked by `tools/sync-agents.ps1 -Check`

Inside Horizon, treat the generic project-state model as an operating model, not as a mandatory directory layout. Do not create a parallel `.agent-os/` tree unless the user explicitly asks for an independent state system.

## Generic Role To Horizon Owner

| Generic role | Horizon owner |
| --- | --- |
| project contract | `AGENTS.zh-CN.md` for root repository rules; sync to `AGENTS.md` |
| recovery entrypoint | root `AGENTS.md`, then `docs/agents/index.md`, then the relevant context pack or task note |
| workflow / command rules | `.agents/skills/horizon-workflow/SKILL.zh-CN.md`; sync to `SKILL.md` |
| project-state system workflow | `.agents/skills/agent-project-system/SKILL.zh-CN.md`; sync to `SKILL.md` |
| long-lived domain context | `docs/agents/zh-CN/*.md`; sync to `docs/agents/*.md` |
| concrete task state, reproduction steps, validation recipes | `docs/tasks/zh-CN/*.md`; sync to `docs/tasks/*.md` |
| requirements and acceptance for one long task | a concrete `docs/tasks/zh-CN/<task>.md` note |
| milestones and TODOs for one long task | the same concrete task note, using `Top next action` and `Active items` sections |
| acceptance evidence | `Evidence` / `Validation` sections in the task note, or tests/docs when they are the durable evidence |
| failed explorations | `Failed explorations` in the task note, or a focused `docs/agents/zh-CN/*.md` pitfall when it is domain-wide |
| recent run log | avoid a permanent chat-style log by default; preserve only state changes that affect recovery |

## Fusion Levels

1. **Workflow-only**: update `horizon-workflow` when the lesson is a trigger, command route, forbidden action, or cross-agent behavior.
2. **Domain context**: update `docs/agents/zh-CN/*.md` when the lesson is stable architecture, ownership, lifecycle, validation, or recurring pitfall in one domain.
3. **Concrete task note**: update or create `docs/tasks/zh-CN/<task>.md` when the lesson is a reproducible checklist, active long-task state, validation recipe, or failed exploration tied to a task.
4. **Independent state system**: create `.agent-os/` only when the user explicitly wants a separate project-state document set for Horizon or for another target project.

## Horizon Task-State Shape

For a long-running Horizon task, prefer the template in `assets/templates/horizon-task-state.template.md`.

Keep it short. A future agent should be able to recover:

- what matters now
- the one next action
- what is blocked
- what evidence exists
- what failed and should not be blindly repeated
- what validation is still needed

## Validation

After modifying Horizon AI materials:

```powershell
.\tools\sync-agents.ps1 -Check
git diff --check
```

Run CMake or tests only when the change also touches C++, CMake, tools, examples, or behavior.
