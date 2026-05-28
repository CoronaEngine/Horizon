---
name: horizon-workflow
description: Vendor-neutral workflow for AI agents working in the Horizon C++ Vulkan repository. Use when an agent edits this repo, handles =sa/=ca/=ai/=gc/=cm/=gh commands, or needs routing to build, GitHub, formatting, Vulkan, Helicon, or push-constant context.
---
<!-- HORIZON_WORKFLOW_SKILL_ZH_CN_SHA256: d0552f31463ed4cad55c08958e738ac78aa55f41b1d5fb23dd6bcbb707f9e929 -->

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

For `=ai`, inspect `git status --short --branch` first. Preserve only stable, reusable project rules, architecture decisions, naming/lifetime/concurrency conventions, validation workflows, and user preferences. Do not preserve temporary guesses, one-off output, unresolved debates, casual chat, or secrets. Write to the right Chinese source file, sync the English AI-facing file, and run `tools/sync-agents.ps1 -Check`; if nothing is certain enough, report candidates and why they were not preserved.

For `=gc`, `=cm`, and `=gh`, read `docs/agents/git.md` before acting.

## Context Routing

- Build or CMake: `docs/agents/build.md`
- GitHub publish/PR/commit: `docs/agents/git.md`
- Formatting/style: `docs/agents/formatting.md`
- Vulkan backend: `docs/agents/vulkan.md`
- Helicon/codegen/reflection: `docs/agents/helicon.md`
- Push constants: `docs/agents/push-constants.md`

If a context pack is missing, inspect source files directly and keep the answer explicit about assumptions.

## Non-Negotiables

- Do not revert user changes unless explicitly asked.
- Do not stage unrelated files.
- Do not default to `git add .`.
- Do not push failed validation unless the user explicitly asks to continue.
- Keep public API changes in `include/` deliberate and called out.
- Use `SKILL.zh-CN.md` for Chinese skill sources; do not place Chinese sources at `zh-CN/SKILL.md`, because many agents discover every `SKILL.md` as a separate skill.
