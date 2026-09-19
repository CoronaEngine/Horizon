# 本次 VS/FS 与构建修复的人工审查指南

记录日期：2026-09-20。适用分支：`refactor_dsl`。

审查基线：`ff5bfd1fc724a2e9890da752b4d157e8551aa31a`。本文审查的是该提交之上的
**当前工作区改动，包括未跟踪的新文件**，不是已经提交的 Expr 命名空间迁移。
后续继续修改代码时，应刷新文件清单和验证结果。
如果 HEAD 已变化，先更新审查基线，不直接套用本文的历史验证记录。

建议先确认对外接口，再顺着构造过程读 AST 和校验，最后单独检查构建修复。
不要把“全部测试通过”直接当作语义正确：本次有一项需要优先处理的 `discard` 返回值校验问题，见第 4 节。

## 1. 先明确这次审什么

| 改动组 | 本次范围 | 不应据此认定完成的内容 |
| --- | --- | --- |
| VS/FS 功能 | 阶段构造、逻辑输入输出布局、六个 builtin getter、discard、阶段配对与 AST 校验 | HLSL 代码生成、shader 编译、pipeline、draw、GPU 执行 |
| DSL 配套修复 | 控制流回调异常传播、构造栈恢复、结构体 trait 命名空间 | 整套 Callable/Kernel 异常模型重构 |
| 构建修复 | `.venv` bootstrap、Conan 并发锁、Ninja + MSVC 的编译/链接环境恢复与头文件依赖识别 | 所有编译器和生成器通用支持、旧 CMake 缓存的工具链一致性强制校验 |
| 测试与文档 | AST/DSL 用例、构建脚本回归、设计/实施状态和使用说明 | 未提交代码的远端 CI 验证 |

审查前，从仓库根目录执行：

```powershell
git status --short
git branch --show-current
git rev-parse HEAD
git diff HEAD --name-status
git diff --cached
git ls-files --others --exclude-standard
```

- `git diff HEAD` 同时呈现已跟踪文件的暂存与未暂存改动，但不会显示未跟踪新文件的内容。
  新文件必须在 CLion 的未版本化文件列表中逐一打开阅读。
- 在 CLion 中，对已跟踪文件使用 Git 的 **Show Diff**；关注每个改动块，而不是只读修改后的整文件。
- 工作区可能混有人工修改。不能仅凭作者身份、文件位置或测试通过，将所有差异归为此次功能实现。
- 不要为了获得“干净基线”执行 reset、checkout 覆盖文件，或清理未跟踪源文件。
- 当前任务不需要进入 `modules/ocarina/`，也不需要审查或修改旧后端。

## 2. 推荐阅读顺序

第一遍只追一条成功路径：从 `VertexShader` / `FragmentShader` 的 lambda，追到
`Function::define_raster`、阶段最终化、接口布局和 `RasterShader` 配对。
第二遍再追失败路径、所有权与缓存。

