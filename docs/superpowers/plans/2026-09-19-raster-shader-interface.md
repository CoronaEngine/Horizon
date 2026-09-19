# VS/FS Shader Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在新 AST/DSL 中实现可构造、可校验、可组合的 VS/FS 对外接口。

**Architecture:** 每个阶段持有一个现有 `ast::Function`，普通表达式和语句继续共用唯一 AST。
阶段接口布局单独存储，DSL 负责 lambda 包装，RasterShader 只组合和校验。
本期不引入编译器、RHI 或物理 GPU 资源。

**Tech Stack:** C++20、CMake、Conan 2、MSVC、Ninja Multi-Config、现有轻量 C++ 测试与 CTest。

**Spec:** [VS/FS 接口设计](../specs/2026-09-19-raster-shader-interface-design.md)。

**人工审查：** [本次改动的阅读顺序、检查重点与验证流程](../../reviews/2026-09-20-raster-and-build-review.md)。

**执行状态（2026-09-19）：** 六项功能任务已在当前工作区实现，尚未提交或推送。
core Debug 构建 `horizon-tests`、CTest 20/20 通过；独立 core Release 构建与
AST/DSL/architecture CTest 10/10 通过。新文件的 clang-format 检查与 `git diff --check` 通过。
本机以现有 `.venv/Scripts/python.exe` 执行 `tools/dev.py`，替代下文 Conda 启动前缀；
目标族、配置和验证范围不变。当前只完成 host AST 能力，未验证后端编译或 GPU。
独立审查发现的五项边界问题已补回归并修复；控制流异常传播涉及
`src/dsl/api/stmt_builder.h` / `syntax.h` 的必要调整，同时修正 `while_` 的回调适配。

## Global Constraints

- 使用 C++20；主验证平台为 Windows x64、MSVC、Ninja Multi-Config。
- 生产代码修改限于 `src/ast/` 和 `src/dsl/`；配套测试与文档单独列入执行范围。
- 依赖保持 `dsl -> ast -> math -> core`，不新增第三方依赖。
- AST 只维护一套模型；所有节点由所属 `ast::Function` 创建和持有。
- 新符号使用 `horizon::ast` / `horizon::dsl`；类型 CamelCase，函数与变量 snake_case。
- `horizon::dsl::detail::Expr<T>` 仅供内部实现使用；新公开 shader 值接口及用户示例使用
  `Var<T>` 或其别名，不能通过显式签名或 `auto` 返回内部表达式类型。
- 不查阅或修改 `modules/ocarina/`；Helicon 仅作本任务已获授权的只读参考。
- 不修改 Helicon、RHI、Runtime、Conan 或目标族配置，不把 `Helicon` 链接到 `horizon-dsl`。
- 所有构建命令从仓库根目录执行，使用独立的 `core` 构建目录。
- 实现期遵守执行时的 `AGENTS.md`；本文不自动授权未来代码提交或推送。

## HLSL VS builtin 映射与 Helicon 实现

