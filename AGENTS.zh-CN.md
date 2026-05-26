# Horizon AI 协作指南

> 本文件是人类维护的中文源文件。修改项目规则时，先改这里，再同步更新 `AGENTS.md` 英文版。
> `AGENTS.md` 是给 AI / Codex 读取的主要上下文文件，必须与本文保持一致。

## 1. 项目定位

Horizon 是一个基于 Vulkan 的 C++20 图形硬件抽象层，提供面向资源、命令、管线和 shader 工作流的接口。

项目当前仍处于快速迭代和重构阶段。AI 修改代码时，必须优先保证：

- 不破坏已有构建入口。
- 不扩大无关改动范围。
- 不混淆重复目录。
- 每次改动都给出明确验证命令。
- 修改前先阅读相关路径的真实代码，不凭文件名猜测结构。

## 2. 主要目录

- `include/`：公共 API。这里的改动会影响用户侧代码，必须谨慎。
- `src/`：Horizon 主实现。
- `src/Helicon/`：shader DSL、AST、代码生成、编译和反射逻辑。
- `src/hardware_wrapper_vulkan/`：当前 CMake 实际编译的 Vulkan 后端目录。
- `src/HardwareWrapperVulkan/`：历史/并行 Vulkan 后端目录。修改前必须确认任务是否明确要求这个目录。
- `src/hardware_wrapper/`、`src/HardwareWrapper/`：硬件抽象对象包装层，存在大小写历史目录并存问题。
- `tools/`：工具程序和脚本，当前包含 `ShaderCompileScripts` 和格式化脚本。
- `examples/`：示例程序和 shader 资源。
- `cmake/`：CMake 辅助模块。
- `modules/`：内嵌模块，尤其是 `modules/ocarina` 和 `modules/corona`。
- `third-party/`：第三方库和二进制，不要无关修改。
- `docs/`：项目文档。
- `tests/`：测试目录，目前可能不完整，新增测试应优先放这里。

## 3. 重复目录规则

本仓库存在大小写命名并存的历史目录，例如：

- `src/hardware_wrapper_vulkan/`
- `src/HardwareWrapperVulkan/`
- `src/hardware_wrapper/`
- `src/HardwareWrapper/`

AI 修改前必须先确认任务指定的是哪一套目录。

默认规则：

- 构建相关判断以当前 `src/CMakeLists.txt` 为准。
- 用户明确指定 camel-case 路径时，优先检查 `src/HardwareWrapperVulkan/`、`src/HardwareWrapper/`。
- 用户没有指定路径时，优先检查当前 CMake 实际编译的路径。
- 不要在一次改动里同时重构两套镜像目录，除非任务明确要求迁移或删除历史目录。

## 4. 构建命令

推荐 Windows / MSVC / Ninja 工作流：

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon
cmake --build --preset msvc-debug --target ShaderCompileScripts
cmake --build --preset msvc-debug --target HorizonExamples
```

如果只验证库：

```powershell
cmake --build --preset msvc-debug --target Horizon
```

如果修改 shader 编译、反射或工具生成：

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
```

如果修改示例、资源加载、渲染路径：

```powershell
cmake --build --preset msvc-debug --target HorizonExamples
```

## 5. 格式化规则

项目使用 `.clang-format`。当前已知风格重点：

- C++ 使用 4 空格缩进。
- 指针/引用靠近类型侧，例如 `int* value`、`auto& device`。
- namespace 内部缩进。
- 短 inline 函数可以单行。
- 不自动追加 namespace 结尾注释。
- 尽量不要格式化第三方目录。

格式检查示例：

```powershell
clang-format --style=file --dry-run --Werror src/hardware_wrapper_vulkan/hardware_context.h
```

项目也有脚本：

```powershell
.\tools\code-format.ps1 -Check
.\tools\code-format.ps1
```

AI 不应在无关任务中大范围格式化全仓库。

## 6. CMake 规则

- 根目录 `CMakeLists.txt` 负责项目级选项、依赖、子目录接入。
- `src/CMakeLists.txt` 定义 `Horizon` 静态库。
- `tools/CMakeLists.txt` 定义 `ShaderCompileScripts`。
- `examples/CMakeLists.txt` 定义 `HorizonExamples`。
- `include/` 是公共 include surface。
- `src/` 内部目录是实现细节。
- 不要把 Vulkan、VMA、Windows 平台头无故暴露到公共 API。

修改 CMake 后至少运行：

```powershell
cmake --preset ninja-msvc
cmake --build --preset msvc-debug --target Horizon
```

