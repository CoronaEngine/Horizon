---
name: agent-project-system
description: 初始化、吸收、恢复和治理 AI-first 项目状态文档系统。用户要求搭建可恢复项目文档、吸收外部 agent-project-system、维护里程碑/TODO/验收证据/失败探索，或让长任务可由文件恢复时使用。
---

# Agent Project System

本 skill 保存完整的 `agent-project-system` 资源，并提供 Horizon 适配。它可以用于新项目、外部项目、研究项目或 Horizon 内的长任务状态治理。

## 使用边界

- 若目标是 Horizon 仓库本身，先遵守根 `AGENTS.md`、`.agents/skills/horizon-workflow/SKILL.md` 和中文源同步规则。
- 在 Horizon 内不要默认替换根 `AGENTS.md`、创建根 `CLAUDE.md` 硬链接或新建 `.agent-os/`；只有用户明确要求独立状态系统时才这样做。
- Horizon 内的稳定状态优先进入现有 repo-native 材料：根规则写 `AGENTS.zh-CN.md`，跨 Agent 工作流写 `.agents/skills/horizon-workflow/SKILL.zh-CN.md`，领域上下文写 `docs/agents/zh-CN/*.md`，具体长任务恢复摘要或验证配方写 `docs/tasks/zh-CN/*.md`。
- 若目标是新项目或外部项目，可以按本 skill 的完整模型创建 `AGENTS.md`、`CLAUDE.md`、`.agent-os/` 和状态文档集。

## 触发场景

- 用户要求“全部吸收”“沉淀进项目”“让项目可恢复”“搭建项目状态系统”。
- 需要从已有代码、文档、输出或对话中重建项目目标、当前事实、下一步、证据和失败探索。
- 需要长期维护 milestones、TODO、acceptance evidence、lessons learned、run log。
- 需要把项目从聊天依赖转成文件可恢复状态。

## 工作流

1. 识别目标位置：Horizon 仓库内、另一个已有项目，还是全新项目。
2. 如果目标是 Horizon 仓库本身，先读 `references/horizon-adapter.md`，按其中映射进入 Horizon 自带的 `AGENTS`、`docs/agents`、`docs/tasks`、workflow skill 和 `sync-agents` 框架。
3. 其他目标先读 `references/workflow.md`；生成或修改文档集前读 `references/document-contract.md`；处理恢复、状态更新或自治推进前读 `references/runtime-governance.md`。
4. 新项目初始化优先使用 `scripts/init_project_system.py` 和 `assets/templates/`。
5. Horizon 长任务状态优先使用 `assets/templates/horizon-task-state.template.md` 的形状落入具体 `docs/tasks/zh-CN/<task>.md`。
6. 需要新 item ID 时使用 `scripts/allocate_item_id.py`，不要手写猜下一个编号。
7. 验证完整状态系统时使用 `scripts/validate_project_system.py`；注意该脚本校验的是通用 `.agent-os` 模型，不是 Horizon 现有 AI 文档布局。
8. 任何完成、验收或发布结论都必须有证据；没有证据时标为 hypothesis、unverified、blocked 或 partial。

## Horizon 最小状态形状

当 Horizon 的某个长任务需要可恢复状态，但不需要独立 `.agent-os/` 时，具体 `docs/tasks/zh-CN/*.md` 可使用这些短小节：

- `当前事实`：目标、范围、约束和当前状态。
- `Top next action`：一个全局下一步。
- `Active items`：只保留仍会影响后续工作的 backlog / doing / blocked / done / verified / abandoned 条目。
- `Evidence`：验证命令、输出摘要、相关文件或产物路径。
- `Failed explorations`：未来 Agent 可能重复踩到的失败尝试、拒绝原因和重试条件。
- `Validation`：下一轮或发布前的最小检查。

只有跨小节或跨文件引用确实降低歧义时才使用 `TD-001`、`EV-001`、`EXP-001` 等轻量 ID；不要把 Horizon 强行改造成通用全局状态机。

## 资源

- `references/workflow.md`：初始化、采纳、恢复和运行期治理流程。
- `references/document-contract.md`：文档职责、全局 item ID、状态机和单一下一步规则。
- `references/runtime-governance.md`：运行期更新条件、真实性标签和失败探索闭环。
- `references/horizon-adapter.md`：把通用项目状态系统映射到 Horizon 自带 AI 框架的适配规则。
- `scripts/init_project_system.py`：创建通用项目状态文档集。
- `scripts/allocate_item_id.py`：扫描现有文档并分配下一个 typed item ID。
- `scripts/validate_project_system.py`：校验通用项目状态文档系统。
- `assets/templates/`：通用状态文档模板。
- `assets/templates/horizon-task-state.template.md`：Horizon 长任务状态最小模板。

## 不要做

- 不要把原始聊天逐字搬进文档；只提炼稳定意图、状态、证据、失败探索和下一步。
- 不要为了“看起来完整”在 Horizon 内复制一整套不受同步脚本管理的并行文档树。
- 不要把没有验证的完成状态写成已完成。
- 不要删除失败探索；如果它能阻止未来重复误判，就保留下来。
