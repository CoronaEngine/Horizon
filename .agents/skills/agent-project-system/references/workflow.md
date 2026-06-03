# Workflow

## Purpose

This skill creates and maintains a project operating system for AI agents. The system must preserve intent, current state, history, evidence, and recovery order without depending on the original chat thread.

## Modes

### Mode 1: New Project Initialization

Use this mode when the project does not yet have the document system.

Operating rules:

1. Ask until every human-owned decision is explicit.
2. Distinguish user-owned decisions from agent-owned design choices.
3. Put user-owned truth into `requirements.md`.
4. Put agent-owned execution structure into the rest of the system.
5. End initialization only when the first complete document set exists and there are no unresolved human-decision gaps.

Human-owned decisions usually include:

- final goal
- required outputs
- acceptance boundaries
- hard constraints
- prohibited directions
- mandatory resource or environment limits

Agent-owned decisions usually include:

- architecture
- workstream decomposition
- milestone ordering
- TODO breakdown
- evidence collection shape
- retry strategy

### Mode 2: Existing Project Adoption

Use this mode when code, notes, outputs, or partial docs already exist.

Operating rules:

1. Audit first.
2. Infer what can be discovered from the project itself.
3. Ask only what remains ambiguous and matters to intent.
4. Rebuild the document system around actual project reality.
5. Prefer a clean reconstruction over preserving fragmented or inconsistent state docs.

Audit checklist:

- repository or folder structure
- build or runtime entrypoints
- existing docs
- explicit TODOs and issue lists
- output artifacts and experiments
- current blockers
- likely active workstreams
- existing acceptance evidence

### Mode 3: Recovery

Use this mode whenever work is resumed after interruption or handed off to a fresh agent.

Recovery order:

1. root `AGENTS.md`
2. state `project-index.md`
3. active items referenced by the index
4. newest `run-log.md` entries
5. deeper reads only if needed

The goal is fast reconstruction of the current truth, not full historical rereading.

### Mode 4: Runtime Governance

Use this mode during normal autonomous progress.

Update the document system whenever there is a material state change:

- new TODO
- state transition
- blocker
- milestone completion or replanning
- fresh evidence
- failed exploration
- end of an autonomous work session

Do not force heavy full-document rewrites on every small task. Prefer targeted updates.

## Escalation Rules

Escalate to the human only when one of these is true:

- a question still requires human judgment and cannot be inferred
- a hard external blocker prevents progress
- many exploration paths have failed and the project is no longer moving
- the user-specified goal appears internally inconsistent or impossible under stated constraints

Do not escalate merely because architecture or implementation choices are difficult. Those are usually the agent's responsibility.

## Truth Model

Never claim success without evidence. If a result is only suspected, mark it as a hypothesis or an unverified state.

Evidence may include:

- test commands
- output summaries
- file paths
- experiment settings
- benchmark snapshots
- cited external sources
- artifact locations

## Required Deliverable Standard

After initialization or adoption, the project must be resumable from files alone. A future agent should not need the original chat transcript to know:

- what the project is trying to achieve
- what constraints are fixed
- what is in progress
- what has already been tried
- what evidence exists
- what should happen next
