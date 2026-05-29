# Horizon Agent 上下文索引

本目录是 `docs/agents/*.md` 的中文源。修改上下文包时，先改这里，再同步英文文件。

全局中文源还有：

- `AGENTS.zh-CN.md`
- `docs/tasks/zh-CN/*.md`
- `.agents/skills/horizon-workflow/SKILL.zh-CN.md`

英文文件是 Agent 默认读取入口或按需任务说明：

- `docs/agents/index.md`
- `docs/agents/build.md`
- `docs/agents/git.md`
- `docs/agents/formatting.md`
- `docs/agents/vulkan.md`
- `docs/agents/helicon.md`
- `docs/agents/push-constants.md`
- `docs/tasks/*.md`

共享工作流 skill：

- `.agents/skills/horizon-workflow/SKILL.md`

保持每个上下文包短小稳定。优先新增小文件，不要把细节塞回 `AGENTS.md`。

不要在 `.agents/skills/` 下创建 `zh-CN/SKILL.md` 作为中文源，避免被 Agent 识别成重复 skill。
