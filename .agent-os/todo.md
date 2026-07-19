# TODO

## Backlog

- `TD-003` `[backlog]`: 建立 Horizon 构建与测试证据基线。
  - Related items: `WS-002`, `MS-003`
  - Definition of done: 记录当前推荐 configure/build/test 命令、实际输出摘要、失败点或通过证据，并更新 `acceptance-report.md`。

## Ready

- `TD-002` `[ready]`: 确认 Horizon 的长期工程目标和近期优先级。
  - Related items: `OBJ-002`, `MS-002`, `RSK-001`
  - Definition of done: 用户选择或说明主要目标；文档更新 `requirements.md`、`change-decisions.md`、`project-index.md`。

## Doing

- None

## Blocked

- None

## Done

- `TD-010` `[done]`: baseline H 键按下时输出 `hotfix`。
  - Related items: `OBJ-003`, `REQ-011`, `MS-004`
  - Evidence: `EV-015`, `EV-016`
  - Verification: 源码分支、编译和 baseline 启动已验证；真实按键输出自动捕获受执行策略阻止，`AC-011` 仍待人工按键确认。

## Verified

- `TD-005` `[verified]`: 在 `example_baseline` 中实现 H 键触发的 hotfix 检测、自动 reload 和行为更新。
  - Related items: `OBJ-003`, `REQ-007`, `MS-004`
  - Evidence: `EV-017`, `EV-018`, `EV-019`, `EV-020`
  - Verification: 真实 H 输入触发专用 `.cpp` 检测和运行时编译；临时 DLL 在原进程内加载，既有对象状态得到迁移，v2 行为模块进入同一进程；最终 v1 构建和启动门禁通过。

- `TD-001` `[verified]`: 初始化 Horizon 项目文档系统。
  - Related items: `OBJ-001`, `REQ-001`, `REQ-002`, `MS-001`
  - Evidence: `EV-001`, `EV-003`, `EV-005`
  - Verification: 根契约和 `.agent-os/` 文件存在，内容包含当前可验证事实和未确认目标，验证脚本通过，`CLAUDE.md` 硬链接已确认。
- `TD-004` `[verified]`: 为 `HorizonExamples` 添加完整 hotfix target 依赖。
  - Related items: `OBJ-003`, `REQ-006`, `MS-004`
  - Evidence: `EV-006`
  - Verification: 完整 CMake 配置成功；Ninja 查询显示 `examples/HorizonExamples.exe` 输入包含 `lib/vision-hotfix-all.lib`、hotfix 核心、compiler/parser plugins 和现有 test DLL。
- `TD-006` `[verified]`: 创建仓库级 `clion-build` skill。
  - Related items: `REQ-008`, `AC-008`
  - Evidence: `EV-008`, `EV-009`
  - Verification: skill 校验与绝对路径扫描通过；实际调用复用了 CLion cache 并执行相同 build target、目录和并行度。
- `TD-008` `[verified]`: 创建仓库级 `run-horizon-baseline` skill。
  - Related items: `REQ-009`, `AC-009`
  - Evidence: `EV-011`, `EV-012`
  - Verification: skill 校验与绝对路径扫描通过；实际启动产生持续运行的 baseline 进程并返回 PID。
- `TD-007` `[verified]`: 处理 `HorizonExamples` 的 MSVC D8016 字符集参数冲突。
  - Related items: `OBJ-003`, `MS-004`
  - Evidence: `EV-013`
  - Verification: Helicon、Corona kernel 和测试辅助统一使用 `/utf-8` 后，`clion-build` 成功且 baseline 进程持续运行。
- `TD-009` `[verified]`: 建立阶段性修改双 skill 验证规则。
  - Related items: `REQ-010`, `AC-010`, `CD-005`
  - Evidence: `EV-013`, `EV-014`
  - Verification: `AGENTS.md` 已写入门禁，本阶段按顺序执行两个 skills 并通过。

## Abandoned

- None
