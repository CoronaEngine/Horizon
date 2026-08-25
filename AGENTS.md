# Horizon 协作与代理开发指南

本文档适用于 Horizon 仓库根目录及其全部子目录，用于约束自动化代理和参与开发的协作者。若某个子目录以后增加自己的 `AGENTS.md`，则子目录文档可以补充或收紧本文件中的规则；发生冲突时，以距离目标文件最近的文档为准。

## 1. 工程概况

- Horizon 是一个以 Windows x64 为主要开发平台的 C++20 工程。
- 构建系统使用 CMake，主要生成器为 Ninja Multi-Config。
- 第三方依赖由 Conan 2 管理，推荐通过名为 `horizon-dev` 的 Conda 环境运行开发脚本。
- 当前主要编译器是 MSVC；使用 VS Code/CMake Tools 时必须启用 Visual Studio Developer Environment。
- Ocarina、Ocarina 测试和 Vision Hotfix 需要 CUDA；普通 Horizon、Helicon 和工具目标不应无条件依赖 CUDA。
- 工程包含正在进行的 Helicon/Ocarina 合并与目录迁移。当前 Horizon 代码仅包括 `src/core/`、`src/math/`、`src/ast/`、`src/dsl/` 和 `src/runtime/`；今后围绕 Horizon 的讨论、检索、分析和修改默认都限定在这五个目录，只有用户明确要求时才扩展范围。

## 2. 目录职责

- `include/`：Horizon 对外公开的头文件。
- `src/`：Horizon 源代码的唯一修改作用域，当前仅包括 `core`、`math`、`ast`、`dsl` 和 `runtime`。这些目录的详细职责、当前依赖和目标架构以 [`docs/architecture/overview.md`](docs/architecture/overview.md) 为权威来源。
- `modules/ocarina/`：外部的旧 Ocarina 模块及其 generator、RHI、后端和测试。默认完全忽略，不主动搜索、查阅、修改或将 `src/` 的改动同步到该目录；只有用户明确指定 Ocarina 为任务范围时才可进入。
- `examples/`：示例程序与示例资源。
- `tools/`：开发工作流、着色器编译工具及辅助脚本。
- `cmake/`、`CMakePresets.json`、`conanfile.py`：构建、目标族和依赖配置。
- `build/`、`cmake-build-*`、`.idea/`、`.venv/`：本地产物或环境目录，不应作为源代码修改目标，也不要提交其中的临时文件。

## 3. 开始工作前

1. 阅读本文件以及目标目录内更具体的说明文档。
2. 运行 `git status --short`，确认当前分支和已有未提交修改。
3. 用户已有的修改必须保留。不要清理、覆盖或顺手格式化与当前任务无关的文件。
4. 将 Horizon 代码任务限定在 `src/core/`、`src/math/`、`src/ast/`、`src/dsl/` 和 `src/runtime/`；除非用户明确要求，不得扩展到其他目录，也不得为了寻找参考实现而查阅 `modules/ocarina/`。
5. 修改前检查相关 `CMakeLists.txt`，确认目标文件是否真正参与当前构建。

## 4. 架构与命名空间

- `src/` 中的新代码使用小写根命名空间 `horizon`，并使用 `horizon::core`、`horizon::math`、`horizon::ast`、`horizon::dsl`、`horizon::runtime` 表达类型的真实模块归属；不得新增 `ocarina` 命名空间。
- 新增依赖必须遵守架构概览中定义的目标方向，不得扩大已记录的反向依赖或循环依赖。
- AST 只维护一套模型，不得创建平行的第二套 AST 类型。
- 避免在公共头文件的全局作用域使用 `using namespace`。迁移兼容代码若必须使用，应限制在命名空间内部，并在后续重构中逐步替换为明确限定名或窄范围 `using` 声明。
- `detail` 中的符号必须使用真实所有者的命名空间。拆分模块后，不要默认不同模块的 `detail` 仍是同一个命名空间。

## 5. C++ 代码规范

- 使用 C++20；不要依赖未在根 `CMakeLists.txt` 中启用的更新语言标准。
- 遵循仓库根目录的 `.clang-format` 和 `.clang-tidy`。
- 当前主要命名习惯：
  - 类型、类、结构体和枚举使用 `CamelCase`。
  - 函数和普通变量使用 `snake_case`。
  - 成员变量使用尾部下划线，例如 `context_`。