| 顺序 | 文件 | 先回答的问题 |
| --- | --- | --- |
| 1 | [DSL README](../../src/dsl/README.md)、[设计文档](../superpowers/specs/2026-09-19-raster-shader-interface-design.md)、[实施计划](../superpowers/plans/2026-09-19-raster-shader-interface.md) | 用户能写什么？哪些仍是计划？HLSL builtin 映射与本期实现范围是否区分？ |
| 2 | [raster.h](../../src/dsl/api/raster.h)、[raster_builtin.h](../../src/dsl/api/raster_builtin.h)，对照 [DSL 测试](../../tests/dsl/test_raster.cpp) 的 `test_pairing_and_ownership` | 对外签名是否只使用 Var/别名？阶段如何持有 AST？组合是否延长两端生命周期？ |
| 3 | [variable.h](../../src/ast/variable.h)、[symbol_name.h](../../src/ast/symbol_name.h)、[statement.h](../../src/ast/statement.h)、[shader_interface.h](../../src/ast/shader_interface.h) | 新增的变量、语句和接口元数据分别表达什么？有没有只声明却没有行为的项？ |
| 4 | [function.h](../../src/ast/function.h)、[function.cpp](../../src/ast/function.cpp)、[function_corrector.h](../../src/ast/function_corrector.h)、[function_corrector.cpp](../../src/ast/function_corrector.cpp) | AST 在哪里创建、归属谁？构造失败后栈是否恢复？最终化的顺序是否合理？ |
| 5 | [shader_interface.cpp](../../src/ast/shader_interface.cpp)、[raster_validation.h](../../src/ast/raster_validation.h)、[raster_validation.cpp](../../src/ast/raster_validation.cpp) | 布局规则、写权限、阶段规则、调用链和诊断是否与设计一致？ |
| 6 | 回到 [raster.h](../../src/dsl/api/raster.h)，再读 [stmt_builder.h](../../src/dsl/api/stmt_builder.h)、[syntax.h](../../src/dsl/api/syntax.h)、[struct.h](../../src/dsl/types/struct.h)、[dsl.h](../../src/dsl/dsl.h) | 包装有没有绕过 AST 校验？为什么要改已有控制流和结构体宏？公共头是否可独立包含？ |
| 7 | [AST 测试](../../tests/ast/test_raster.cpp)、[DSL 测试](../../tests/dsl/test_raster.cpp)、[拒绝检查脚本](../../tests/ast/expect_raster_rejection.cmake)，以及 [AST CMake](../../tests/ast/CMakeLists.txt)、[DSL CMake](../../tests/dsl/CMakeLists.txt) | 测试是否验证需求而非重复实现？测试是否真正注册并参与 horizon-tests？ |
| 8 | 按第 5 节读构建修复，最后复核 [架构概览](../architecture/overview.md) 与 [根 README](../../README.md) | 构建是否覆盖新代码？文档有没有夸大阶段、平台或验证范围？ |

每个改动块至少记下三件事：它解决的问题、如果删除它会出现的具体错误、证明其必要性的测试或调用链。
无法说明用途的改动先标记待说明，不要仅因“以后可能会用到”接受它。

## 3. AST / DSL 的具体审查方式

### 3.1 沿成功路径逐层核对

在 `raster.h` 找到 `RasterStage` 构造器，再按下列顺序使用跳转到定义：

```text
用户 lambda
  → VertexShader / FragmentShader
  → detail::RasterStage
  → Function::define_raster / begin_raster
  → 创建参数、表达式、语句和返回值
  → finalize_raster
      → 调用图预检
      → FunctionCorrector
      → build_shader_interface
      → validate_raster_function
      → reset_raster_hashes
  → RasterShader
      → C++ 签名约束
      → validate_raster_pair
```

这表示 host 端构建 AST 的过程；lambda 此时执行，不是 GPU 已经运行 shader。

- [ ] `VertexShader`、`FragmentShader` 和 `RasterShader` 沿用同一套 `ast::Function`，没有另建 AST。
- [ ] 六个 getter 返回 `Float4`、`Uint`、`Bool` 等 Var 别名；用户示例不要求使用 `detail::Expr`。
- [ ] 值参数和 `const Var<T>&` 的规则清楚；裸指针、可写引用、错误返回类型在相应边界被拒绝。
- [ ] 阶段持有 `shared_ptr<const Function>`，组合持有阶段副本；lambda 临时对象销毁后 AST 仍有效。
- [ ] 对移动后对象的处理明确；不允许把无效阶段组装成可用的 RasterShader。

### 3.2 用一份嵌套结构手算接口布局

不要只看 slot 数量。选取测试中的嵌套结构，先在纸上列出成员顺序，再与 `flatten` 的输出逐项比较：

- `root_index` 是否指向正确的参数；输出根是否为 0。
- `member_path` 是否完整保留嵌套成员路径。
- 输入、输出各自从 location 0 开始，每个合法叶子占一个 location。
- builtin 不占普通 location；FS 输出 location 对应颜色 attachment。
- varying 浮点为 Smooth、整数为 Flat；VS 输入和 FS 输出为 None。
- void、空结构体和非法叶子类型是否区别处理。
- `real` 经精度策略解析后的实际 AST 类型是否参与校验和配对，而不只是比较 C++ 类型名。

