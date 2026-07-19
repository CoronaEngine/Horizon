# Architecture And Milestones

## Current Design

- 文档系统采用根入口 `AGENTS.md` + `.agent-os/` 状态目录。
- 当前仓库观察：Horizon 是 CMake/C++20 项目，顶层 `CMakeLists.txt` 暴露 `HORIZON_BUILD_TOOLS`、`HORIZON_BUILD_EXAMPLES`、`HORIZON_BUILD_TESTS`、`HORIZON_BUILD_OCARINA`、`HORIZON_BUILD_VISION_HOTFIX` 等构建开关。
- `examples/HorizonExamples` 是示例可执行程序，默认由 `HORIZON_BUILD_EXAMPLES` 控制；它链接 `glm`、`glfw`、`Horizon`，并触发 shader 复制、runtime deps 复制和 `helicon_compile_shaders`。启用 Vision hotfix 时，顶层 CMake 还会链接 `vision-hotfix-all`。
- `src/hotfix/` 是 Vision hotfix 支撑模块，受 `HORIZON_BUILD_VISION_HOTFIX` 控制，并要求 ocarina 目标可用。
- baseline GLFW window 已注册 H 键回调；当前 H 只输出 `hotfix`，后续再接入文件检测和 reload。
- CoronaEngine 仓库会通过 FetchContent 拉取 Horizon，但该跨仓库集成目标属于 `OBJ-002` 的待确认范围。

## Workstreams

- `WS-001` `[verified]`: 文档系统初始化与接管。
- `WS-002` `[backlog]`: 构建/示例/hotfix/CoronaEngine 集成状态治理。
- `WS-003` `[active]`: `HorizonExamples` baseline hotfix 交互测试接入。

## Milestones

- `MS-001` `[verified]`: 建立最小可恢复文档系统。
  - Related workstreams: `WS-001`
  - Acceptance: `AC-001` 到 `AC-005` 由 `EV-001`、`EV-003`、`EV-005` 支撑。
- `MS-002` `[ready]`: 锁定 Horizon 的长期工程目标和近期优先级。
  - Related workstreams: `WS-001`, `WS-002`
  - Acceptance: 用户确认 `OBJ-002` 的目标范围、非目标和优先验收边界；结果写入 `requirements.md` 或 `change-decisions.md`。
- `MS-003` `[backlog]`: 建立 Horizon 构建与集成证据基线。
  - Related workstreams: `WS-002`
  - Acceptance: 记录可重复的 configure/build/test 命令、结果、失败点和下一步。
- `MS-004` `[in-progress]`: 完成 baseline hotfix 交互测试。
  - Related workstreams: `WS-003`
  - Acceptance: `AC-006` 和 `AC-007` 均有可重复证据；当前仅 `AC-006` 已满足。

## Major Planning Decisions

- 2026-07-19：采用接管模式初始化。原因：仓库已有大量代码和构建逻辑，但没有现成 `AGENTS.md` / `.agent-os` 文档系统。
- 2026-07-19：验证脚本通过，`CLAUDE.md` 确认为 `AGENTS.md` 的硬链接；`MS-001` 标记为 verified。
- 2026-07-19：为避免顶层目录顺序调整，在 `src/hotfix` targets 创建完成后，条件链接 `HorizonExamples` 与 `vision-hotfix-all`。
- 2026-07-19：统一 MSVC 字符集选项为 `/utf-8`，消除 Helicon、Corona kernel 与 Ocarina 传递选项组合造成的 D8016。
- 2026-07-19：阶段性修改采用 `clion-build` -> `run-horizon-baseline` 的固定验证门禁。
- 2026-07-19：baseline hotfix 交互先建立 H 键入口，再逐步挂接检测与 reload。
