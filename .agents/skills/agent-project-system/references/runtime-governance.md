# Runtime Governance

## Goal

Keep the project document system accurate enough that any future agent can resume work from files, while avoiding heavy ceremony on every minor change.

## When To Update

Update the state docs when one of these happens:

- a new workstream is introduced
- a milestone is added, changed, completed, or abandoned
- a TODO changes state
- evidence is produced
- a blocker appears or clears
- an exploration fails
- a work session ends

## What To Update

Minimum expected updates by event:

- new TODO: `todo.md`, maybe `project-index.md`
- state transition: `todo.md`, `project-index.md`, maybe `run-log.md`
- blocker: `todo.md`, `project-index.md`, maybe `lessons-learned.md`
- evidence: `acceptance-report.md`, maybe `project-index.md`
- failed exploration: `lessons-learned.md`, maybe `todo.md`
- milestone completion: `architecture-milestones.md`, `acceptance-report.md`, `project-index.md`
- work session end: `run-log.md`

## Truthfulness

Allowed labels for uncertain reality:

- hypothesis
- suspected
- partial
- unverified
- blocked

Disallowed behavior:

- claiming a target is met without evidence
- collapsing failed attempts into silence
- marking TODOs complete before updating evidence and bookkeeping

## Minimal Closed Loop For Failed Exploration

For each failed exploration, capture at least:

- item ID
- motivation
- method
- observed result
- failure or rejection reason
- retry condition

This prevents future agents from repeating blind loops.

## Recovery Quality Standard

A recovery attempt should be able to answer these in under a few minutes of reading:

- what matters most right now
- what is actively blocked
- what has already been tried
- what success means
- what should happen next

If the current document set cannot answer those questions quickly, the document system is stale and should be repaired before further autonomous execution.