对应测试入口：`test_recursive_interface_layout_and_hash`、
`test_interface_rejections_and_declared_returns`、`test_interface_precision_policy`、
`test_resolved_precision_pair`、`test_stage_signatures_and_layout`。

### 3.3 从一次非法写入反向追踪根变量

对 `fragment_coord().x = 0.0f`，从 AssignStmt 的左值一路追到 builtin 根变量。
然后换成 swizzle、下标、Callable 引用实参和 ForStmt 隐式步进，检查它们是否经过同一权限规则。

- [ ] 只读性由 AST 变量权限决定，不能因为外层类型是 Var 就允许写入。
- [ ] `auto coord = fragment_coord()` 和移动包装保留根变量权限。
- [ ] 从已有 Var 左值复制出 Local 后，可以修改 Local，且不会写回 builtin。
- [ ] 同一个 Callable 在不同调用点绑定不同实参时，不能复用错误的只读判断。
- [ ] VS 的 position 写入可经过合法引用调用被识别；读取其他只读 builtin 不会误算为写 position。

对应测试入口：`test_readonly_roots_and_reference_calls`、
`test_builtin_aliases_and_local_copies`、`test_review_readonly_and_return_regressions`。

### 3.4 单独审查调用链、异常和 hash

- 对同一 Callable，先从 FS 使用，再从 VS 使用；阶段限制必须按实际入口重新检查。
- 检查递归调用、Kernel 中的 raster builtin/discard、物理资源捕获和不允许的阶段操作。
- 对 lambda、if/switch/loop/for/scope 回调分别抛异常；之后马上构造一个正常 shader，检查 Function/Scope 栈。
- 对照 `stmt_builder.h` 与 `syntax.h` 的 diff，确认移除 noexcept 的位置确实会执行可能抛异常的回调；
  关注 `while_` 从 `operator*` 调整为 `operator/` 的回调签名原因。
- 在构造中提前查询 hash，再添加语句或调用；检查最终 hash 不会沿用构造中缓存，组合也不应修改阶段 hash。
- 结构体宏的 trait 应落在真实所有者 `horizon::math`；不要默认不同模块的 detail 是同一命名空间。

对应测试入口：`test_reachable_callables_and_recursion`、`test_failure_recovery_and_no_current`、
`test_control_flow_exception_recovery`、`test_hash_queries_during_construction`。

## 4. 优先处理的未决问题与容易误读的差异

### 4.1 discard 不能仅凭“出现过”就豁免缺失返回

当前 `raster_validation.cpp` 的 `on_discard()` 会把所有调用栈 frame 的 `has_discard` 设为 true；
函数结束时，只要该标记为 true，非 void 函数没有值返回也可通过检查。
AST 测试 `test_review_readonly_and_return_regressions` 中的两个正例明确接受这种行为。

这应作为**优先语义问题**，不能拿现有测试通过作为正确性的证明：

1. walker 会访问 if 的两个分支以及循环体，“AST 中可访问到 discard”不等于运行时必经。
2. 丢弃片元输出不应直接等同于从函数及所有调用者 return。微软对
   [Direct3D discard 指令](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/discard--sm4---asm-)
   的说明指出，标记丢弃后仍可继续执行以支持导数计算。
3. 设计中的返回类型检查，没有授权“任意可达 discard 代替非 void 返回”这一额外规则。

审查动作：先确认 DSL 期望的 discard 合同，再修改相应校验、测试和 DSL README。
至少补查“仅在条件分支中 discard”“被调用函数 discard 后调用者继续执行”“非 void 函数没有返回”三个情形。
在结论明确前，不应批准这项返回值豁免。

### 4.2 明确哪些是约定边界，哪些还没实现

- position 目前只检查 AST 遍历范围内是否存在写入，不证明每条执行路径都写出完整位置。
  若接受这一期边界，应在审查结论中明确记录；不能表述为完整控制流验证。
