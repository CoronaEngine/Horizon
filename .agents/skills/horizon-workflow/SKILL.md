---
name: horizon-workflow
description: Vendor-neutral workflow for AI agents working in the Horizon C++ Vulkan repository. Use when an agent edits this repo, handles =sa/=ca/=ai/=gc/=cm/=gh commands, or needs routing to build, GitHub, formatting, Vulkan, Helicon, or push-constant context.
---
<!-- HORIZON_WORKFLOW_SKILL_ZH_CN_SHA256: e3f4d4ce719a859797abe869aa1271f7bf54ced13d5c0f6cfab8ed415d13f43d -->

# Horizon Workflow

This skill is plain Markdown so Codex, Claude, Cursor, Gemini CLI, or other agents can use it.

## Start

1. Read root `AGENTS.md`.
2. Run or inspect `git status --short --branch` before edits.
3. Load only the relevant file from `docs/agents/`.
4. Keep changes scoped to the user request.
5. Report validation results.

## Command Routing

- `=sa`: sync all English agent files from Chinese sources.
- `=ca`: check whether all English agent files are synchronized with Chinese sources.
- `=ai`: distill durable project context from recent AI conversations into project AI materials.
- `=gc`: run GitHub publication precheck only.
- `=cm`: commit intended changes to the current local branch only.
- `=gh`: commit intended changes and publish them to a GitHub PR.

For `=sa` and `=ca`, use `tools/sync-agents.ps1`.

For `=ai`:

1. Inspect `git status --short --branch` first, and do not overwrite or revert existing user changes.
2. Extract reusable intent from recent conversations: user trigger phrases, the correct entrypoint, forbidden actions, validation commands, and settled designs.
3. Decide whether the material is worth preserving: it must reduce future misreads, shorten orientation, improve implementation speed, or lower hallucination risk. Do not preserve temporary guesses, one-off output, unresolved debates, casual chat, secrets, or overly narrow implementation details.
4. Write to the right owner:
   - Root repository rules: `AGENTS.zh-CN.md`.
   - Long-lived domain context: `docs/agents/zh-CN/*.md`.
   - Short task checklists, reproduction steps, and validation recipes: `docs/tasks/zh-CN/*.md`.
   - Shared workflow, commands, intent recognition, and cross-agent behavior: `.agents/skills/horizon-workflow/SKILL.zh-CN.md`.
5. Confirm evidence before writing: explicit user preferences, current code, verified commands, or settled designs. If evidence is weak, report candidates and why they were not preserved.
6. After changing a Chinese source, sync the English AI-facing file and run `tools/sync-agents.ps1 -Check`.

For `=gc`, `=cm`, and `=gh`, read `docs/agents/git.md` before acting.

## Context Routing

- Build or CMake: `docs/agents/build.md`
- Codegraph / symbol flow / call chains / impact: `docs/agents/codegraph.md`
- GitHub publish/PR/commit: `docs/agents/git.md`
- Formatting/style: `docs/agents/formatting.md`
- Vulkan backend: `docs/agents/vulkan.md`
- Helicon/codegen/reflection: `docs/agents/helicon.md`
- Push constants: `docs/agents/push-constants.md`

If a context pack is missing, inspect source files directly and keep the answer explicit about assumptions.

## AI Material and Skill Design

- Keep Horizon routing thin: root `AGENTS` only holds entry rules, long-lived domain context lives in `docs/agents/zh-CN/`, short task checklists and validation recipes live in `docs/tasks/zh-CN/`, and skills carry only strongly triggered workflows and intent recognition.
- Do not copy external repository AI frameworks wholesale. Before borrowing, compare repository structure, language source, sync mechanism, context size, and drift risk; discard frameworks that do not fit Horizon.
- When the user asks to absorb, initialize, adopt, or recover a project-state system, load `.agents/skills/agent-project-system/SKILL.md`; when the target is this Horizon repository, continue with its `references/horizon-adapter.md`. The complete generic system lives in that skill, while Horizon only wires in the adapted recovery entrypoint, single next action, evidence loop, failed explorations, and necessary TODO state.
- When multi-turn or long-running tasks need recoverable state, first use the Horizon adapter in `agent-project-system`; by default, do not create `.agent-os/` or `CLAUDE.md` at the Horizon root unless the user explicitly asks for a separate state system.
- Avoid turning skills into long API manuals. Put stable domain detail in `docs/agents/zh-CN/*.md`; put concrete reproduction steps or recurring tasks in `docs/tasks/zh-CN/*.md`.
- Add a new skill only when there is a clear trigger phrase, repeated workflow, or frequent-misread risk; every skill must have an accurate frontmatter `description`.
- Useful lightweight structures to borrow include `Common Mistakes`, `Key Paths`, `Validation`, and `Do` / `Do not` sections because they reduce misreads and hallucination.

## Non-Negotiables

- Do not revert user changes unless explicitly asked.
- Do not stage unrelated files.
- Do not default to `git add .`.
- Do not push failed validation unless the user explicitly asks to continue.
- Keep public API changes in `include/` deliberate and called out.
- Use `SKILL.zh-CN.md` for Chinese skill sources; do not place Chinese sources at `zh-CN/SKILL.md`, because many agents discover every `SKILL.md` as a separate skill.
