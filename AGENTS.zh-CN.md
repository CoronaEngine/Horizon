# Horizon AI 入口

> 本文件是根 AI 入口的中文源文件。修改根规则时，先改这里，再同步更新 `AGENTS.md`。
> 其他中文源位于 `docs/agents/zh-CN/` 和 `.agents/skills/horizon-workflow/SKILL.zh-CN.md`。
> 英文文件是 AI 默认读取入口，必须与中文源保持一致。

## 1. 核心原则

Horizon 是一个 C++20 Vulkan 图形硬件抽象层，包含公共 API、Vulkan 后端、Helicon shader/codegen/reflection、示例和工具脚本。

AI 在本仓库工作时必须：

- 先读本文件，再按任务读取 `docs/agents/*.md`。
- 只加载当前任务需要的上下文，避免注意力稀疏。
- 修改前查看 `git status --short --branch`。
- 不要回滚用户已有改动。
- 不要无关修改 `third-party/`、`modules/` 或历史镜像目录。
- 每次实质改动都给出验证命令和结果。

## 2. 路由表

按任务只读需要的文件：

- 构建、CMake、preset：`docs/agents/build.md`
- 本地提交、GitHub 发布、commit、PR：`docs/agents/git.md`
- 格式化、clang-format：`docs/agents/formatting.md`
- Vulkan 后端、VOLK/VMA、descriptor、barrier：`docs/agents/vulkan.md`
- Helicon、shader DSL、codegen、reflection：`docs/agents/helicon.md`
- push constant layout、绑定字段、runtime 消费链：`docs/agents/push-constants.md`

项目内共享 Agent skill：

- `.agents/skills/horizon-workflow/SKILL.md`

该 skill 是通用 Markdown 工作流，不局限于 Codex；其他 AI Agent 也可以读取。

## 3. 关键目录

- `include/`：公共 API，改动必须谨慎。
- `src/`：Horizon 主实现。
- `src/hardware_wrapper_vulkan/`：当前 CMake 实际编译的 Vulkan 后端。
- `src/HardwareWrapperVulkan/`：历史/并行 Vulkan 后端；只有任务明确指定时才改。
- `src/Helicon/`：shader DSL、AST、codegen、compiler、reflection。
- `tools/`：工具程序和脚本。
- `examples/`：示例程序和 shader 资源。
- `docs/agents/`：按需加载的 AI 上下文包。
- `.agents/skills/`：项目共享 Agent skill。

## 4. 默认验证

修改 Agent 文档或 skill 后，必须检查同步：

```powershell
.\tools\sync-agents.ps1 -Check
```

C++ / CMake / 工具 / 示例改动按需运行：

```powershell
.\tools\dev.ps1 build Horizon
.\tools\dev.ps1 build ShaderCompileScripts
.\tools\dev.ps1 build HorizonExamples
```

纯文档改动通常不需要 CMake 构建。

## 5. 项目口令

这些口令使用 `=` 前缀，避免和 slash command、mention 语法冲突。

### `=sa`

根据中文源同步所有英文 Agent 文件。

- 不要修改中文源文件。
- 同步范围包括根 `AGENTS.md`、`docs/agents/*.md` 和项目 skill。
- 保持章节结构一致；英文要短、直接、适合作为 AI 上下文。
- 更新对应英文文件顶部的 sync marker。
- 运行 `.\tools\sync-agents.ps1 -Check`。

### `=ca`

检查所有英文 Agent 文件是否与中文源同步。

- 只运行 `.\tools\sync-agents.ps1 -Check`。
- 不要修改文件。
- 如果不同步，提示用户运行 `=sa`。

### `=gc`

检查当前改动是否可以发布到 GitHub。

- 查看 `git status --short --branch`。
- 检查改动范围和相关 diff。
- 运行相关验证。
- 不要暂存、提交、推送或创建 PR。
- 如果准备好本地提交，提示用户可以运行 `=cm`。
- 如果用户明确需要 GitHub PR，再提示可以运行 `=gh`。

### `=cm`

只把当前意图明确的改动提交到当前本地分支。

- 先检查改动范围和验证结果。
- 只暂存本次任务相关文件，不要默认 `git add .`。
- commit 必须包含标题和中文正文摘要；正文说明改了什么、为什么改、验证了什么。
- 不要推送、不要创建 PR、不要合并分支。
- 汇报分支、commit 和验证结果。

### `=gh`

把当前意图明确的改动提交并发布到 GitHub PR。

- 先检查改动范围和验证结果。
- 只暂存本次任务相关文件，不要默认 `git add .`。
- 如果存在未提交改动，先按 `=cm` 规则提交。
- 推送当前分支到 `origin`。
- 创建 GitHub draft PR。
- 汇报分支、commit、PR URL 和验证结果。

## 6. 同步规则

中文文件是人类维护源，英文文件是 AI 默认读取入口。

同步关系：

- `AGENTS.zh-CN.md` -> `AGENTS.md`
- `docs/agents/zh-CN/*.md` -> `docs/agents/*.md`
- `.agents/skills/horizon-workflow/SKILL.zh-CN.md` -> `.agents/skills/horizon-workflow/SKILL.md`

每次修改任一中文源后，必须同步对应英文文件：

- 英文应清晰、简洁，适合 AI 作为入口上下文。
- 规则含义必须一致，不要求逐字翻译。
- 英文文件顶部的 SHA256 marker 必须匹配中文源。
- 中文和英文冲突时，以中文为准。
- 不要在 `.agents/skills/` 下创建 `zh-CN/SKILL.md` 作为中文源，避免被 Agent 识别成第二个 skill。
