# Change Decisions

## Purpose

追加记录后续人类决策。这里记录会改变范围解释、约束或验收边界的决定，不用它悄悄改写 `requirements.md`。

## Entries

- `CD-001` 2026-07-19 `[active]`: 采用 `agent-project-system` 技能为当前 Horizon 仓库初始化 AI 可恢复项目文档。
  - Related items: `OBJ-001`, `REQ-001`, `REQ-002`, `WS-001`
  - Human rationale: 用户明确请求“初始化项目文档”。
  - Effect on project: 新增根契约和 `.agent-os/` 状态文档；后续 agent 应优先从这些文件恢复项目状态。
- `CD-002` 2026-07-19 `[active]`: 将 hotfix 交互测试接入 `HorizonExamples` 的 `example_baseline`，第一阶段先增加 target 依赖。
  - Related items: `OBJ-003`, `REQ-006`, `REQ-007`, `WS-003`
  - Human rationale: 用户计划用 H 键触发修改检测、自动 reload 和当前 exe 行为更新，并明确要求先修改 CMake target 依赖。
  - Effect on project: `HorizonExamples` 在 Vision hotfix 开启时链接 `vision-hotfix-all`；运行时代码作为后续工作。
- `CD-003` 2026-07-19 `[active]`: 新增仓库级 `clion-build` skill，固定复用 CLion Debug 构建树来构建 `HorizonExamples`。
  - Related items: `REQ-008`, `AC-008`, `TD-006`
  - Human rationale: 用户要求构建方式和目录与 CLion 完全一致，并禁止 skill 包含绝对路径。
  - Effect on project: skill 从现有 cache 动态解析 CLion 的 CMake/MSVC，不自行 configure、不创建其他构建目录。
- `CD-004` 2026-07-19 `[active]`: 新增仓库级 `run-horizon-baseline` skill，固定以 `baseline` 参数启动 Debug 示例。
  - Related items: `REQ-009`, `AC-009`, `TD-008`
  - Human rationale: 用户明确要求项目 skill 用于以 baseline 为参数启动 HorizonExamples。
  - Effect on project: 启动脚本固定 executable、工作目录和参数，并为子进程补充 Debug runtime PATH；不隐式构建。
- `CD-005` 2026-07-19 `[active]`: 将 `clion-build` 后接 `run-horizon-baseline` 设为每次阶段性修改的强制验收门禁。
  - Related items: `REQ-010`, `AC-010`, `TD-009`
  - Human rationale: 用户明确要求每次阶段性修改完成后都使用两个 project skills 验证。
  - Effect on project: `AGENTS.md` 新增强制规则；构建或 baseline 启动任一失败时不得声明阶段完成。
- `CD-006` 2026-07-19 `[active]`: baseline hotfix 交互的第一步只实现 H 键日志回调。
  - Related items: `OBJ-003`, `REQ-011`, `TD-010`
  - Human rationale: 用户明确要求按下 H 后打印 `hotfix` 字符串。
  - Effect on project: GLFW window 注册 key callback，仅处理 `GLFW_KEY_H` 的 `GLFW_PRESS`；尚未触发 hotfix 检测或 reload。
- `CD-007` 2026-07-19 `[active]`: 将 baseline 的 H 键日志桩扩展为完整 hotfix 检测、reload 和行为输出。
  - Related items: `OBJ-003`, `REQ-007`, `REQ-011`, `TD-005`, `MS-004`
  - Human rationale: 用户要求参照 hotfix test 为 baseline 增加独立用例，外部修改 `.cpp` 后按 H reload，并以打印展示新行为和状态迁移。
  - Effect on project: `CD-006` 的固定 `hotfix` 文本不再作为当前验收要求；H 现在输出无修改、构建中、构建启动或 reload 结果。
- `CD-008` 2026-07-19 `[active]`: Vision hotfix runtime artifacts 使用与其他依赖库一致的 provider 声明和通用 staging helper。
  - Related items: `OBJ-003`, `REQ-006`, `AC-009`, `MS-004`
  - Human rationale: 用户不接受在 `HorizonExamples` 中维护 hotfix DLL 特例列表，希望 vision-hotfix 依赖与其他 runtime 依赖采用相同处理方式。
  - Effect on project: `vision-hotfix-all` 负责声明插件 artifacts，`horizon_install_runtime_deps` 统一复制普通 target runtime DLL 和 provider 插件；CLion 可直接启动 examples 目录下的 exe。
- `CD-009` 2026-07-19 `[active]`: hotfix 构建 targets 和产物统一使用 `horizon-hotfix*` 前缀。
  - Related items: `OBJ-003`, `REQ-006`, `AC-006`
  - Human rationale: 用户明确要求仍包含 `vision` 的 hotfix target 名改为 `horizon`。
  - Effect on project: 核心库、聚合库、测试库、compiler/parser interface targets 与插件 DLL 全部改为 `horizon-hotfix*`；运行时插件加载字符串同步更新。`vision::hotfix` C++ 命名空间和已有配置开关不在本次 target 重命名范围内。