- 指针和引用采用左对齐风格，例如 `const Type *type`、`Function &function`。
- 公共接口优先表达所有权、生命周期和空值语义；不要用裸指针隐式表达所有权。
- AST 基类接口应通过虚函数表达节点行为。不要重新引入以函数对象成员模拟多态的设计。
- 不要在无关文件中进行全文件格式化。若仓库的格式化配置与本机工具版本不兼容，应报告问题或仅对改动区域采用等价格式，不要制造大面积格式噪声。
- 源文件使用 UTF-8。修改已有中文文档时注意编码，避免把可读内容写成乱码。

## 6. 构建方式

完整的环境准备、preset、目标族和命令示例统一维护在 [`README.md`](README.md) 的 Windows 构建指南中。代理执行构建时还必须遵守以下约束：

- 所有命令从仓库根目录执行，完整配置和构建优先使用 `tools/dev.py`。
- 不同目标族必须使用各自独立的构建目录，禁止在同一个构建目录中切换目标族。
- `Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel` 的大小写必须保持不变。
- 只有在已经成功配置且依赖未变化时才使用 `build-fast`。
- 若用户明确要求“按 CLion 配置构建”，应使用当前 CLion 配置对应的 CMake、生成器、profile 和构建目录；不要自行改用另一套 preset 后宣称等价。
- 不要把本机 CMake、Ninja、Visual Studio 或 CLion 的绝对安装路径写入仓库文件。

### 判断构建是否覆盖改动

- 构建成功只证明该 target 实际包含的源文件能够构建。
- 修改 `src/core/`、`src/math/`、`src/ast/`、`src/dsl/`、`src/runtime/` 后，若它们尚未接入当前 CMake target，必须额外进行头文件聚合检查、单文件语法检查，或先完成目标接入。
- `ninja: no work to do` 不是新迁移代码通过编译的证据。
- 若验证被任务作用域之外的旧模块阻断，应明确报告边界和首个外部错误，不得转而查阅或修改 `modules/ocarina/`。

## 7. 测试与验证

- 验证范围应与修改风险和实际构建覆盖相称。代码修改原则上执行格式检查、相关目标构建和测试；无法执行或目标未覆盖时，必须采用可行的局部检查并报告原因。
- 提交前运行：

```powershell
git diff --check
```

- 修复缺陷时应尽可能增加能够在修复前失败、修复后通过的回归测试。
- 测试失败时先保留首个有因果价值的错误；不要只报告后续级联错误。

## 8. CMake 与依赖管理

- 新增或删除源目录时，同步检查对应 `CMakeLists.txt`，但不要把“复制目录”和“接入构建”视为同一操作，除非任务明确同时要求。
- 仅新增工程内部 target 或复用已有依赖时，修改相应的 `CMakeLists.txt`。
- 新增第三方依赖时：
  1. 在 `conanfile.py` 中声明，并限制到需要的目标族。
  2. 在 CMake 中使用 `find_package` 和 `target_link_libraries`。
  3. 不要使用 `FetchContent` 绕过 Conan 下载第三方库。
- 新增完整目标族时，需要同步更新 Conan 选项、CMake presets、`tools/dev.py` 的目标族和 target 映射。
- 修改 glob 覆盖范围、新增 target 或改变依赖后，必须重新 Configure，不能只执行旧构建目录中的快速构建。

## 9. 文档规则

- 根 `AGENTS.md` 保存全工程协作规则。
- `docs/` 适合保存总体架构、跨模块流程和设计决策记录。
- 模块职责、接口和局部约束应就近写在模块目录的 `README.md` 中。
- 文档描述必须区分“当前实现”“迁移状态”和“最终目标”，不要把计划中的设计写成已经完成的事实。
- 代码、CMake 接入方式或构建命令变化时，同步更新相关文档。

## 10. Git 与提交

- 未经用户明确要求，不要提交、推送、变基或创建 PR。
- 提交前检查 `git status --short` 和 `git diff`，只纳入本次任务范围内的文件。
- 使用仓库当前配置的用户身份。不要擅自增加共同作者、机器人署名或额外签名。
- 用户指定提交消息时应原样使用，包括中文和空格。
- 禁止使用 `git reset --hard`、`git checkout -- <file>` 等可能覆盖用户修改的命令，除非用户明确授权且目标已核实。
- push 前确认当前分支及其上游，push 后报告分支和提交哈希。

## 11. 完成任务时的报告

最终说明应简洁包含：

1. 修改了什么，以及主要文件或模块。
2. 实际执行了哪些构建、测试和静态检查。
3. 哪些内容没有验证，以及阻断原因。
4. 是否已提交、提交哈希以及是否已推送。

不得用“应该可以”“看起来没问题”代替可核实的验证结果，也不得把未被当前 CMake target 覆盖的文件描述为已通过完整构建。