本计划以 HLSL 表达 shader 接口，不要求增加 GLSL 接口或生成器。下表限定为现代原生
Direct3D HLSL 的 VS 系统输入输出语义，不包含 D3D9 兼容语义、其他阶段的系统值或
普通内建函数。HLSL 的阶段分类依据 [VS 语义表](https://llvm.github.io/wg-hlsl/proposals/0040-semantics-overview/#vertex-shader)。
“Helicon 已实现”表示在当前源码中确认了公开包装、AST 工厂与语义生成路径，不代表
本次执行过 shader 编译或 GPU 验证；“本期计划”列中的六个 builtin 已实现 host AST 包装，
标为“本期未覆盖”的系统值仍未实现。

| HLSL VS 系统语义 | 方向、作用与要求 | Helicon 当前接口 | 新 AST/DSL 本期计划 |
| --- | --- | --- | --- |
| `SV_VertexID` | 输入：顶点索引 | `vertexIndex()`，生成 `SV_VertexID` | `vertex_index() -> Uint`，只读；数值语义见下文 |
| `SV_InstanceID` | 输入：实例索引 | `instanceIndex()`，生成 `SV_InstanceID` | `instance_index() -> Uint`，只读；数值语义见下文 |
| `SV_ViewID` | 输入：视图编号；SM 6.1+，需要视图实例化支持 | 未实现对应 builtin | 本期未覆盖 |
| `SV_StartVertexLocation` | 输入：起始顶点位置或 indexed draw 的 base vertex；`int`，SM 6.8+ 与 ExtendedCommandInfo 能力 | 未实现对应 builtin | 本期未覆盖 |
| `SV_StartInstanceLocation` | 输入：起始实例位置；`uint`，SM 6.8+ 与 ExtendedCommandInfo 能力 | 未实现对应 builtin | 本期未覆盖 |
| `SV_Position` | 输出：齐次裁剪空间位置，`float4` | `position()`；在 VS 中生成系统输出 | `vertex_position() -> Float4`，可读写的 builtin 输出 |
| `SV_ClipDistance[n]` | 输出：用户裁剪距离，浮点标量或向量 | 未实现对应 builtin | 本期未覆盖 |
| `SV_CullDistance[n]` | 输出：用户剔除距离，浮点标量或向量 | 未实现对应 builtin | 本期未覆盖 |
| `SV_ViewportArrayIndex` | 输出：viewport 索引；VS 直接送入光栅器且设备支持该阶段输出 | 未实现对应 builtin | 本期未覆盖 |
| `SV_RenderTargetArrayIndex` | 输出：渲染目标数组层；VS 直接送入光栅器且设备支持该阶段输出 | 未实现对应 builtin | 本期未覆盖 |
| `SV_ShadingRate` | 输出：图元着色率；SM 6.4+，需要 VRS Tier 2 等能力支持 | 未实现对应 builtin | 本期未覆盖 |

这 11 类中，Helicon 已有 3 类对应接口，另外 8 类未提供对应 builtin 包装或生成逻辑。
源码依据为 [BuiltinVariate.h](../../../src/Helicon/Codegen/BuiltinVariate.h)、
[AST.cpp](../../../src/Helicon/Codegen/AST/AST.cpp) 与
[SlangGenerator.cpp](../../../src/Helicon/Codegen/Generator/SlangGenerator.cpp)。
较新系统值及设备限制参考微软的 [ViewID 文档](https://github.com/microsoft/DirectXShaderCompiler/wiki/SV_ViewID)、
[起始顶点与实例位置规范](https://microsoft.github.io/hlsl-specs/proposals/0015-extended-command-info/)、
[VRS 规范](https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html) 和
[系统语义文档](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics)。

### Helicon 的额外 draw builtin

| 语义来源 | Helicon 当前接口 | 新 AST/DSL 本期计划 |
| --- | --- | --- |
| Slang 的 `SV_DrawIndex`，不属于上表的原生 Direct3D HLSL VS 系统语义 | `drawIndex()`，AST 与生成路径已实现 | `draw_index() -> Uint`，只读 builtin 输入 |

### 数值语义与后端边界

HLSL 是接口语言，不能由“只支持 HLSL”推断目标后端只有 Direct3D。Helicon 的
[RasterizedPipelineObject.h](../../../src/Helicon/Codegen/RasterizedPipelineObject.h)
当前取得的是 SPIR-V；生成器按 Slang 到 SPIR-V 的路径使用上述语义名称，并明确记录：

- `vertexIndex()` 用于 vertex pulling；indexed draw 下包含 `vertexOffset`。
- `instanceIndex()` 包含 `firstInstance`；当 instance count 为 1 时可作为逐 draw 数据下标。
- `drawIndex()` 提供 draw 标识；接口注释说明直接 draw 下为 0，不能无条件替代上述逐 draw 下标。

新 AST/DSL 本期继续沿用设计第 6 节记录的数值约定。这张表中的名称映射不表示
与原生 Direct3D HLSL 的数值语义完全相同：后者把 ID 与起始顶点、实例偏移分开表达，
详见[扩展命令信息规范](https://microsoft.github.io/hlsl-specs/proposals/0015-extended-command-info/#motivation)。
未来接入具体编译后端时，再核对偏移处理与能力支持；不能仅因使用 HLSL 就删除
`draw_index()`、改写现有索引约定，或以伪造常量替代不支持的系统值。

上述对照记录覆盖范围，不把 8 类未覆盖系统值自动加入本期任务。本期仍实现设计中的
六个 getter（四个 VS builtin、两个 FS builtin）与 `discard()`，公开类型为 `Var<T>`
或 `Uint`、`Float4`、`Bool` 等别名，权限由 AST 校验。

普通 `POSITION`、`NORMAL`、`TEXCOORD`、`COLOR` 等顶点属性输入对应 VS lambda 参数，
普通 varying 输出对应返回值或返回结构体成员，不计入这 11 类 builtin。
Helicon 的 `position()` 在 FS 中还会生成位置输入，新 DSL 将其分为 `fragment_coord()`；
`front_facing()` 对应 FS 的 `SV_IsFrontFace`，也不计入 VS 表。

## Review Focus

1. 同一个 Callable 先用于 FS 再用于 VS，阶段限制不得被首次使用或缓存污染（任务 3、5）。
2. input 的成员、swizzle 与引用参数间接写入都必须拒绝，复制后的局部值应可写（任务 3、4）。
3. 构造 lambda 或验证抛异常后，下一次构造不得继承残留 Function/Scope（任务 1、4）。
4. 同一 C++ 接口类型在不同精度策略下解析不同，配对必须检查实际 AST 类型（任务 2、5）。
5. 无 varying、无颜色输出、嵌套 MRT 及对象生命周期不得依赖默认对象或临时 lambda（任务 4、5）。
6. builtin 的公开返回类型统一为对应 `Var<T>`；只读性由 AST 根变量权限决定，直接包装或
   移动保持原权限，从已有包装对象左值拷贝才产生可写 Local（任务 3、4）。

## 0. 换机接手与基线

历史交接基线（不表示当前完成状态）：设计依据提交为
`8d015222a37dc46d8355c0d2ab964758aeb555c0`，文档随 `refactor_dsl` 分支传递。
不要把文档提交视为已有功能，也不要从 Helicon 的构建结果推断新 AST 可编译。

后续基线变更：`ff5bfd1fc724a2e9890da752b4d157e8551aa31a` 已将 `Expr<T>` 移入
`horizon::dsl::detail`。本计划已调整 builtin 的公开返回类型及权限测试；这项已完成的
命名空间迁移不属于下列 VS/FS 功能，不要为 raster API 重新导出 `Expr`。

在新机器已有 clone 且工作区干净时：

```powershell
git fetch origin
git switch refactor_dsl
git pull --ff-only origin refactor_dsl
git status --short
git log -3 --oneline
```

若分支存在本地分歧或未提交修改，保留它们并按实际情况处理；不 reset、不强推。
先读根 AGENTS.md、设计文档与本计划，再执行。任务互相依赖，建议同一实施者顺序完成；
执行方法由新机器上的用户指令决定，不因本文件的通用技能提示自动启动并行代理。

工具环境与 [README](../../../README.md) 的 Windows 指南一致：Conda `horizon-dev`、
Python >= 3.11、Conan >= 2.28 且 < 3、CMake >= 4.0、Ninja、MSVC x64/Windows SDK。
不存在环境时可按 README 创建；不复制旧机器的绝对安装路径或 build 目录。

已检查 `tools/dev.py`：`core` 目标族设置 `with_engine=False`、`with_tests=True`，
`horizon-test-*` 归到 core；旧的 `test-*` 前缀会归到 Ocarina，新增测试不得使用后者。
README 中把 core 描述成 Horizon/Helicon 的旧表述不作为本计划的构建依据。

基线命令：

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build horizon-tests --configuration Debug --target-family core
conda run -n horizon-dev --no-capture-output ctest --test-dir build/conan/core/debug -C Debug --output-on-failure
```

保存首个有因果价值的失败。若基线失败，先判定是否在本任务边界内，不能通过扩大范围进入
旧模块来“修好环境”。本期不要求 CUDA、Slang 或可用 GPU。

## 文件职责

| 文件 | 操作与职责 |
| --- | --- |
| `src/ast/shader_interface.h/.cpp` | 新增接口 slot、默认插值和递归布局 |
| `src/ast/raster_validation.h/.cpp` | 新增诊断、异常、阶段与配对验证 |
| `src/ast/function.h/.cpp` | 阶段入口工厂、接口所有权、builtin、hash |
| `src/ast/variable.h`、`symbol_name.h` | StageInput 和六个 builtin 的 tag/名称 |
| `src/ast/statement.h/.cpp` | DiscardStmt 及 Visitor |
| `src/ast/function_corrector.h/.cpp` | raster Context/捕获校正，保留 Kernel 专用参数拆分 |
| `src/dsl/api/raster_builtin.h` | 新增光栅化 builtin 构造 API |
| `src/dsl/api/raster.h` | 阶段包装、CTAD、配对 traits 与 RasterShader |
| `src/dsl/api/func.h` | 仅在无法直接复用 traits 时做最小调整，不改 Kernel/Callable 对外语义 |
| `src/dsl/dsl.h` | 引入 raster 公共入口 |
| `tests/ast/test_raster.cpp` | 新增 AST 构造、布局、诊断与调用图测试 |
| `tests/ast/expect_raster_rejection.cmake` | 原 noexcept Kernel 的非法操作独立进程验证 |
| `tests/dsl/test_raster.cpp` | 新增公开接口与组合测试；单独包含 raster.h 验证自包含性 |
| `tests/ast/CMakeLists.txt`、`tests/dsl/CMakeLists.txt` | 接入两个测试 executable 与 CTest |
| `docs/architecture/overview.md` | 实现完成后更新当前状态和新入口链接 |
| `src/dsl/README.md` | 实现完成后新增简短用法与明确限制 |

AST/DSL CMake 使用 CONFIGURE_DEPENDS glob；新 cpp/h 在重新 Configure 后进入已有库，
无需为 raster 新建生产 target。测试 cpp 放在 tests，不能放进 src 的递归 glob。

## 任务 1：入口分类、诊断和安全构造

**Files:** 修改 Function、FunctionCorrector；创建 shader_interface.h（数据类型）、
raster_validation.h/.cpp、tests/ast/test_raster.cpp；修改 tests/ast/CMakeLists.txt。

**Interfaces:**

- 提供 `Function::Tag::Vertex/Fragment`、`is_vertex/is_fragment/is_raster/is_entry_point`。
- 提供 `Function::define_raster(Tag, const Type *, Func &&) -> shared_ptr<Function>`。
- 提供设计第 7 节的 RasterDiagnosticCode、RasterDiagnostic、RasterValidationError。
- `RasterValidationError::diagnostics()` 返回 `span<const RasterDiagnostic>`。
- ShaderInterface 数据结构采用设计第 4 节，Function 暂可保存空接口。

- [x] 创建测试 target，先用未提供的新接口产生预期编译失败。沿用现有 `expect(bool, const char *)`
  与 failures 计数方式，不增加测试框架。基本用例：

```cpp
auto fs = horizon::ast::Function::define_raster(
    horizon::ast::Function::Tag::Fragment, nullptr, [] {});
expect(fs->is_fragment() && fs->is_entry_point() && !fs->is_kernel(),
       "fragment entry is distinct from a compute kernel");
expect(fs->body()->check_context(fs.get()), "fragment owns its body");
expect(horizon::ast::Function::current() == nullptr, "stack restored");

bool threw = false;
try {
    horizon::ast::Function::define_raster(
        horizon::ast::Function::Tag::Fragment, nullptr,
        [] { throw std::runtime_error("construction probe"); });
} catch (const std::runtime_error &) {
    threw = true;
}
expect(threw && horizon::ast::Function::current() == nullptr,
       "throwing lambda restores construction context");
auto next = horizon::ast::Function::define_kernel([] {});
expect(next->body()->check_context(next.get()), "subsequent kernel is valid");
```

- [x] 接入 CMake，明确测试名：

```cmake
add_executable(horizon-test-ast-raster test_raster.cpp)
target_link_libraries(horizon-test-ast-raster PRIVATE horizon-ast)
target_compile_features(horizon-test-ast-raster PRIVATE cxx_std_20)
add_dependencies(horizon-tests horizon-test-ast-raster)
add_test(NAME horizon.ast.raster COMMAND horizon-test-ast-raster)
```

- [x] 执行 Configure/build，记录首个新 API 未定义错误：

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build horizon-test-ast-raster --configuration Debug --target-family core
```

- [x] 增加 enum 和查询，保留旧 Tag 数值与 `is_kernel()` 行为。新 factory 使用独立非 noexcept
  构造链；按下列顺序实现，RAII 的析构路径必须可用：

```text
检查 tag 为 Vertex/Fragment -> 分配 Function -> 固定声明返回类型
push + 自动 pop guard -> with(body, lambda) -> 退出 scope/pop
调用图递归环预检 -> correct（不拆 raster 参数）-> 生成接口并验证
有诊断抛 RasterValidationError -> 否则返回
```

  本任务先实现入口/Context/异常基础；布局由任务 2 补齐，builtin 规则由任务 3 补齐。
  不调用会使异常触发 terminate 的旧 noexcept `_define()`；复用 `with()` 已有的 ScopeGuard。
- [x] 核查 Corrector 中 `is_kernel()` 的每个用途，区分“入口通用校正”和“仅 Kernel 参数拆分”；
  将 Context 校验推广到 raster，保留 splitting_arguments 的 Kernel 条件。
- [x] 增加无效 tag 诊断、嵌套 raster 构造抛异常后恢复外层 current，以及原 Kernel/Callable
  分类回归。异常诊断测试按 code 比较，不能只要求“抛了任何异常”。
- [x] 运行以下命令，要求本任务测试与原 AST 测试通过：

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build horizon-tests --configuration Debug --target-family core
conda run -n horizon-dev --no-capture-output ctest --test-dir build/conan/core/debug -C Debug -R '^horizon\.ast(\.|$)' --output-on-failure
```

## 任务 2：递归接口布局、返回类型和 hash

**Files:** 创建 shader_interface.cpp；修改 Function、variable.h、symbol_name.h、
raster_validation.cpp 和 tests/ast/test_raster.cpp。

**Interfaces:** `Function::shader_interface() -> const ShaderInterface &`；新增 StageInput tag。
输入槽 root_index 对应 arguments 下标，输出槽 root_index 为 0；字段定义不增加物理布局数据。

- [x] 增加 FS AST 用例：float3 参数、float4 返回，确认输入 Smooth/输出 None，各从 location 0 起。
  非 void 返回由 C++ 包装以后提供，此处用低层 AST 显式构造：

```cpp
using namespace horizon::ast;
using horizon::core::Type;
using horizon::math::float3;
using horizon::math::float4;
auto fs = Function::define_raster(Function::Tag::Fragment, Type::of<float4>(), [] {
    auto *f = Function::current();
    (void)f->argument(Type::of<float3>());
    f->return_(f->local(Type::of<float4>()));
});
const auto &io = fs->shader_interface();
expect(io.inputs.size() == 1 && io.outputs.size() == 1, "input and output slots");
expect(io.inputs[0].location == 0 && io.inputs[0].member_path.empty(), "root input path");
expect(io.inputs[0].interpolation == Interpolation::Smooth, "float varying is smooth");
expect(io.outputs[0].interpolation == Interpolation::None, "color is not interpolated");
```

- [x] 运行任务 1 的 build/CTest 命令，确认新布局断言先失败。
- [x] 实现按 Type members 递归展开；算法中的 append 将完整路径复制到 slot，不能保存临时引用：

```text
walk(type, root, path, direction, next_location):
    若为非空 structure：按 index 依次 push path、walk(member)、pop path
    否则若为允许的标量/向量：append(type, root, path, next_location++, interpolation)
    否则：记录 InvalidInterfaceType，消息含 root 和 path
```

- [x] raster `argument()` 创建 StageInput，普通 Kernel/Callable 参数逻辑保持原样。
  `return_()` 保留声明的 raster 返回类型；validator 遍历 ReturnStmt 检查每个表达式的类型或 void。
- [x] 注册测试结构体并核对具体结果：

```cpp
namespace raster_layout_test {
struct Inner { horizon::math::float2 uv; unsigned int id; };
struct Outer { horizon::math::float3 color; Inner inner; };
}
OC_MAKE_STRUCT_DESC(raster_layout_test::Inner, uv, id)
OC_MAKE_STRUCT_DESC(raster_layout_test::Outer, color, inner)
```

  宏来自 `ast/type_desc.h` 引入的 `core/type_system/type_desc.h`，在全局作用域展开，
  定义自身包含结束分号；不在 namespace raster_layout_test 内展开显式特化。
  Outer 的期望叶子为 `(location=0,path=[0])`、`(1,[1,0])`、`(2,[1,1])`，
  input 插值依次为 Smooth/Smooth/Flat；作为 FS 返回值时插值均为 None。
- [x] 测试 bool、half、array、matrix、Buffer、空 structure 拒绝；使用异常 diagnostics 中的
  InvalidInterfaceType 断言。测试 float 返回声明配上 int ReturnStmt 产生 InvalidReturn。
- [x] 检验不同 StoragePrecisionPolicy 下 real 解析：float32 接受、half 拒绝；用 RAII 恢复全局
  精度策略，不污染后续测试。real 正例显式设置 `allow_real_in_storage = true`。
  普通 float3/float4 不应因 storage policy 意外改变接口含义。
- [x] 将 stage 和 metadata 逐字段纳入 Function hash。测试重复构造相同输入稳定、输入维度或
  整数类型变化改变 hash；布局确定后 reset_hash，禁止在 getter 中修改布局。
- [x] 运行 AST 全部测试。任务 3 开启位置检查后，凡新增 VS 正例都要补上 position 写入。

## 任务 3：builtin、DiscardStmt 与可达调用图校验

**Files:** 修改 Function、Variable tags、symbol_name、Statement、Corrector；
实现 raster_validation.cpp；扩展 AST 测试并创建独立进程拒绝脚本。

**Interfaces:** 六个 `Function` builtin 工厂返回 `const RefExpr *`；
`Function::discard()`；`StmtVisitor::visit(const DiscardStmt *)`；
`validate_raster_function(const Function &) -> vector<RasterDiagnostic>`。

- [x] 写 builtin 去重与 discard 正例：

```cpp
auto fs = horizon::ast::Function::define_raster(
    horizon::ast::Function::Tag::Fragment, nullptr, [] {
        auto *f = horizon::ast::Function::current();
        expect(f->fragment_coord() == f->fragment_coord(), "builtin reused in one function");
        f->discard();
    });
expect(fs->builtin_vars().size() == 1, "only one fragment coordinate builtin");
expect(fs->body()->statements().back()->tag() == horizon::ast::Statement::Tag::Discard,
       "discard is a dedicated statement");
expect(fs->body()->check_context(fs.get()), "discard keeps its function context");
```

- [x] 运行 AST target，确认新工厂/语句尚未实现导致的失败；然后新增六个 tag、名称与
  `_builtin(tag, Type::of<T>())` 包装，类型严格遵循设计第 6 节。
- [x] 新建 DiscardStmt，无表达式参数。实现虚 Visitor、Context 和含 Discard tag 的 hash；
  更新所有范围内的 StmtVisitor 实现。Corrector 对它的 visit 不做数据映射。
- [x] 实现 validator：直接语句权限检查 + 入口可达 CallExpr 的 DFS。使用 visiting/visited
  区分递归和重复访问；递归环预检在 Corrector 前执行，权限检查在校正后执行。
  阶段检查键包含当前入口阶段，引用权限分析另按调用点保留映射，不能被单纯按 Function 的
  visited 集跳过。引用参数写权限必须随调用点实际参数传播：

```text
访问赋值左值 -> 去除 member/subscript/swizzle -> 获得根变量
若根是调用者映射而来的形式引用 -> 沿本调用点映射追到实际根
若实际根为 StageInput 或只读 builtin -> ReadOnlyWrite
复制成局部变量后根为 Local -> 允许
```

- [x] 增加 VS position 正例：使用 `f->vertex_position()`、一个 float4 literal 和 `assign()`。
  缺失、只读取 position 产生 MissingVertexPosition；经 Callable 写出允许。
  本期验证 AST 中有可达写入，不把它描述为已证明所有执行路径都初始化。
- [x] 增加带 discard 的独立 Callable，通过 `f->call(nullptr, callable, {})` 和 expr_statement
  分别用于 FS/VS。FS 成功；VS 的 diagnostics 含 InvalidBuiltinStage；再次用于 FS 仍成功。
  增加两层调用链、重复调用点和递归环诊断用例。
- [x] 使用低层 `assign()` 直接构造 AST，验证 FragmentCoord/FrontFacing/Index 写入拒绝，
  确保只读检查不依赖 DSL 包装类型或 C++ const 限制；
  测试 StageInput 的根、成员和 swizzle，以及传给写入引用参数的情况。
- [x] 用 `add_captured_resource()` 和局部模拟 handle 构造非空捕获记录，验证
  PhysicalResourceCapture；测试后 current 恢复。无需创建真实 GPU 资源。另验证 raster
  使用 ThreadIdx、SynchronizeBlock 或 TraceClosest 时产生 InvalidBuiltinStage。
- [x] 原 Kernel 构造保持 noexcept 时，在 AST 测试 main 增加 `--kernel-discard` 分支，
  构造调用 discard Callable 的 Kernel。应输出稳定诊断标识 InvalidBuiltinStage 并失败。
  CMake 脚本用独立进程检查，不依赖 CTest WILL_FAIL 对异常终止的处理：

```cmake
execute_process(COMMAND "${PROGRAM}" --kernel-discard
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if("${result}" STREQUAL "0")
    message(FATAL_ERROR "Invalid kernel was accepted")
endif()
if(NOT "${output}${error}" MATCHES "InvalidBuiltinStage")
    message(FATAL_ERROR "Expected raster stage diagnostic was not observed")
endif()
```

  在 tests/ast/CMakeLists.txt 接入：

```cmake
add_test(NAME horizon.ast.raster.kernel_rejection
    COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:horizon-test-ast-raster>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_raster_rejection.cmake)
```

- [x] 验证原 compute 的 block/thread builtin 和 RTX 原有逻辑不受新入口规则影响，新增可达
  raster 操作的拒绝不能变成对所有 Kernel 的通用 raster 验证。
- [x] 重新 Configure 后构建 horizon-tests，运行 AST 和 DSL 原有测试。

## 任务 4：VS/FS DSL 构造器与 builtin API

**Files:** 新建 raster_builtin.h、raster.h、tests/dsl/test_raster.cpp；
修改 dsl.h、tests/dsl/CMakeLists.txt；必要时最小调整 func.h traits。

**Interfaces:** 设计第 3 节的 VertexShader/FragmentShader、signature/return_type、const Function
句柄，和第 6 节的 DSL builtin。无需在此任务实现 RasterShader。
builtin 的 HLSL 映射、Helicon 现状和本期范围见上文对照表；不把列出的未覆盖项加入此任务。

- [x] 新测试文件只直接 include `dsl/api/raster.h`，确认它可以独立包含；现有 test_dsl.cpp 继续
  include dsl.h。先用下例产生编译失败：

```cpp
using namespace horizon::dsl;
using horizon::math::float3;
using horizon::math::float4;
VertexShader vs{[](Var<float3> pos, Var<float3> color) {
    vertex_position() = make_float4(pos, 1.0f);
    return color;
}};
FragmentShader fs{[](Var<float3> color) { return make_float4(color, 1.0f); }};
static_assert(std::is_same_v<typename decltype(vs)::signature, float3(float3, float3)>);
static_assert(std::is_same_v<typename decltype(fs)::signature, float4(float3)>);
expect(vs.function()->is_vertex() && fs.function()->is_fragment(), "stage inference");
```

- [x] 用与 AST target 相同方式接入 `horizon-test-dsl-raster`，链接 horizon-dsl，
  CTest 名为 `horizon.dsl.raster`，加入 horizon-tests。
- [x] 用非 noexcept stage 构造器复用 `dsl_function_t` 与 `detail::create<Args...>`。
  Args 的创建使用当前 raster Function 的 argument 工厂；非 void 分支调用 return_，
  void 分支只执行 lambda。显式签名核对 C++ lambda 实际返回的 Var 值类型，不能悄悄强制转换。
- [x] 六个 builtin getter 均直接包装当前 Function 的 RefExpr 并返回对应 `Var<T>`：
  position 与 fragment_coord 返回 Float4，三个 index 返回 Uint，front_facing 返回 Bool。
  例如使用 `return Float4{Function::current()->vertex_position()};` 与
  `return Float4{Function::current()->fragment_coord()};`，由 AST tag 区分写权限。
  不把只读 builtin 先 eval 成可写 Local 后再返回，不公开返回内部 Expr 或 Ref。
- [x] 用类型断言固定公开 builtin 签名，防止显式或 auto 返回类型泄露内部表达式包装：

```cpp
static_assert(std::is_same_v<decltype(vertex_position()), Float4>);
static_assert(std::is_same_v<decltype(vertex_index()), Uint>);
static_assert(std::is_same_v<decltype(instance_index()), Uint>);
static_assert(std::is_same_v<decltype(draw_index()), Uint>);
static_assert(std::is_same_v<decltype(fragment_coord()), Float4>);
static_assert(std::is_same_v<decltype(front_facing()), Bool>);
```

- [x] 添加 DSL 权限正反例：对只读 builtin 的直接赋值、set、成员/swizzle 写入和经 Callable
  引用参数写入均诊断 ReadOnlyWrite。`Float4 coord = fragment_coord()` 直接引用 builtin；
  `auto` 接收或移动包装对象也不产生可写副本。从已有左值执行 `Float4 local = coord`
  才会创建 Local，修改 local 成功且不会写回 builtin。断言具体 AST 根变量和诊断码，
  不把这些失败用例写成 C++ 编译期拒绝；将设计第 6 节的局部拷贝示例纳入测试。
- [x] 增加完整设计示例（OC_STRUCT 注册）；测试多个 VS 属性、嵌套 varying、FS MRT、
  `VertexShader<void()>`、`FragmentShader<void()>`、无属性但有 varying 的 VS。
- [x] 加入以下细节用例：显式签名的泛型 lambda、构造计数为一次、用户 lambda 抛异常后
  下次成功、错误阶段的内建调用、无 current 时 logic_error、input 拷贝后修改成功。
  裸指针/mutable reference 参数用 requires/traits 负断言验证；不能只在文档中写“不支持”。
- [x] 运行以下命令，期望新增及旧 DSL/AST 测试全部通过：

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build horizon-tests --configuration Debug --target-family core
conda run -n horizon-dev --no-capture-output ctest --test-dir build/conan/core/debug -C Debug -R '^horizon\.(ast|dsl)(\.|$)' --output-on-failure
```

## 任务 5：RasterShader 配对、生命周期与只读性

**Files:** 修改 raster.h、raster_validation.cpp、tests/dsl/test_raster.cpp、
tests/ast/test_raster.cpp。

**Interfaces:** `is_raster_pair_compatible_v<VS, FS>`、`RasterShader{vs, fs}`、
`vertex()/fragment()`、`validate_raster_pair(const Function &, const Function &)`。

- [x] 添加编译期匹配矩阵：

```cpp
using VS = horizon::dsl::VertexShader<horizon::math::float3(horizon::math::float3)>;
using FS = horizon::dsl::FragmentShader<horizon::math::float4(horizon::math::float3)>;
using WrongFS = horizon::dsl::FragmentShader<horizon::math::float4(horizon::math::float2)>;
static_assert(horizon::dsl::is_raster_pair_compatible_v<VS, FS>);
static_assert(!horizon::dsl::is_raster_pair_compatible_v<VS, WrongFS>);
static_assert(!horizon::dsl::is_raster_pair_compatible_v<FS, VS>);
static_assert(horizon::dsl::is_raster_pair_compatible_v<
    horizon::dsl::VertexShader<void()>, horizon::dsl::FragmentShader<void()>>);
```

- [x] 运行 DSL target 确认新 traits/组合缺失导致的失败。定义只接受正确阶段对的 traits：
  `void -> tuple<>` 或 `T -> tuple<T>` 为真，其余为假；C++ 外层移除 cvref。
  参数引用限制仍由阶段类自己的 signature 约束承担。
- [x] 实现受 traits 约束的 RasterShader 构造器与 deduction guide，按值保存两个 stage wrapper，
  构造时调用 validate_raster_pair；不再次调用 shader lambda，不调用编译器。
- [x] AST 配对先确认 Vertex/Fragment，再比较 outputs/inputs 的叶子数、路径、location、
  实际 type 和 interpolation；VS output root_index 恒 0，FS input root_index 必须为 0。
  同 C++ 类型不同解析精度也必须拒绝；非法精度先被阶段自身拒绝时也算正确拒绝，
  另用合法但实际类型不同的低层 AST 对测试 InterfaceMismatch 路径。
- [x] 增加不同结构体类型但相同成员布局的 C++ 负例、错误 FS 参数数目、FS void 返回值正例。
  对低层 AST 不匹配检查 diagnostic code 与路径信息。
- [x] 测试临时 stage/lambda 销毁后组合仍持有有效 Function；一个 FS 与两个兼容 VS 组合；
  组合前后接口快照、hash、builtin 数量不变。
- [x] 测试可读取且不能通过 function() 正常修改阶段：

```cpp
const auto f = fs.function();
static_assert(std::is_const_v<std::remove_reference_t<decltype(*f)>>);
const auto before = f->hash();
horizon::dsl::RasterShader first{vs, fs};
horizon::dsl::RasterShader second{vs, fs};
expect(first.fragment().function().get() == second.fragment().function().get(),
       "combinations share immutable stage ownership");
expect(f->hash() == before, "pair validation preserves finalized hash");
```

- [x] 运行全部 AST/DSL 测试，确认共享 Callable 的阶段校验没有被组合或 hash 缓存绕过。

## 任务 6：文档、边界和最终验收

**Files:** 更新 docs/architecture/overview.md，新增 src/dsl/README.md；必要的测试修正仅限上表。

- [x] 将设计第 3 节的完整示例纳入 DSL 测试函数并运行，保证最终文档例子确实经过 C++ 编译。
  新 README 写清所有首轮限制、默认 location/插值、错误处理、builtin 引用与局部副本的区别，
  以及“未接入编译/GPU”状态。用户用法使用 Var 或其别名，不要求使用 detail 中的类型。
- [x] 更新架构概览的“当前实现”与入口链接。设计文档保留历史基线，在顶部记录实际完成状态、
  验证命令和代码提交信息；未完成任务不能勾选。
- [x] 核对没有第二套 AST、真实资源地址、新第三方依赖、Slang/Vulkan 头或 Helicon target 依赖。
  执行现有 `horizon.architecture.dsl_runtime_boundary`，并人工检查新增文件 include 链。
- [x] 全量 core Debug 验证：

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build horizon-tests --configuration Debug --target-family core
conda run -n horizon-dev --no-capture-output ctest --test-dir build/conan/core/debug -C Debug --output-on-failure
```

- [x] 验证拒绝逻辑不依赖 Debug assert，在独立 Release 目录构建测试并执行 AST/DSL/边界检查：

```powershell
conda run -n horizon-dev --no-capture-output python tools/dev.py build horizon-tests --configuration Release --target-family core
conda run -n horizon-dev --no-capture-output ctest --test-dir build/conan/core/release -C Release -R '^horizon\.(ast|dsl|architecture)(\.|$)' --output-on-failure
git diff --check
git status --short
git diff --stat
```

- [x] 仅对改动区域检查仓库格式规范。若 clang-format 版本无法解析配置，记录原因并按等价格式
  检查，不做全文件格式化。若要提交实现，先确认新机器用户已授权；不要沿用本轮文档 push
  授权去推送未来代码。
- [x] 最终报告列出实际通过的 target/CTest、未验证项目和首个阻断错误。明确：没有本期 GPU
  验证，不能把 host AST 测试通过写成“光栅化已经可运行”。

## 接手提示词

以下保留原交接模板；继续开发前先核对本文顶部状态与实际工作区，勿重复已完成任务：

> 请读取根 AGENTS.md、docs/superpowers/specs/2026-09-19-raster-shader-interface-design.md
> 和 docs/superpowers/plans/2026-09-19-raster-shader-interface.md，按计划顺序实现新 AST/DSL
> 的 VS/FS 对外接口，并完成对应测试和文档。使用当前机器的环境与仓库实际状态核对基线；
> 不查阅或修改 modules/ocarina，不接入 GPU 后端。先报告基线差异，再从第一个未完成任务继续。
