# Horizon Git 工作流上下文

仅在处理 `=gc`、`=cm`、`=gh`、commit、push、PR 或 GitHub 发布时加载。

## `=gc` 预检

不要修改任何内容。

1. 运行 `git status --short --branch`。
2. 检查改动文件和相关 diff。
3. 如果存在 AGENTS 文件，运行 `.\tools\sync-agents.ps1 -Check`。
4. 只有 C++、CMake、工具或示例改动才运行最小相关 build。
5. 汇报哪些文件会被纳入提交，以及验证是否通过。
6. 如果只需要本地提交，提示用户运行 `=cm`；只有用户明确需要 PR 时才提示 `=gh`。

## `=cm` 本地提交

不要推送，不要创建 PR，不要合并分支。

1. 运行 `git status --short --branch`。
2. 暂存前检查改动文件和相关 diff。
3. 只暂存目标文件，不要默认 `git add .`。
4. 提交前运行相关验证。
5. commit 使用简洁 conventional 标题和中文正文摘要。
6. 正文必须说明改了什么、为什么改、验证了什么。
7. 创建本地 commit 后停止。
8. 汇报分支、commit hash 和验证结果。

## `=gh` 发布

1. 运行 `git status --short --branch`。
2. 暂存前检查改动文件和相关 diff。
3. 只暂存目标文件，不要默认 `git add .`。
4. 提交前运行相关验证。
5. 如果存在未提交改动，先按 `=cm` 规则创建本地 commit。
7. 推送当前分支到 `origin`。
8. 创建 GitHub draft PR。
9. 汇报分支、commit hash、PR URL 和验证结果。

## Commit 风格

可用前缀：

- `docs: ...`
- `build: ...`
- `tools: ...`
- `test: ...`
- `refactor: ...`
- `fix: ...`
- `feat: ...`

标题示例：

```text
docs: 添加 AI 协作指南和自动化口令
```

正文形状示例：

```text
新增 ...

调整 ...

验证：...
```

## 安全规则

- 验证失败时不要推送，除非用户明确要求继续。
- 不要覆盖无关工作区改动。
- `=cm` 永远不推送、不创建 PR。
- `=gh` 默认创建 draft PR。
