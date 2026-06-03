# Document Contract

## Directory Layout

Recommended layout:

```text
<project-root>/
├─ AGENTS.md
├─ CLAUDE.md
└─ .agent-os/
   ├─ project-index.md
   ├─ requirements.md
   ├─ change-decisions.md
   ├─ architecture-milestones.md
   ├─ todo.md
   ├─ acceptance-report.md
   ├─ lessons-learned.md
   └─ run-log.md
```

`AGENTS.md` must live at the project root. `CLAUDE.md` should be a hard link to the same file when possible. The state directory name may vary, but the document roles should stay stable.

## Document Responsibilities

### `AGENTS.md`

Project operating contract.

Must define:

- recovery order
- document responsibilities
- required update discipline
- truthfulness guardrail
- escalation conditions
- language and code-comment conventions

### `project-index.md`

Current truth and fast recovery entrypoint.

Must contain:

- current objective summary
- active workstreams
- current top next action
- active blockers
- recent important changes
- pointers to the currently relevant deeper documents

### `requirements.md`

Human intent truth source.

Must contain:

- goals
- requirements
- acceptance criteria
- non-goals
- hard constraints

Rules:

- preserve user meaning
- allow faithful cleanup and formatting
- do not silently add assumptions

### `change-decisions.md`

Append-only record of later human decisions or scope clarifications that change interpretation without rewriting `requirements.md`.

### `architecture-milestones.md`

Current execution design.

Must contain:

- workstreams
- architecture or research route
- milestone list
- milestone acceptance conditions
- rationale for major changes when replanned

### `todo.md`

Executable work inventory.

Each TODO should be the smallest independently completable and independently verifiable work item that still matters.

Recommended sections:

- backlog
- ready
- doing
- blocked
- done
- verified
- abandoned

### `acceptance-report.md`

Per-item evidence ledger.

Must record both:

- passed checks
- failed or not-yet-passed checks

Every conclusion should point to evidence.

### `lessons-learned.md`

Exploration and trap memory.

Each exploration record should answer:

- why it was attempted
- what was tried
- what happened
- why it failed or was not selected
- when it is worth retrying

### `run-log.md`

Lightweight recent history for recovery.

Each entry should say:

- what the agent worked on
- what state changed
- what evidence or artifacts were produced
- what the next likely action is

## Global Item IDs

Use typed item IDs across all documents.

Recommended prefixes:

- `OBJ`
- `REQ`
- `AC`
- `WS`
- `MS`
- `TD`
- `RSK`
- `EXP`
- `EV`
- `CD`

Example:

- `REQ-003`
- `MS-002`
- `TD-014`

Rules:

- IDs are globally unique by prefix and numeric suffix
- references between documents should use item IDs, not bare prose
- document section numbers are optional and separate from item IDs

## State Machine

Default TODO and milestone states:

- `backlog`
- `ready`
- `doing`
- `blocked`
- `done`
- `verified`
- `abandoned`

State rules:

- every transition needs a reason
- `done` is not the same as `verified`
- `abandoned` requires replacement or retry guidance when applicable

## Single Next Action Rule

Even in multi-workstream projects, `project-index.md` should expose one global top next action. Parallelism may exist, but the current priority order should still be explicit.