- 这里只构建、验证 host AST，没有验证 HLSL 后端、索引偏移的真实 GPU 数值、光栅输出或性能。
- `draw_index()` 保留已有 Slang 扩展语义；不要将它误写为本期已验证的原生 Direct3D HLSL 系统语义。
- 旧 CMake 缓存中编译器与 Conan 环境不一致，目前没有强制阻断检查，见第 5 节。

### 4.3 statement.h 的历史和当前差异

审查快照中，`Warning` 已被删除，`Discard` 位于原来的枚举位置。
基线提交本来就有 `Warning`，更早的 `0b70bbed`（2026-08-11）已有大写 `WARNING`，
`04c8025c` 将其重命名；不能把它算作这次新增功能。
应把删除旧未使用标签和新增 discard 节点分开判断，并检查是否有外部缓存依赖枚举数值。

当前 diff 还删掉了 `Statement` 的显式析构声明。其基类
[ASTNode](../../src/ast/ast_node.h) 和 [Hashable](../../src/core/util/hash.h) 有虚析构，
因此隐式析构仍为 virtual；不能据此报告“通过基类删除时不再虚析构”。
但这是否属于本次功能必要改动，仍应与工作区其他人工修改分开确认。

## 5. 构建修复单独审查

| 顺序与文件 | 检查重点 |
| --- | --- |
| [CMakeLists.txt](../../CMakeLists.txt) | bootstrap 在 project 前，launcher 在 project 后且在创建目标前生效；新 AST 源文件经 [src/ast/CMakeLists.txt](../../src/ast/CMakeLists.txt) 的 glob 纳入目标。 |
| [horizon_dev_bootstrap.cmake](../../cmake/horizon_dev_bootstrap.cmake) | 已有 `.venv` 优先，没有时使用 Conda；失败应明确退出，递归保护有效；目标族和配置不混用。 |
| [workflow.py](../../tools/workflow.py) | `conan_cache_lock` 按 Conan home 加锁，覆盖 export → install → 环境生成，异常后释放；Windows/Linux 分支分别检查。环境快照包含 PATH、INCLUDE、LIB、LIBPATH、VSLANG。 |
| [horizon_msvc_build_environment.cmake](../../cmake/horizon_msvc_build_environment.cmake) | 只适用于 Windows + Ninja + MSVC；每个构建目录有自己的快照；C/C++ 编译和链接都恢复环境，保留已有 launcher。 |
| [horizon_run_build_tool.cmake](../../cmake/horizon_run_build_tool.cmake) | 环境先恢复再运行工具；空格、分号、引号、反斜杠和 bracket delimiter 不改变实参；包含文件前缀规范化后 Ninja 可识别；编译失败不能变成成功。 |
| [test_workflow.py](../../tools/test_workflow.py) | 污染的 LIB 环境、无控制台编码、参数边界、原始错误传播、并发事务和异常解锁均有回归；测试没有只比对生产源码字符串来替代关键行为验证。 |

需要追问的边界：

- 新机器应从源码重新配置，不复制 `.idea`、构建目录或生成的环境快照。
- MSVC/SDK 版本、安装路径变化后使用新的构建目录。仅 Reload 不保证清除旧编译器路径；
  当前 launcher 恢复环境，但不会主动拒绝或替换 CMake 缓存中的另一版编译器。
- Conan 锁只约束使用该锁的 Horizon 开发脚本，不能与手动 Conan 命令或其他项目自动协调。
- 不同目标族、Debug/Release 使用各自目录；Ninja Multi-Config 也仍被固定到该目录对应的 Conan 配置。
- 当前真实构建验证来自本机 Windows/MSVC。非 Windows 的锁实现、其他生成器和不同语言/非 ASCII 路径
  需要分别验证；不能从本机通过推断全部兼容。

## 6. 如何实际验证

### 6.1 先确认测的是原 CLion Debug

在 CLion 选择原有 **Debug** profile，执行 **Reload CMake Project**。
不要用 `core-debug` 的结果代替此 profile。本文记录时，原 Debug 的实际配置是：

| 项目 | 值 |
| --- | --- |
| 构建目录 | `cmake-build-debug` |
| CMake / generator | CLion 自带 CMake 4.0.2 / Ninja |
| 配置 / 目标族 | Debug / examples |
| 测试开关 | `HORIZON_BUILD_TESTS=ON` |
| All CTest 构建目标 | `all`，之后执行 CTest；并不是只构建两个 raster 测试 |