## 7. 公共 API 规则

公共 API 主要位于：

- `include/Horizon.h`
- `include/HardwareCommands.h`

修改公共 API 时必须说明：

- 用户代码是否需要修改。
- 是否影响资源绑定方式。
- 是否影响 pipeline 调用方式。
- 是否影响 shader codegen 生成的绑定结构。
- 是否需要更新 examples。

不要为了修内部实现，把 Vulkan 类型、Windows 类型或第三方实现细节泄露到 `include/`。

## 8. Vulkan 后端规则

Vulkan 后端改动必须谨慎处理以下内容：

- Vulkan 对象生命周期。
- VMA allocation 生命周期。
- descriptor set / descriptor pool。
- pipeline layout。
- push constant range。
- command buffer 提交。
- queue family 选择。
- image layout 和 memory barrier。
- swapchain 和 display 逻辑。

include 边界规则：

- 暴露 Vulkan 类型的内部 header 可以直接 include `<volk.h>`。
- 暴露 VMA 类型的内部 header 可以 include `<vk_mem_alloc.h>`。
- Windows `HANDLE` 只应出现在确实需要暴露该类型的内部接口中。
- `VOLK_IMPLEMENTATION` 不允许出现在 header。
- `VMA_IMPLEMENTATION` 只能放在一个 `.cpp` 实现文件中。
- 不要创建新的万能工具头来集中 include 所有 Vulkan 依赖。

## 9. Shader / Helicon 规则

`src/Helicon/` 负责 shader DSL、AST、代码生成、编译和反射。

常见关键路径：

- `src/Helicon/Compiler/ShaderCodeCompiler.h`
- `src/Helicon/Compiler/ShaderCodeCompiler.cpp`
- `src/Helicon/Compiler/ShaderLanguageConverter.h`
- `src/Helicon/Compiler/ShaderLanguageConverter.cpp`
- `src/Helicon/Codegen/`
- `tools/main.cpp`

修改 shader reflection 时，必须沿消费链检查：

1. 反射数据从哪里产生。
2. `ShaderResources` 存储了哪些字段。
3. `tools/main.cpp` 如何生成绑定代码。
4. `include/Horizon.h` 如何把用户赋值转为 runtime 写入。
5. Vulkan pipeline 如何使用这些信息创建 layout 或写入 push constant。

## 10. Push Constant 规则

当前 push constant 的有效来源是 SPIR-V 反射，尤其是 `spirv-cross`。

不要假设 Slang reflection 一定能提供完整 push constant 信息。

Vulkan 侧实际关心的信息包括：

- push constant block size。
- member name。
- member byte offset。
- member type size。
- bind type。
- resource-like value 是 32-bit 还是 64-bit handle。

修改 push constant 相关逻辑时，必须检查：

- `ShaderResources::pushConstantSize`
- `ShaderResources::pushConstantName`
- `ShaderBindInfo::byteOffset`
- `ShaderBindInfo::typeSize`
- `ShaderBindInfo::bindType`
- generated binding struct。
- compute/rasterizer pipeline 中的 push constant buffer 分配和 `vkCmdPushConstants` 调用。

## 11. 测试和验证规则

每次改动后至少执行一个相关验证命令。

推荐最小验证：

```powershell
cmake --build --preset msvc-debug --target Horizon
```

修改 tools / shader compiler：

```powershell
cmake --build --preset msvc-debug --target ShaderCompileScripts
```

修改 examples：

```powershell
cmake --build --preset msvc-debug --target HorizonExamples
```

修改格式或头文件 include：

```powershell
.\tools\code-format.ps1 -Check
cmake --build --preset msvc-debug --target Horizon
```

如果测试系统已建立：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

如果无法运行验证，必须在回复中明确说明原因。

## 12. Git 和改动范围

AI 必须遵守：

- 不要 revert 用户已有改动。
- 不要执行 `git reset --hard`。
- 不要删除不理解的文件。
- 不要无关重排 include、格式化或命名。
- 修改前先查看 `git status --short --branch`。
- 一个任务只改相关文件。
- 遇到工作区已有无关改动时，忽略它们，不要回滚。

提交建议：

- `docs: ...`
- `build: ...`
- `tools: ...`
- `test: ...`
- `refactor: ...`
- `fix: ...`

## 13. 给 AI 的工作方式

AI 开始任务时应该：

1. 阅读本文件。
2. 查看 `git status --short --branch`。
3. 搜索相关路径和符号。
4. 先理解真实调用链。
5. 再做最小必要修改。
6. 运行相关验证。
7. 总结修改内容、验证结果和剩余风险。

