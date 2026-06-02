---
name: agent-project-system
description: Initialize, adopt, recover, and govern an AI-first project state document system. Use when the user wants recoverable project docs, asks to absorb an external agent-project-system, needs milestone/TODO/acceptance-evidence/failed-exploration tracking, or wants long-running work recoverable from files.
---
<!-- AGENT_PROJECT_SYSTEM_SKILL_ZH_CN_SHA256: c176b1555864dda4ea2fccb80a6ebc440ce10f6dc2c4d4e5c99405ea9998f2da -->

# Agent Project System

This skill stores the full `agent-project-system` resources and adds Horizon-specific adaptation. Use it for new projects, external projects, research projects, or long-running state governance inside Horizon.

## Boundaries

- If the target is the Horizon repository itself, first obey root `AGENTS.md`, `.agents/skills/horizon-workflow/SKILL.md`, and the Chinese-source sync rules.
- Inside Horizon, do not replace root `AGENTS.md`, create a root `CLAUDE.md` hard link, or create `.agent-os/` by default; do that only when the user explicitly asks for a separate state system.
- Stable Horizon state should prefer existing repo-native materials: root rules in `AGENTS.zh-CN.md`, cross-agent workflow in `.agents/skills/horizon-workflow/SKILL.zh-CN.md`, domain context in `docs/agents/zh-CN/*.md`, and concrete long-task recovery summaries or validation recipes in `docs/tasks/zh-CN/*.md`.
- For a new or external project, the full model may create `AGENTS.md`, `CLAUDE.md`, `.agent-os/`, and the complete state document set.

## Triggers

- The user asks to "fully absorb", "distill into the project", "make the project recoverable", or build a project-state system.
- You need to reconstruct goals, current truth, next action, evidence, and failed explorations from existing code, docs, outputs, or chat.
- You need to maintain milestones, TODOs, acceptance evidence, lessons learned, or a run log over time.
- The project should become recoverable from files instead of depending on chat history.

## Workflow

1. Identify the target: inside Horizon, another existing project, or a brand-new project.
2. If the target is the Horizon repository itself, read `references/horizon-adapter.md` first and map into Horizon's native `AGENTS`, `docs/agents`, `docs/tasks`, workflow skill, and `sync-agents` framework.
3. For other targets, read `references/workflow.md` first. Before generating or revising a document set, read `references/document-contract.md`. Before recovery, status updates, or autonomous continuation, read `references/runtime-governance.md`.
4. For new-project initialization, prefer `scripts/init_project_system.py` and `assets/templates/`.
5. For Horizon long-task state, prefer the shape in `assets/templates/horizon-task-state.template.md` and place it in a concrete `docs/tasks/zh-CN/<task>.md`.
6. When allocating a new item ID, use `scripts/allocate_item_id.py` instead of guessing the next number by hand.
7. Validate a complete generic state system with `scripts/validate_project_system.py`; note that this script validates the generic `.agent-os` model, not Horizon's existing AI-doc layout.
8. Completion, acceptance, or publication claims must have evidence. Without evidence, mark the state as hypothesis, unverified, blocked, or partial.

## Horizon Minimal State

When a long-running Horizon task needs recoverable state but not a separate `.agent-os/`, a concrete `docs/tasks/zh-CN/*.md` note can use these short sections:

- `当前事实`: goal, scope, constraints, and current state.
- `Top next action`: one global next step.
- `Active items`: only backlog / doing / blocked / done / verified / abandoned items that still affect later work.
- `Evidence`: validation commands, output summaries, related files, or artifact paths.
- `Failed explorations`: failed attempts future agents might repeat, rejection reasons, and retry conditions.
- `Validation`: the smallest checks to run on the next pass or before publication.

Use lightweight IDs such as `TD-001`, `EV-001`, or `EXP-001` only when cross-section or cross-file references reduce ambiguity; do not force Horizon into the generic global state machine.

## Resources

- `references/workflow.md`: initialization, adoption, recovery, and runtime governance workflow.
- `references/document-contract.md`: document roles, global item IDs, state machine, and single next action rule.
- `references/runtime-governance.md`: runtime update triggers, truth labels, and failed-exploration loop.
- `references/horizon-adapter.md`: adapter rules for mapping the generic project-state system into Horizon's native AI framework.
- `scripts/init_project_system.py`: scaffold a generic project state document set.
- `scripts/allocate_item_id.py`: scan existing docs and allocate the next typed item ID.
- `scripts/validate_project_system.py`: validate a generic project state document system.
- `assets/templates/`: generic state document templates.
- `assets/templates/horizon-task-state.template.md`: minimal Horizon long-task state template.

## Do Not

- Do not paste raw chat transcripts into docs; distill stable intent, state, evidence, failed explorations, and next action.
- Do not copy a full parallel document tree into Horizon just to look complete when the sync-managed AI materials are the better owner.
- Do not mark unverified work as complete.
- Do not erase failed explorations; keep them when they can prevent repeated future mistakes.