首先运行两个 raster 测试 target，再运行 **All CTest**。关注构建日志里是否真的重新编译受影响源文件。
需要全量复核时，在该 profile 下 Clean 后重建；清理操作只针对生成物，不删除源码改动。

### 6.2 命令行复核：读取 CLion 缓存中的工具路径

以下命令在仓库根目录运行，不写死任何机器的 CMake、Ninja 安装路径。
前提是 CLion 的配置已成功生成。它们复核相同目录和目标，不代替一次真实 IDE 构建的启动环境验证。

```powershell
$reviewBuild = (Resolve-Path .\cmake-build-debug).Path
$reviewCache = @{}
Get-Content -LiteralPath (Join-Path $reviewBuild 'CMakeCache.txt') | ForEach-Object {
    if ($_ -match '^([^#/][^:]*):[^=]*=(.*)$') {
        $reviewCache[$matches[1]] = $matches[2]
    }
}
$reviewCmake = $reviewCache['CMAKE_COMMAND']
$reviewCtest = $reviewCache['CMAKE_CTEST_COMMAND']
$reviewNinja = $reviewCache['CMAKE_MAKE_PROGRAM']

'CMAKE_GENERATOR', 'CMAKE_BUILD_TYPE', 'HORIZON_DEV_CONFIGURATION',
'HORIZON_DEV_TARGET_FAMILY', 'HORIZON_BUILD_TESTS' | ForEach-Object {
    "$_=$($reviewCache[$_])"
}

& $reviewCmake --build $reviewBuild --config Debug --target horizon-test-ast-raster horizon-test-dsl-raster
if ($LASTEXITCODE -ne 0) { throw 'Raster 构建失败，先检查首个错误' }
& $reviewCtest --test-dir $reviewBuild -C Debug -R '^horizon\.(ast|dsl)\.raster' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Raster 测试失败' }

& $reviewCmake --build $reviewBuild --config Debug --target all
if ($LASTEXITCODE -ne 0) { throw 'all 构建失败，不能继续宣称测试覆盖当前源码' }
& $reviewCtest --test-dir $reviewBuild -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest 失败' }
```

开发脚本与静态差异检查：

```powershell
$reviewOriginalPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $reviewCmake);$reviewOriginalPath"
    .\.venv\Scripts\python.exe -m unittest tools.test_workflow
    if ($LASTEXITCODE -ne 0) { throw '开发脚本测试失败' }
} finally {
    $env:PATH = $reviewOriginalPath
}
git diff --check
```

没有项目 `.venv` 时，按根 README 使用 Conda 环境运行同一 unittest 模块。
确认测试实际找到并使用了上述 CLion CMake。本文快照在 Windows 下应执行 22 项，且没有 skipped；
找不到 CMake 时两个 launcher 回归会跳过，不能仅凭 unittest 返回成功判断它们已验证。
`git diff --check` 不会检查未跟踪文件的内容；新文件还需检查格式、编码和 CMake 接入。

### 6.3 专门复查“改头文件却没有重新编译”

先使用上一节获得的 Ninja 路径查询 AST 对象依赖：

```powershell
& $reviewNinja -C $reviewBuild -t deps 'src/ast/CMakeFiles/horizon-ast.dir/function.cpp.obj'
```

结果应含 `src/ast/statement.h`，依赖有效且非零。历史记录为 40 条，但不要把固定数量当作长期要求。

如需实际验证错误能被捕获，在保存现有差异并备份文件后，临时在 `statement.h` 的 `#pragma once`
后加入 `#error HORIZON_REVIEW_HEADER_PROBE`，在 CLion 原 Debug 中构建 `horizon-ast`。
期望立即在该头文件报告错误。随后**只撤销这一行探针**并重新构建，不要用整文件回退覆盖已有修改。

`ninja: no work to do`、旧 exe 能运行或 CTest 单独全绿，都不能证明最新头文件已被编译。

