# Architecture And Milestones

## Current Design

- 文档系统采用根入口 `AGENTS.md` + `.agent-os/` 状态目录。
- 当前仓库观察：Horizon 是 CMake/C++20 项目，顶层 `CMakeLists.txt` 暴露 `HORIZON_BUILD_TOOLS`、`HORIZON_BUILD_EXAMPLES`、`HORIZON_BUILD_TESTS`、`HORIZON_BUILD_OCARINA`、`HORIZON_BUILD_VISION_HOTFIX` 等构建开关。
- `examples/HorizonExamples` 是示例可执行程序，默认由 `HORIZON_BUILD_EXAMPLES` 控制；它链接 `glm`、`glfw`、`Horizon`，并触发 shader 复制、runtime deps 复制和 `helicon_compile_shaders`。启用 Vision hotfix 时，顶层 CMake 还会链接 `horizon-hotfix-all`。
- `src/hotfix/` 是 Vision hotfix 支撑模块，受 `HORIZON_BUILD_VISION_HOTFIX` 控制，并要求 ocarina 目标可用。
- `horizon-hotfix-all` 通过 `INTERFACE_HORIZON_RUNTIME_DEPS` 声明动态加载插件；消费目标使用通用 `horizon_install_runtime_deps` helper staging 普通 runtime DLL 和 provider 插件，不在 example 中维护 DLL 特例名单。
- baseline GLFW window 的 H 键回调已接入 hotfix 文件检测；主循环派发异步构建回调，专用 `BaselineHotfixTest` 通过 `HotfixSlot` 替换并迁移状态。
- CoronaEngine 仓库会通过 FetchContent 拉取 Horizon，但该跨仓库集成目标属于 `OBJ-002` 的待确认范围。

## Workstreams

- `WS-001` `[verified]`: 文档系统初始化与接管。
- `WS-002` `[backlog]`: 构建/示例/hotfix/CoronaEngine 集成状态治理。
- `WS-003` `[verified]`: `HorizonExamples` baseline hotfix 交互测试接入。

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
- `MS-004` `[verified]`: 完成 baseline hotfix 交互测试。
  - Related workstreams: `WS-003`
  - Acceptance: `AC-006` 和 `AC-007` 分别由 `EV-006`、`EV-017`、`EV-018`、`EV-020` 支撑。

## Major Planning Decisions

- 2026-07-19：采用接管模式初始化。原因：仓库已有大量代码和构建逻辑，但没有现成 `AGENTS.md` / `.agent-os` 文档系统。
- 2026-07-19：验证脚本通过，`CLAUDE.md` 确认为 `AGENTS.md` 的硬链接；`MS-001` 标记为 verified。
- 2026-07-19：为避免顶层目录顺序调整，在 `src/hotfix` targets 创建完成后，条件链接 `HorizonExamples` 与 `vision-hotfix-all`。
- 2026-07-19：统一 MSVC 字符集选项为 `/utf-8`，消除 Helicon、Corona kernel 与 Ocarina 传递选项组合造成的 D8016。
- 2026-07-19：阶段性修改采用 `clion-build` -> `run-horizon-baseline` 的固定验证门禁。
- 2026-07-19：baseline hotfix 交互先建立 H 键入口，再逐步挂接检测与 reload。
- 2026-07-19：baseline hotfix 使用独立测试 `.cpp`、`HotfixSlot` 和每帧 callback 派发；运行时链接继承 Ninja 的目标路径、插件搜索目录和 `LINK_PATH`。
- 2026-07-19：hotfix runtime artifacts 改为 provider 元数据 + 通用 helper，与 Helicon/TBB 的“依赖模块声明、消费目标 staging”模式一致，并支持 CLion 无额外 PATH 直接启动。
- 2026-07-19：hotfix CMake targets、静态聚合库和运行时插件 DLL 统一采用 `horizon-hotfix*` 前缀；`vision::hotfix` C++ 命名空间和 `HORIZON_BUILD_VISION_HOTFIX` 配置开关保持不变。
