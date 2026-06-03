# Horizon Git Workflow Context
<!-- AGENT_DOCS_GIT_ZH_CN_SHA256: afcedbdb74a30e3197fda23c39c20ed0d8ee9ae233433acc685dabae3cf16aeb -->

Load this file only for `=gc`, `=cm`, `=gh`, commits, pushes, PRs, or GitHub publication work.

## `=gc` Check

Do not mutate anything.

1. Run `git status --short --branch`.
2. Inspect changed files and relevant diffs.
3. Run `.\tools\sync-agents.ps1 -Check` when AGENTS files exist.
4. Run the smallest relevant build only for C++, CMake, tooling, or example changes.
5. Report which files would be included and whether validation passed.
6. If only a local commit is needed, tell the user to run `=cm`; mention `=gh` only when the user explicitly needs a PR.

## `=cm` Local Commit

Do not push, create a PR, or merge branches.

1. Run `git status --short --branch`.
2. Inspect changed files and relevant diffs before staging.
3. Stage only intended files; do not default to `git add .`.
4. Run relevant validation before committing.
5. Commit with a concise conventional title and a Chinese body summary.
6. The body must explain what changed, why it changed, and what was verified.
7. Stop after creating the local commit.
8. Report branch, commit hash, and validation result.

## `=gh` Publish

1. Run `git status --short --branch`.
2. Inspect changed files and relevant diffs before staging.
3. Stage only intended files; do not default to `git add .`.
4. Run relevant validation before committing.
5. If uncommitted changes exist, create a local commit first using the `=cm` rules.
7. Push the current branch to `origin`.
8. Open a GitHub draft PR.
9. Report branch, commit hash, PR URL, and validation result.

## Commit Style

Use prefixes such as:

- `docs: ...`
- `build: ...`
- `tools: ...`
- `test: ...`
- `refactor: ...`
- `fix: ...`
- `feat: ...`

Example title:

```text
docs: 添加 AI 协作指南和自动化口令
```

Example body shape:

```text
新增 ...

调整 ...

验证：...
```

## Safety

- Never push if validation fails unless the user explicitly asks to continue.
- Never overwrite unrelated worktree changes.
- `=cm` never pushes or creates a PR.
- `=gh` uses a draft PR by default.
