# Horizon Agent Contract

本文件是 Horizon 仓库的 AI 项目运行契约，也是恢复入口。`CLAUDE.md` 应与本文件保持同一内容；当前初始化脚本已尝试创建硬链接。

## Mission

- 让未来 agent 能从文件恢复项目状态，而不是依赖聊天历史。
- 把用户明确给出的目标、限制和验收标准作为最高优先级事实源。
- 对实现、验证、失败探索、阻塞和下一步保持可追溯记录。
- 当前仓库事实：Horizon 是一个 CMake/C++20 项目，包含核心 `src/`、`tools/`、`examples/`、`modules/ocarina/`、`src/hotfix/` 和 Helicon shader 编译相关 CMake。

## Recovery Order

1. 读本文件。
2. 读 `.agent-os/project-index.md`。
3. 读 index 中引用的 active / ready 项。
4. 读 `.agent-os/run-log.md` 的最近条目。
5. 需要时再读 `.agent-os/requirements.md`、`.agent-os/architecture-milestones.md`、`.agent-os/acceptance-report.md` 和 `.agent-os/lessons-learned.md`。

## Required Documents

项目状态目录是 `.agent-os/`，必须包含：

- `project-index.md`：当前真相、唯一全局下一步、活跃工作流、阻塞和读下一步。
- `requirements.md`：人类意图、需求、验收标准、非目标和硬限制。
- `change-decisions.md`：后续人类决策的追加记录。
- `architecture-milestones.md`：工作流、架构理解、里程碑和验收条件。
- `todo.md`：可执行工作项及状态。
- `acceptance-report.md`：证据账本，包括通过、失败和待验证。
- `lessons-learned.md`：失败探索、陷阱和重试条件。
- `run-log.md`：最近工作会话记录。

## Non-Negotiable Rules

1. 不得在没有明确人类决策的情况下改变用户目标、需求或验收含义。
2. 后续范围变更写入 `change-decisions.md`，不要悄悄改写原始需求。
3. 跨文档使用全局 typed item IDs，例如 `OBJ-001`、`REQ-001`、`MS-001`、`TD-001`、`EV-001`。
4. 没有证据时，不得声称已完成、已验证或满足验收。
5. 失败探索和阻塞必须保留，不能为了让进度看起来更顺而抹掉。
6. `project-index.md` 必须暴露一个全局 top next action。
7. 文档可以用中文；代码注释、脚本输出和用户可见运行日志里的新增代码文本默认用英文。
8. 编辑代码后，如果依赖关系发生变化，按项目约定运行 `codegraph sync` 更新依赖图。
9. 每次阶段性修改完成后，必须依次使用 `$clion-build` 构建 `HorizonExamples`，再使用 `$run-horizon-baseline` 启动 baseline 验证；两项都成功并记录证据后，才能声明该阶段完成。任何失败必须如实记录并继续处理或明确报告阻塞。

## Escalation Rules

只有在以下情况升级给用户：

- 仍有无法从仓库推断、且会影响方向的人类判断。
- 外部资源或环境硬阻塞导致无法继续。
- 多条探索路径失败，项目实质停滞。
- 用户目标或限制之间互相冲突。

## Update Discipline

当 TODO 状态变化、证据产生、阻塞出现或解除、里程碑重排、失败探索发生、或一次自主工作会话结束时，更新 `.agent-os/` 下相应文档。小改动做定向更新，不需要每次重写全套文档。
