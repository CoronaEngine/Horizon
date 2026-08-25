# Horizon Enum PascalCase Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename Horizon enum members to PascalCase while preserving enum types, values, behavior, and `EPort::{H, D}`.

**Architecture:** Apply deterministic member mappings to the 16 nonconforming enums in the five Horizon source directories. Update qualified references directly and repair DSL preprocessor token-pasting so generated `UnaryOp`, `BinaryOp`, and `CallOp` identifiers use the renamed members.

**Tech Stack:** C++20, CMake, Ninja Multi-Config, PowerShell, MSVC preprocessor/compiler.

**Spec:** User-approved requirements in the current Codex thread.

## Global Constraints

- Limit source changes to `src/core`, `src/math`, `src/ast`, `src/dsl`, and `src/runtime`.
- Preserve `EPort`, `H`, and `D` exactly.
- Preserve enum declaration order, underlying types, and explicit numeric values.
- Update token-pasting macros and their arguments, not only direct references.
- Do not touch `modules/ocarina` or unrelated user changes.
- Do not commit, push, or create a PR without explicit user authorization.

---

### Task 1: Rename declarations and direct references

**Files:**
- Modify: enum declaration headers and their direct users under the five Horizon source directories.
- Test: repository-wide source audit restricted to the five Horizon directories.

**Interfaces:**
- Consumes: existing enum types and numeric values.
- Produces: the same enum types with PascalCase member identifiers.

- [x] Record the existing non-Pascal enum-member audit and confirm it finds the known declarations.
- [x] Rename declaration members using the approved acronym and dimension rules.
- [x] Rename scoped and class-local member references without changing enum values.
- [x] Verify that `EPort::{H, D}` remains unchanged.

### Task 2: Repair generated enum references

**Files:**
- Modify: `src/dsl/api/operators.h`
- Modify: `src/dsl/api/builtin.h`
- Modify: `src/dsl/core/var.h`
- Modify: other DSL headers containing direct generated-operation references.

**Interfaces:**
- Consumes: PascalCase `UnaryOp`, `BinaryOp`, and `CallOp` members from Task 1.
- Produces: macros whose token-pasting expands to the exact renamed identifiers.

- [x] Change macro tag arguments from uppercase enum spellings to PascalCase spellings.
- [x] Update vector and matrix token-pasting to generate `MakeFloat2` and `MakeFloat2x2`-style identifiers.
- [x] Audit all `##` sites that participate in enum-member construction.
- [x] Run a preprocessor/compile check through the affected targets.

### Task 3: Verify the refactor

**Files:**
- Verify: all modified source files.

**Interfaces:**
- Consumes: renamed declarations, references, and macro expansions.
- Produces: evidence that no old member reference remains and current targets still build.

- [x] Scan enum declarations for remaining non-Pascal members, excluding `EPort::{H, D}` and already compliant enums.
- [x] Scan qualified enum references and macro arguments for stale uppercase/lowercase spellings.
- [x] Run formatting/diff checks, relevant configured builds, and available tests.
- [x] Report any validation boundary where a source directory is not connected to a current CMake target.
