---
name: horizon-workflow
description: Horizon C++ Vulkan 仓库的通用 Agent 工作流。Agent 修改本仓库、处理 =sa/=ca/=ai/=gc/=cm/=gh 口令，或需要路由到 build、GitHub、formatting、Vulkan、Helicon、push-constant 上下文时使用。
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

`=ai` 执行时：

1. 先查看 `git status --short --branch`，不要覆盖或回滚用户已有改动。
2. 从近期对话中提取可复用意图：用户触发词、正确入口、禁止动作、验证命令、已落地设计。
3. 先判断是否值得沉淀：内容必须能在下一轮减少误判、缩短定位、提高动手效率，或降低幻觉风险。不要沉淀临时猜测、一次性输出、未定论争议、闲谈、秘密信息或过窄实现细节。
4. 按归属写入：
   - 根仓库规则：`AGENTS.zh-CN.md`。
   - 长期领域上下文：`docs/agents/zh-CN/*.md`。
   - 短小任务清单、复现步骤、验证配方：`docs/tasks/zh-CN/*.md`。
   - 共享工作流、口令、意图识别、跨 Agent 行为：`.agents/skills/horizon-workflow/SKILL.zh-CN.md`。
5. 写入前确认事实依据来自用户明确偏好、当前代码、已验证命令或已落地设计；证据不足时只汇报候选项和不沉淀理由。
6. 修改中文源后同步英文 AI 文件，并运行 `tools/sync-agents.ps1 -Check`。

`=gc`、`=cm` 和 `=gh` 执行前读取 `docs/agents/git.md`。

## 上下文路由

- 构建或 CMake：`docs/agents/build.md`
- Codegraph / 符号流 / 调用链 / 影响面：`docs/agents/codegraph.md`
- GitHub publish / PR / commit：`docs/agents/git.md`
- 格式化 / 风格：`docs/agents/formatting.md`
- Vulkan 后端：`docs/agents/vulkan.md`
- Helicon / codegen / reflection：`docs/agents/helicon.md`
- Push constants：`docs/agents/push-constants.md`

如果上下文包不存在，直接检查源码，并在回答里说明假设。

## AI 资料和 Skill 设计

- Horizon 保持薄路由：根 `AGENTS` 只放入口规则，长期领域上下文放 `docs/agents/zh-CN/`，短任务清单和验证配方放 `docs/tasks/zh-CN/`，skill 只承载强触发的工作流和意图识别。
- 不要整体照搬外部仓库的 AI 框架。借鉴前先比较仓库结构、语言源、同步机制、上下文体量和过期风险；不适合 Horizon 的框架应舍弃。
- 用户要求吸收、初始化、采纳或恢复项目状态系统时，加载 `.agents/skills/agent-project-system/SKILL.md`，目标是 Horizon 本仓库时继续读取其 `references/horizon-adapter.md`；完整通用系统放在该 skill 中，Horizon 本仓库只接入适配后的恢复入口、唯一下一步、证据闭环、失败探索和必要 TODO 状态。
- 多轮或长任务需要可恢复状态时，先使用 `agent-project-system` 的 Horizon 适配规则；默认不要在 Horizon 根目录创建 `.agent-os/` 或 `CLAUDE.md`，除非用户明确要求独立状态系统。
- 避免把 skill 写成长篇 API 手册。稳定领域细节优先沉淀到 `docs/agents/zh-CN/*.md`，具体复现步骤或常见任务优先沉淀到 `docs/tasks/zh-CN/*.md`。
- 只有存在明确触发词、重复工作流或高频误判风险时才新增 skill；每个 skill 必须有准确的 frontmatter `description`。
- 可借鉴的轻量结构包括 `Common Mistakes`、`Key Paths`、`Validation`、`Do` / `Do not`，用于减少误判和幻觉。

## 硬规则

- 不要回滚用户改动，除非用户明确要求。
- 不要暂存无关文件。
- 不要默认 `git add .`。
- 验证失败时不要推送，除非用户明确要求继续。
- `include/` 下公共 API 改动必须明确说明影响。
- 中文 skill 源文件使用 `SKILL.zh-CN.md`，不要放在 `zh-CN/SKILL.md`，避免被 Agent 识别为重复 skill。
