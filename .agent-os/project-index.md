# Project Index

## Current Truth

- Objective: `OBJ-003`
- Top next action: `TD-005`
- Active workstreams: `WS-003`
- Active blockers: `RSK-001`

## Objective Summary

- `OBJ-001`: 为 Horizon 仓库建立可恢复的 AI 项目文档系统，使未来 agent 能从 `AGENTS.md` 和 `.agent-os/` 恢复当前事实、工作项、证据和下一步。
- `OBJ-002` `[unverified]`: Horizon 的长期产品/工程目标尚未由用户在本文档系统中确认；当前只能从仓库结构推断它是 CMake/C++20 的 Horizon/Helicon 渲染与运行时基础库项目。
- `OBJ-003` `[active]`: 在 `HorizonExamples` 的 `example_baseline` 中接入 hotfix 测试，通过 H 键触发文件修改检测、自动 reload，并更新当前进程中的行为。

## Active Workstreams

- `WS-001` `[verified]`: 项目文档系统初始化与接管。根契约、状态文档、验证脚本和硬链接检查已完成。
- `WS-002` `[backlog]`: Horizon 构建与集成状态治理。围绕 `HORIZON_BUILD_EXAMPLES`、`HORIZON_BUILD_OCARINA`、`HORIZON_BUILD_VISION_HOTFIX`、Helicon shader 编译和 CoronaEngine 集成关系维护可验证状态。
- `WS-003` `[active]`: `HorizonExamples` baseline hotfix 交互测试接入；CMake target 依赖和 H 键日志入口已完成，检测/reload 待实现。

## Top Next Action

- `TD-005` `[ready]`: 在 `example_baseline` 中实现 H 键触发的 hotfix 检测、自动 reload 和行为更新。

## Active Blockers

- `RSK-001` `[human-decision]`: Horizon 的长期目标、验收边界和优先级尚未锁定。文档系统已可用于恢复，但不能把任何具体产品路线当作已确认事实。

## Recent Important Changes

- 2026-07-19：初始化 `.agent-os/` 文档系统，创建根 `AGENTS.md` 和硬链接 `CLAUDE.md`，验证脚本通过。
- 2026-07-19：`HorizonExamples` 在启用 Vision hotfix 时链接 `vision-hotfix-all`；完整 CMake 配置和 Ninja 依赖图检查通过。
- 2026-07-19：新增仓库级 `clion-build` skill，复用 CLion Debug 构建树；实际构建暴露 MSVC D8016 字符集选项冲突。
- 2026-07-19：新增 `run-horizon-baseline` skill；已验证能够以 baseline 参数启动 Debug 示例并返回 PID。
- 2026-07-19：新增阶段双 skill 验证门禁；统一 `/utf-8` 后构建与 baseline 启动均通过，D8016 阻塞解除。
- 2026-07-19：baseline 注册 H 键 press 回调并打印 `hotfix`；构建和启动门禁通过，真实按键输出待人工确认。

## Read Next

- `.agent-os/requirements.md`
- `.agent-os/architecture-milestones.md`
- `.agent-os/todo.md`
- `.agent-os/acceptance-report.md`
- `.agent-os/run-log.md`
