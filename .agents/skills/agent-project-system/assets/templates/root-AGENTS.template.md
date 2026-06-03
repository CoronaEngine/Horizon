# AGENTS.md

This file is the root operating contract for the project.

## Mission

- Preserve the user-defined goal and acceptance boundaries as the highest-priority truth source.
- Keep the project recoverable from documents alone.
- Maintain truthful state, evidence, failures, and next-action visibility for future agents.

## Recovery Order

1. Read this file.
2. Read `[state-dir]/project-index.md`.
3. Read the active items referenced there.
4. Read `[state-dir]/run-log.md`.
5. Dive deeper only as needed.

## Required Documents

The project state directory must contain:

- `project-index.md`
- `requirements.md`
- `change-decisions.md`
- `architecture-milestones.md`
- `todo.md`
- `acceptance-report.md`
- `lessons-learned.md`
- `run-log.md`

## Non-Negotiable Rules

1. Do not change the user-defined goal, requirements, or acceptance meaning without an explicit human decision.
2. Record human decisions in `change-decisions.md` instead of silently rewriting the original requirements.
3. Use typed global item IDs across the document set.
4. Do not claim completion or verification without evidence.
5. Failed explorations and blocked paths must remain visible.
6. Keep exactly one global top next action in `project-index.md`.
7. If project documents are in Chinese, code comments and `print` output must still be in English.

## Escalation Rules

Escalate to the human only when:

- a required human judgment is still unresolved
- a hard external blocker prevents progress
- repeated exploration has failed and progress has effectively stalled
- the user's stated constraints appear mutually incompatible

## Update Discipline

Update the state docs whenever there is a meaningful change in TODO state, evidence, blockers, milestones, or failed explorations.
