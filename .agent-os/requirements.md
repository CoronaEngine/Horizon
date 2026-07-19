# Requirements

## Goals

- `OBJ-001`: 初始化 Horizon 仓库的 AI-first 项目文档系统，让后续 agent 能从仓库文件恢复项目状态、约束、TODO、证据和下一步。
- `OBJ-002` `[needs-human-decision]`: Horizon 的长期工程目标尚未在本文档系统中确认。候选方向包括独立 Horizon 库维护、CoronaEngine 集成支撑、hotfix/ocarina 相关治理、examples/build 稳定性治理。
- `OBJ-003` `[active]`: 在 `HorizonExamples` 的 `example_baseline` 中加入可交互的 hotfix 测试。

## Requirements

- `REQ-001`: 项目根目录必须包含 `AGENTS.md`，并尽可能让 `CLAUDE.md` 与其保持硬链接或等价内容。
- `REQ-002`: 项目状态目录必须包含 `project-index.md`、`requirements.md`、`change-decisions.md`、`architecture-milestones.md`、`todo.md`、`acceptance-report.md`、`lessons-learned.md`、`run-log.md`。
- `REQ-003`: 文档必须区分可验证仓库事实、agent 推断和需要用户确认的人类决策。
- `REQ-004`: 后续工作必须记录 TODO 状态、验证证据、失败探索、阻塞和工作会话结束摘要。
- `REQ-005`: 文档可以使用中文；新增代码注释、脚本输出和运行时用户可见代码文本默认使用英文。
- `REQ-006`: `HorizonExamples` 必须在启用 `HORIZON_BUILD_VISION_HOTFIX` 时获得完整 hotfix target 依赖。
- `REQ-007`: baseline 示例中按下 H 键后，应触发文件修改检测，完成自动 reload，并让当前可执行程序中的既有对象表现出更新后的行为。
- `REQ-008`: 仓库必须提供名为 `clion-build` 的 project skill，使用 CLion Debug 的 `cmake-build-debug` 构建目录、`HorizonExamples` target 和相同并行度；skill 内容不得包含绝对路径。
- `REQ-009`: 仓库必须提供 project skill，以 `baseline` 作为唯一参数启动 `cmake-build-debug` 中的 `HorizonExamples`，并使用能正确解析相对 shader 和 hotfix runtime DLL 的工作目录与 PATH。
- `REQ-010`: 每次阶段性修改完成后，必须依次使用 `clion-build` 和 `run-horizon-baseline` 验证；两项均成功并记录证据后才能声明阶段完成。
- `REQ-011`: baseline 示例必须注册 H 键回调；每次收到 `GLFW_PRESS` 时向标准输出打印一行 `hotfix`，忽略 `GLFW_REPEAT`。

## Acceptance Criteria

- `AC-001`: 根 `AGENTS.md` 存在，且 `CLAUDE.md` 已建立为硬链接或内容等价副本。
- `AC-002`: `.agent-os/` 下八个 required documents 全部存在。
- `AC-003`: `project-index.md` 暴露一个全局 top next action、活跃工作流、活跃阻塞和读下一步路径。
- `AC-004`: `requirements.md` 明确标记尚未由用户确认的长期项目目标，而不是伪造产品路线。
- `AC-005`: 初始化结果通过技能验证脚本或等价文件检查。
- `AC-006`: 开启 examples、ocarina 和 Vision hotfix 后，CMake 可成功生成，且 `HorizonExamples` 的构建图包含 `vision-hotfix-all` 及其插件/测试依赖。
- `AC-007`: 运行 baseline 示例时，按 H 能触发一次 hotfix 检查；修改受监视实现文件后能够 reload，且无需重启 exe 即可观察到行为变化。
- `AC-008`: `clion-build` 通过 skill 结构校验和绝对路径扫描，实际调用能从 CLion cache 取得同一 CMake/MSVC，并执行 `--build cmake-build-debug --target HorizonExamples -j 30`。
- `AC-009`: baseline 启动 skill 通过结构和绝对路径扫描；实际调用能创建持续运行的 `HorizonExamples baseline` 进程，并在调用后立即返回 PID。
- `AC-010`: 阶段验收证据同时包含 `clion-build` 成功结果，以及 `run-horizon-baseline` 启动后进程持续存活的结果。
- `AC-011`: baseline 运行时实际按下 H，标准输出出现且仅出现对应按下事件的一行 `hotfix`。

## Non-Goals

- 本次初始化不修改 C++、CMake、shader、third-party 或构建产物。
- 本次初始化不声称 Horizon 当前能完整 configure/build/test，除非后续有命令证据。
- 本次初始化不替用户决定 Horizon 的长期产品目标或是否保留 hotfix。
- 当前 CMake 依赖阶段不实现 H 键处理、文件监视注册或运行时行为替换；这些属于后续 `TD-005`。

## Hard Constraints

- 保护用户已有代码和未跟踪文件；不要执行破坏性 git 操作。
- 编辑代码后若依赖关系变化，需要运行 `codegraph sync`。
- 每次阶段性修改结束必须依次执行 `clion-build` 和 `run-horizon-baseline`；失败不得被忽略或记为完成。
- 所有完成/验证声明必须有证据条目支撑。

## Source Note

本文档保存人类定义的意图。2026-07-19 的初始化请求是“初始化项目文档”；同日用户确认近期目标为 `HorizonExamples` baseline hotfix 交互测试，但 Horizon 的长期工程目标仍待确认。