### 6.4 不要漏掉负例和复用场景

| 场景 | 应检查的结果 |
| --- | --- |
| VS/FS 使用不属于自身阶段的 builtin；Kernel 使用 raster builtin/discard | 拒绝，并能识别诊断原因 |
| 只读输入的成员、下标、swizzle 或 Callable 引用实参被写入 | 拒绝；已有左值复制出的 Local 可写 |
| 同一个 Callable 先用于 FS、再用于 VS | 第二次按实际入口重新校验，不受首次结果污染 |
| VS/FS 类型、解析精度或布局不匹配 | 对应编译期约束或运行期 AST 配对拒绝 |
| 构造/控制流回调抛异常后再次构造 | 下一次构造正常，Function/Scope 没有残留 |
| 构造中提前查询 hash，随后追加语句 | 最终 hash 反映完整 AST |
| 非 void 函数无返回但出现 discard | 当前测试接受；这正是第 4.1 节待纠正的审查问题，不能视作预期成功 |
| 两个配置并发 bootstrap，共用 Conan home | 一个等待另一个完成完整事务；失败后不残留锁占用 |
| IDE 带旧版 INCLUDE/LIB，无控制台启动 | 编译、链接恢复配置环境；头文件依赖正常记录 |
| 旧缓存中的编译器与新 Conan 环境不同 | 当前没有自动阻断；这是仍需补充的防护，不要写成已通过的负例 |

## 7. 已有证据与本次文档验证的边界

以下是本会话此前的本机验证记录，**不是撰写本文时重新执行的结果**：

| 记录 | 结果 | 能证明什么 |
| --- | --- | --- |
| 原 CLion Debug 目录，以无控制台方式并带旧版工具路径配置、全量构建 all | 成功 | 当时工作区在该模拟启动条件下可以构建 |
| 同一目录的 CTest | 20/20 通过 | 当时 host 测试通过，不含 GPU/后端验证 |
| `tools.test_workflow` | 22/22 通过 | 当时构建脚本回归通过 |
| 最小真实 MSVC/Ninja 项目，终端及无控制台模式 | 构建成功；修改测试头文件触发预期失败 | 环境恢复及头文件增量依赖可以工作 |
| 原 Debug 的 `function.cpp.obj` | 40 条依赖，包含 statement.h | 当时 Ninja 记录了这个关键头文件 |

上述完整构建通过后，`statement.h` 又发生了编辑，因此不能把那次 20/20 当作最新工作区的复验。
审查者应按第 6 节对最后一次源代码修改后的状态运行验证，并记录实际命令、配置和首个错误。
远端基线提交的 CI 结果也不覆盖这些未提交改动。

本次文档任务只增加审查指南及实施计划中的入口，不修改生产代码；验证限于文档路径、命令/符号核对和差异检查。

## 8. 如何记录结论

按问题给出具体反馈，避免只写“这里不对”或“测试通过”：

```text
文件 / 函数：
触发条件或最小例子：
当前行为：
设计或接口要求的行为：
影响范围：
建议修改与应增加的验证：
是否阻止接受本次改动：
```

结束前逐项确认：

- [ ] 已读完未跟踪新文件，而不只是 git diff 中已有文件。
- [ ] 每个新增接口有当前用途，公开 API 不要求使用内部 Expr。
- [ ] 已对 discard 返回值豁免给出明确结论，未用当前正例测试掩盖语义问题。
- [ ] 已区分本期范围限制、已确认问题和单纯的个人风格偏好。
- [ ] 最后一次代码修改后，原 CLion Debug 的构建及相关 CTest 已重新执行。
- [ ] 已确认头文件依赖有效，记录构建环境与未覆盖平台。
- [ ] 构建脚本的并发、参数透传、失败传播与编码回归已通过。
- [ ] 文档的“当前实现”和“未实现”与最终代码一致。
- [ ] 审查结论明确写为“通过”“修改后复审”或“仅通过某一改动组”，并列出遗留项。

建议分别记录 VS/FS 功能与构建修复的结论。一个组通过不能替代另一个组的审查；
完成审查也不自动授权提交、推送或创建 PR。
