# Lessons Learned

## Exploration Records

- `EXP-001` `[noted]`: 初始化时不伪造长期产品目标。
  - Motivation: 技能要求文档可恢复，但用户当前只要求“初始化项目文档”，没有说明 Horizon 的长期工程目标。
  - Method: 审计仓库结构、CMake 入口和近期对话中已查过的 examples / hotfix 信息。
  - Result: 可以记录 CMake 和模块事实，但不能把独立库维护、CoronaEngine 集成、hotfix 清理等方向写成已确认目标。
  - Why not selected: 直接写死某个长期目标会改变用户意图。
  - Retry condition: 用户明确选择或描述 Horizon 的长期目标和近期优先级后，再更新 `OBJ-002`。
- `EXP-002` `[noted]`: 不使用被策略禁止的桌面按键注入验证。
  - Motivation: 希望自动验证 GLFW H 键回调确实打印 `hotfix`。
  - Method: 尝试 Win32 message injection，以及窗口激活配合 `SendKeys` 并捕获 stdout。
  - Result: 两种命令都在执行前被本机策略拒绝，没有产生运行副作用。
  - Why not selected: 不应绕过执行策略；构建与 baseline 启动仍可作为阶段证据。
  - Retry condition: 使用允许的 UI 自动化工具，或由用户在可见 baseline 窗口中人工按 H 确认控制台输出。
