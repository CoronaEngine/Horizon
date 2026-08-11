# Horizon 协作与代理开发指南

本文档适用于 Horizon 仓库根目录及其全部子目录，用于约束自动化代理和参与开发的协作者。若某个子目录以后增加自己的 `AGENTS.md`，则子目录文档可以补充或收紧本文件中的规则；发生冲突时，以距离目标文件最近的文档为准。

## 1. 工程概况

- Horizon 是一个以 Windows x64 为主要开发平台的 C++20 工程。
- 构建系统使用 CMake，主要生成器为 Ninja Multi-Config。
- 第三方依赖由 Conan 2 管理，推荐通过名为 `horizon-dev` 的 Conda 环境运行开发脚本。
- 当前主要编译器是 MSVC；使用 VS Code/CMake Tools 时必须启用 Visual Studio Developer Environment。
- Ocarina、Ocarina 测试和 Vision Hotfix 需要 CUDA；普通 Horizon、Helicon 和工具目标不应无条件依赖 CUDA。
- 工程包含正在进行的 Helicon/Ocarina 合并与目录迁移。修改代码前必须区分“当前参与构建的实现”和“迁移中的副本”。

## 2. 目录职责

- `include/`：Horizon 对外公开的头文件。
- `src/core/`：通用基础设施、类型系统、容器封装及运行时工具。
- `src/math/`：数学类型、数值工具与几何算法。
- `src/ast/`：唯一一套 AST 节点模型、遍历接口及相关校验逻辑。
- `src/dsl/`：面向用户的 DSL 类型、表达式构造和语句构造接口。
- 上述四个目录来自 Ocarina 迁移，当前并未全部加入 `src/CMakeLists.txt`，不能假设构建 `Horizon` 会编译这些文件。`src/` 下其他目录仍需大幅调整，现阶段不在本文档中定义其职责。
- `modules/ocarina/`：旧 Ocarina 模块及其 generator、RHI、后端和测试。除非任务明确要求同步修改，否则不要把对 `src/` 新目录的修改扩散到这里，反之亦然。
- `examples/`：示例程序与示例资源。
- `tools/`：开发工作流、着色器编译工具及辅助脚本。
- `cmake/`、`CMakePresets.json`、`conanfile.py`：构建、目标族和依赖配置。
- `build/`、`cmake-build-*`、`.idea/`、`.venv/`：本地产物或环境目录，不应作为源代码修改目标，也不要提交其中的临时文件。

## 3. 开始工作前

1. 阅读本文件以及目标目录内更具体的说明文档。
2. 运行 `git status --short`，确认当前分支和已有未提交修改。
3. 用户已有的修改必须保留。不要清理、覆盖或顺手格式化与当前任务无关的文件。
4. 明确任务作用于 `src/` 的迁移代码、`modules/ocarina/` 的旧代码，还是两者都需要修改。
5. 修改前检查相关 `CMakeLists.txt`，确认目标文件是否真正参与当前构建。

## 4. 架构与命名空间

新迁移代码使用小写根命名空间 `horizon`，模块命名空间如下：

```cpp
namespace horizon::core {}
namespace horizon::math {}
namespace horizon::ast {}
namespace horizon::dsl {}
```

- 类型名保持清晰的模块归属，例如 `horizon::ast::Node`、`horizon::ast::Function`。
- 不要在新迁移代码中继续引入新的 `ocarina` 命名空间。
- 旧 `modules/ocarina/` 在完成迁移前仍可能使用 `ocarina`；不要仅为统一文本而进行无边界的大规模替换。
- AST 只维护一套模型。Helicon 与 Ocarina 合并时，不要再创建平行的第二套 AST 类型。
- `core` 应承载通用基础设施；`math` 承载数学类型与算法；`ast` 承载语法树模型和遍历接口；`dsl` 承载面向用户的构造接口。
- 新增依赖时应优先保持清晰的分层。若箭头表示“依赖者指向被依赖者”，目标方向为 `dsl -> ast -> math -> core`。现有循环依赖属于迁移债务，不要在没有说明的情况下继续扩大。
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

### 6.1 推荐工作流

所有命令从仓库根目录执行。完整配置和构建优先使用 `tools/dev.py`：

```powershell
# 配置 Horizon/Helicon 核心目标族
conda run -n horizon-dev --no-capture-output python tools/dev.py configure --configuration Debug --target-family core

# 配置并构建 Horizon
conda run -n horizon-dev --no-capture-output python tools/dev.py build Horizon --configuration Debug

# 已完成配置后快速构建
conda run -n horizon-dev --no-capture-output python tools/dev.py build-fast Horizon --configuration Debug --target-family core
```

可用目标族及其构建目录：

| 目标族 | Debug preset | 构建目录 |
| --- | --- | --- |
| Horizon / Helicon | `core-debug` | `build/conan/core/debug` |
| 工具 | `tools-debug` | `build/conan/tools/debug` |
| 示例 | `examples-debug` | `build/conan/examples/debug` |
| Ocarina | `ocarina-debug` | `build/conan/ocarina/debug` |
| Ocarina 测试 | `ocarina-tests-debug` | `build/conan/ocarina-tests/debug` |
| Vision Hotfix | `vision-hotfix-debug` | `build/conan/vision-hotfix/debug` |

- 不同目标族必须使用各自独立的构建目录，禁止在同一个构建目录中切换目标族。
- `Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel` 的大小写必须保持不变。
- 只有在已经成功配置且依赖未变化时才使用 `build-fast`。
- 若用户明确要求“按 CLion 配置构建”，应使用当前 CLion 配置对应的 CMake、生成器、profile 和构建目录；不要自行改用另一套 preset 后宣称等价。
- 不要把本机 CMake、Ninja、Visual Studio 或 CLion 的绝对安装路径写入仓库文件。

### 6.2 判断构建是否覆盖改动

- 构建成功只证明该 target 实际包含的源文件能够构建。
- 修改 `src/core/`、`src/math/`、`src/ast/`、`src/dsl/` 后，若它们尚未接入当前 CMake target，必须额外进行头文件聚合检查、单文件语法检查，或先完成目标接入。
- `ninja: no work to do` 不是新迁移代码通过编译的证据。
- 若验证被尚未迁移的 generator、RHI 或其他旧模块阻断，应明确报告边界和首个外部错误，不要把它描述为本次修改已完全编译通过。

## 7. 测试与验证

- 验证范围应与风险相称：至少执行格式检查、目标构建以及与修改模块相关的测试。
- 提交前运行：

```powershell
git diff --check
```

- Ocarina 测试目标位于 `modules/ocarina/tests/`，使用 `ocarina-tests` 目标族。示例：

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build test-core-half --configuration Debug
```

- 新增测试时优先放入与功能对应的类别：`core`、`codegen`、`resources`、`math`、`runtime`、`debug` 或 `io`。
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
