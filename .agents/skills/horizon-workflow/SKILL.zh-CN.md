---
name: horizon-workflow
description: Horizon C++ Vulkan 仓库的通用 Agent 工作流。Agent 修改本仓库、处理 =sa/=ca/=gc/=cm/=gh 口令，或需要路由到 build、GitHub、formatting、Vulkan、Helicon、push-constant 上下文时使用。
---

# Horizon Workflow

本 skill 是普通 Markdown，Codex、Claude、Cursor、Gemini CLI 或其他 Agent 都可以读取。

## 开始

1. 阅读根目录 `AGENTS.md`。
2. 编辑前运行或查看 `git status --short --branch`。
3. 只加载相关的 `docs/agents/` 文件。
4. 改动范围保持贴合用户请求。
5. 汇报验证结果。

## 口令路由

- `=sa`：根据所有中文源同步英文 Agent 文件。
- `=ca`：只检查所有英文 Agent 文件是否与中文源同步。
- `=ai`：把近期 AI 对话中值得长期保留的项目上下文沉淀到项目 AI 资料中。
- `=gc`：只做 GitHub 发布预检。
- `=cm`：只提交目标改动到当前本地分支。
- `=gh`：提交目标改动并发布到 GitHub PR。

`=sa` 和 `=ca` 使用 `tools/sync-agents.ps1`。

`=ai` 执行时，先查看 `git status --short --branch`，只沉淀稳定、可复用的项目规则、架构决策、命名/生命周期/并发约定、验证流程和用户偏好。不要沉淀临时猜测、一次性输出、未定论争议、闲谈或秘密信息。按归属写入中文源文件，再同步英文 AI 文件并运行 `tools/sync-agents.ps1 -Check`；如果没有足够确定的内容，只汇报候选项和不沉淀的理由。

`=gc`、`=cm` 和 `=gh` 执行前读取 `docs/agents/git.md`。

## 上下文路由

- 构建或 CMake：`docs/agents/build.md`
- GitHub publish / PR / commit：`docs/agents/git.md`
- 格式化 / 风格：`docs/agents/formatting.md`
- Vulkan 后端：`docs/agents/vulkan.md`
- Helicon / codegen / reflection：`docs/agents/helicon.md`
- Push constants：`docs/agents/push-constants.md`

如果上下文包不存在，直接检查源码，并在回答里说明假设。

## 硬规则

- 不要回滚用户改动，除非用户明确要求。
- 不要暂存无关文件。
- 不要默认 `git add .`。
- 验证失败时不要推送，除非用户明确要求继续。
- `include/` 下公共 API 改动必须明确说明影响。
- 中文 skill 源文件使用 `SKILL.zh-CN.md`，不要放在 `zh-CN/SKILL.md`，避免被 Agent 识别为重复 skill。
