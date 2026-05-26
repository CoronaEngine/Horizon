---
name: horizon-workflow
description: Vendor-neutral workflow for AI agents working in the Horizon C++ Vulkan repository. Use when an agent edits this repo, handles =sa/=ca/=gc/=cm/=gh commands, or needs routing to build, GitHub, formatting, Vulkan, Helicon, or push-constant context.
---
<!-- HORIZON_WORKFLOW_SKILL_ZH_CN_SHA256: be4bac2be599c38135b93cd2f298de064b86e5fab7d1e422e8e9a13628a9de5f -->

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
- `=gc`: run GitHub publication precheck only.
- `=cm`: commit intended changes to the current local branch only.
- `=gh`: commit intended changes and publish them to a GitHub PR.

For `=sa` and `=ca`, use `tools/sync-agents.ps1`.

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