AI 回复应尽量包含：

- 改了什么。
- 为什么这么改。
- 运行了什么验证。
- 是否有未验证部分。
- 用户下一步可以做什么。

## 14. 禁止事项

不要：

- 无任务要求时修改 `third-party/`。
- 无任务要求时修改 `modules/` 内第三方或外部项目代码。
- 把 Vulkan 实现细节泄露到公共 API。
- 同时混改大小写两套历史目录。
- 用猜测替代代码搜索。
- 大范围格式化无关文件。
- 添加没有验证路径的大型抽象。
- 在 header 中定义 `VOLK_IMPLEMENTATION` 或 `VMA_IMPLEMENTATION`。
- 为了消除编译错误而隐藏真实生命周期或同步问题。

## 15. 中文到英文同步规则

`AGENTS.zh-CN.md` 是人工维护源文件。

每次修改本文件后，应同步更新 `AGENTS.md`：

- 保持章节结构一致。
- 英文表达要清晰、直接，适合 AI 作为项目上下文读取。
- 不需要逐字翻译，但规则含义必须一致。
- 如果中文新增禁止事项，英文必须同步新增。
- 如果中文修改构建命令，英文必须同步修改。
- 如果发现英文和中文冲突，以中文为准。

## 16. 项目快捷命令

当用户输入以下项目快捷命令时，AI 必须按对应规则执行。
这些命令使用 `=` 前缀，避免和系统内置 slash command、mention 语法冲突。

### `=sa`

`=sa` 是 "sync agents" 的缩写。

根据 `AGENTS.zh-CN.md` 同步更新 `AGENTS.md`。

规则：

- 将 `AGENTS.zh-CN.md` 视为唯一真实来源。
- 不要修改 `AGENTS.zh-CN.md`。
- `AGENTS.md` 必须保持英文。
- 保持两个文件的章节结构一致。
- 保留所有路径、命令、警告、约束和禁止事项。
- 英文表达要简洁、直接，适合作为 AI 项目上下文。
- 同步后，在 `AGENTS.md` 顶部添加或更新 `AGENTS_ZH_CN_SHA256` marker。
- 同步后运行 `.\tools\sync-agents.ps1 -Check`。
- 不要修改无关文件。

### `=ca`

`=ca` 是 "check agents" 的缩写。

检查 `AGENTS.md` 是否与 `AGENTS.zh-CN.md` 同步。

规则：

- 运行 `.\tools\sync-agents.ps1 -Check`。
- 不要修改任何文件。
- 汇报两个文件是否同步。
- 如果不同步，提示用户运行 `=sa`。

### `=gc`

`=gc` 是 "GitHub check" 或 "git check" 的缩写。

检查当前工作区是否已经准备好发布到 GitHub。

规则：

- 运行 `git status --short --branch`。
- 检查当前改动文件和相关 diff。
- 运行相关验证命令，但不要暂存、提交、推送或创建 PR。
- 如果存在 `AGENTS.md` 和 `AGENTS.zh-CN.md`，运行 `.\tools\sync-agents.ps1 -Check`。
- 文档改动通常不需要 CMake 构建。
- C++、CMake、工具脚本或示例改动必须运行对应的构建验证。
- 汇报哪些文件会被纳入提交。
- 汇报验证通过或失败。
- 如果已经准备好发布，提示用户可以运行 `=gh`。

### `=gh`

`=gh` 是 "GitHub publish" 的缩写。

将当前意图明确的改动发布到 GitHub。

规则：

- 运行 `git status --short --branch`。
- 在暂存前检查当前改动文件和相关 diff。
- 如果工作区包含无关改动，先询问用户哪些文件应该进入本次 PR。
- 只暂存本次任务相关文件。
- 不要使用 `git add .`，除非整个工作区明确都属于本次提交范围。
- 提交前运行相关验证命令。
- 如果存在 `AGENTS.md` 和 `AGENTS.zh-CN.md`，必须运行 `.\tools\sync-agents.ps1 -Check`。
- 文档改动通常不需要 CMake 构建。
- C++、CMake、工具脚本或示例改动必须运行对应的构建验证。
- 验证失败时不要推送，除非用户明确要求继续。
- 使用简洁的 conventional commit message。
- commit message 必须包含标题和正文摘要；正文用中文说明改了什么、为什么改、验证了什么。
- 推送当前分支到 `origin`。
- 创建 GitHub draft pull request。
- 汇报分支名、commit hash、PR URL 和验证结果。
