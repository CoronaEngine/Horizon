# Run Log

## Entries

- 2026-07-19 初始化 Horizon 项目文档系统。
  - Worked on: `OBJ-001`, `WS-001`, `MS-001`, `TD-001`
  - State changes: 无文档系统 -> 已创建并验证 `AGENTS.md`、硬链接 `CLAUDE.md`、`.agent-os/` 基础文档；`TD-001` 和 `MS-001` 标记为 `verified`。
  - Evidence produced: `EV-001`, `EV-002`, `EV-003`, `EV-005`
  - Next likely action: 请用户确认 `OBJ-002` 的长期工程目标和近期优先级。

- 2026-07-19 阅读 `src/hotfix/` 目录。
  - Worked on: `WS-002`
  - State changes: 无代码变更；形成 hotfix 模块结构和运行流程理解。
  - Evidence produced: 阅读 `src/hotfix/CMakeLists.txt`、`hotfix.*`、`file_tool.*`、`build_system.*`、`build_rules.*`、`compiler.*`、`cmd_process.*`、`module_interface.*`、`macro.h`、`object.h`、`rules_parsers/`、`compiles/`、`test/`。
  - Next likely action: 如需继续，确认是要清理 hotfix、验证构建，还是分析 CoronaEngine/Vision 依赖面。

- 2026-07-19 为 `HorizonExamples` 添加 hotfix target 依赖。
  - Worked on: `OBJ-003`, `WS-003`, `MS-004`, `TD-004`
  - State changes: 顶层 CMake 在 hotfix targets 创建后，将 `vision-hotfix-all` 条件链接到 `HorizonExamples`；未搬迁测试代码，未实现 H 键运行时逻辑。
  - Evidence produced: `EV-006`, `EV-007`
  - Next likely action: 执行 `TD-005`，在 `example_baseline` 中接入 H 键与 hotfix 生命周期。

- 2026-07-19 创建仓库级 `clion-build` skill。
  - Worked on: `REQ-008`, `AC-008`, `TD-006`
  - State changes: 新增 `.agents/skills/clion-build/`，固定复用 `cmake-build-debug`、`HorizonExamples` 和 `-j 30`；skill 内无绝对路径。
  - Evidence produced: `EV-008`, `EV-009`, `EV-010`
  - Next likely action: 修复或确认 MSVC D8016 字符集选项冲突，然后继续 `TD-005`。

- 2026-07-19 创建仓库级 `run-horizon-baseline` skill。
  - Worked on: `REQ-009`, `AC-009`, `TD-008`
  - State changes: 新增 `.agents/skills/run-horizon-baseline/`，固定从 Debug examples 输出目录以 `baseline` 参数启动，并为子进程补充 Debug bin PATH。
  - Evidence produced: `EV-011`, `EV-012`
  - Next likely action: 继续 `TD-007`，恢复当前 target 构建。

- 2026-07-19 建立 hotfix example 阶段验证门禁并解除 D8016。
  - Worked on: `OBJ-003`, `REQ-006`, `REQ-010`, `TD-004`, `TD-007`, `TD-009`
  - State changes: 确认 `HorizonExamples` 链接 `vision-hotfix-all`；`AGENTS.md` 要求每个阶段依次运行两个 skills；MSVC charset 参数统一为 `/utf-8`，`RSK-002` 解除。
  - Evidence produced: `EV-013`, `EV-014`
  - Next likely action: 执行 `TD-005`，实现 baseline 的 H 键 hotfix 检测和 reload。

- 2026-07-19 添加 baseline H 键日志回调。
  - Worked on: `OBJ-003`, `REQ-011`, `TD-010`
  - State changes: baseline window 注册 GLFW key callback；H 的 press 事件打印 `hotfix`，repeat 不打印；尚未接入检测和 reload。
  - Evidence produced: `EV-015`, `EV-016`
  - Next likely action: 人工确认 H 键输出，随后继续 `TD-005` 将回调连接到 hotfix 检测逻辑。

- 2026-07-19 完成 baseline hotfix 交互测试。
  - Worked on: `OBJ-003`, `WS-003`, `MS-004`, `TD-005`
  - State changes: 新增专用 RuntimeObject/HotfixSlot 测试对象；H 键接入修改检测，主循环派发异步 callback；hotfix 支持非 `bin` 输出目标、Debug plugin 搜索路径和 Ninja `LINK_PATH`；`OBJ-003`、`WS-003`、`MS-004`、`TD-005` 标记为 verified。
  - Evidence produced: `EV-017`, `EV-018`, `EV-019`, `EV-020`
  - Failed explorations preserved: compiler plugin 初始搜索目录错误；运行时 linker 初始遗漏 `slang.lib` 搜索路径；文件写入与过早按 H 产生一次 v1 重编译竞态；`codegraph.cmd sync` 再次确认仓库未初始化 CodeGraph。
  - Next likely action: `TD-002`，确认 Horizon 长期工程目标和近期优先级。

- 2026-07-19 统一 Vision hotfix runtime 依赖处理并修复 CLion 直接启动。
  - Worked on: `OBJ-003`, `REQ-006`, `REQ-009`, `CD-008`
  - State changes: 新增通用 `horizon_install_runtime_deps`；`vision-hotfix-all` 声明 compiler/parser plugin artifacts；`HorizonExamples` 不再包含具体 hotfix DLL 名单。
  - Evidence produced: `EV-021`, `EV-022`
  - Failed explorations preserved: 跨 CMake 目录注册 TARGET POST_BUILD 被拒绝；target property 中的嵌套生成表达式需要 `TARGET_GENEX_EVAL`；`codegraph.cmd sync` 仍因仓库未初始化而不可用。
  - Next likely action: 用户在 CLion 中重试 `HorizonExamples baseline`；若还有错误，采集第一条错误原文区分工作目录和运行时 hotfix 路径问题。

- 2026-07-19 将 hotfix targets 和 DLL 前缀统一为 Horizon。
  - Worked on: `OBJ-003`, `REQ-006`, `AC-006`, `CD-009`
  - State changes: `vision-hotfix*` CMake targets、聚合库和运行时 DLL 重命名为 `horizon-hotfix*`；compiler/parser 动态加载字符串与 test 注册路径同步更新；C++ 命名空间和配置开关保持不变。
  - Evidence produced: `EV-023`
  - Verification: `clion-build` 成功生成 HorizonExamples 和全部新名前缀产物；当前 `build.ninja` 无旧 target 名；`run-horizon-baseline` 返回 PID `5940` 且进程响应。
  - Failed explorations preserved: `codegraph.cmd sync` 返回 `CodeGraph not initialized`，仓库依赖图工具仍未初始化。
  - Next likely action: `TD-002`，确认 Horizon 长期工程目标和近期优先级。
