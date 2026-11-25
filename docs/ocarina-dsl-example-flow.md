# Ocarina DSL 示例运行流程：从 AST 构建到 Slang 生成

本文以 [`examples/example_ocarina_dsl`](https://github.com/CoronaEngine/Horizon/tree/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/example_ocarina_dsl) 为例，梳理
Ocarina DSL 从 C++ lambda 到 Slang 源码、再到 Vulkan Compute 的完整内部链路。
重点在 **AST 构建**和 **Slang 源码生成**；Vulkan 调度、渲染代码和 shader 效果只做
简略说明。

> 本文记录了上游提交 `4177351f` 的示例和运行结果，示例链接指向该版本。当前分支的渲染示例入口见 [examples/main.cpp](../examples/main.cpp)。

## 0. 总览

示例的运行入口仍然属于现有的 `HorizonExamples` 示例体系：

```text
HorizonExamples ocarina_dsl
    │
    ├─ examples/main.cpp
    │     └─ run_example_ocarina_dsl(argc, argv)
    │
    └─ examples/example_ocarina_dsl/example_ocarina_dsl.cpp
          ├─ 1. RHIContext / VulkanComputeDevice
          ├─ 2. make_raymarch_kernel()
          │       └─ Kernel 构造 → Function::define_kernel()
          │             ├─ lambda 在 host 执行一次
          │             ├─ 运算符 / 内建函数 / $if / $for 构造 AST
          │             └─ Function::correct() → FunctionCorrector
          ├─ 3. AstToSlangSource::generate()
          │       └─ AST → Slang body
          ├─ 4. compose_slang_source()
          │       └─ 加 prelude / include，保存 .slang
          ├─ 5. device.compile()
          │       └─ AST → Slang → Slang Compiler → SPIR-V → compute pipeline
          └─ 6. dispatch + download + Image::save_image()
```

对应的关键代码入口：

| 阶段 | 入口 | 位置 |
| --- | --- | --- |
| 示例分发 | `mode == "ocarina_dsl"` | [`examples/main.cpp:71`](https://github.com/CoronaEngine/Horizon/blob/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/main.cpp#L71) |
| 示例函数 | `run_example_ocarina_dsl()` | [`example_ocarina_dsl.cpp:340`](https://github.com/CoronaEngine/Horizon/blob/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/example_ocarina_dsl/example_ocarina_dsl.cpp#L340) |
| 渲染主流程 | `render_example()` | [`example_ocarina_dsl.cpp:267`](https://github.com/CoronaEngine/Horizon/blob/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/example_ocarina_dsl/example_ocarina_dsl.cpp#L267) |
| DSL kernel | `make_raymarch_kernel()` | [`example_ocarina_dsl.cpp:134`](https://github.com/CoronaEngine/Horizon/blob/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/example_ocarina_dsl/example_ocarina_dsl.cpp#L134) |
| Slang 生成 | `AstToSlangSource::generate()` | [`ast_to_slang_source.cpp:221`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L221) |
| Slang 编译 | `SlangShaderCompiler::compile_program()` | [`slang_compiler.cpp:434`](../src/backends/vulkan_compute/slang_compiler.cpp#L434) |
| Vulkan 调度 | `VulkanComputeDevice::create_shader()` | [`vk_compute_device.cpp:921`](../src/backends/vulkan_compute/vk_compute_device.cpp#L921) |

## 1. 运行入口和准备

`run_example_ocarina_dsl()` 先解析 `--output`、`--size`、`--print-slang` 等参数，
然后进入 `render_example()`。真正有意思的部分从创建 kernel 开始，但此前还有两件
需要先建立的状态：

1. **RHI 上下文和设备**：`RHIContext::instance().init(runtime_dir, ".cache")` 把
   运行目录加入动态模块搜索路径；`create_device()` 加载
   `ocarina-backend-vulkan-compute` 并创建 `VulkanComputeDevice`。
2. **kernel 的 workgroup 和描述**：
   ```cpp
   auto kernel = make_raymarch_kernel();
   constexpr uint3 block_dim = make_uint3(8u, 8u, 1u);
   kernel.function()->set_block_dim(block_dim);
   kernel.set_description("ocarina_dsl_raymarch");
   ```
   `set_block_dim()` 只写入 `Function::block_dim_`，后面会同时影响
   `OC_WORKGROUP_*` prelude 和 `numthreads` 属性；`set_description()` 写入
   `Function::description_`，主要供诊断和“未显式传 shader name”的默认编译路径
   使用；本示例的 `device.compile(kernel, shader_name)` 使用显式传入的
   `shader_name` 作为编译描述。

此时 `Function` 还不是 Slang，也不是 SPIR-V，只是一棵由 C++ 对象表达的 AST。

## 2. DSL 构建：lambda 如何变成 AST

### 2.1 三层包装：`Var<T>`、`Ref<T>`、`Expression`

Ocarina 的 DSL 值类型不是“运行时值”，而是指向 AST 节点的轻量包装：

```text
Var<T> / Expr<T> / Swizzle<...>
        │
        ▼
Ref<T>（持有 const Expression*）
        │
        ▼
AST 节点：RefExpr / BinaryExpr / CallExpr / ...
```

- [`detail::Ref<T>`](../src/dsl/core/ref.h#L12) 的核心成员只有一个
  `const Expression *expression_`。它不拥有节点，只负责在表达式上做运算符重载。
- [`Var<T>`](../src/dsl/core/var.h#L25) 继承 `Ref<T>`，是用户最常写的类型：
  `Float3`、`Uint2`、`Texture2DVar` 等别名最终都是 `Var<...>`。
- [`detail::extract_expression()`](../src/dsl/core/expr.h#L18) 是 C++ 值进入 AST 的
  入口：
  - 如果参数本身是 DSL 值，直接取出它的 `expression()`；
  - 如果是普通 host 值（`float`、`uint`、`make_float3` 的标量参数等），调用
    `Function::literal(Type::of<T>(), value)` 创建 `LiteralExpr`。
- [`eval<T>()`](../src/dsl/api/stmt_builder.h#L37) 把表达式包装成 `Var<T>`，
  这是很多运算符和内建函数返回值的出口。

AST 的另一半是类型和变量：

- [`Type`](../src/core/type.h#L721) 是全局 intern 的类型对象，保存
  `Tag`、`size`、`alignment`、`dimension`、`members` 等信息。
  [`Type::of<T>()`](../src/core/type_system/type_desc.h#L259) 从
  `TypeDesc<T>::description()` 查询/注册类型，因此同一个 C++ 类型在 AST 中只有
  一个 `Type*` 身份。
- [`Variable`](../src/ast/variable.h#L28) 表示“存储身份”，持有 `Type*`、
  `Variable::Tag`（`LOCAL`、`TEXTURE2D`、`DISPATCH_IDX` 等）、uid 和
  `Usage`。[`Function::create_variable()`](../src/ast/function.cpp#L235) 分配
  自增 uid；`Function::resolve_ast_type()` 会按 `StoragePrecisionPolicy` 解析
  `real` 等精度类型（[`function.cpp:32`](../src/ast/function.cpp#L32)）。

`Type` 决定节点的静态类型和后续 Slang 类型名；`Variable` 决定这个值是否来自参数、
局部变量、纹理还是 builtin。两者合起来才是 `RefExpr` 的完整语义。

`Var<T>` 的三个关键构造：

| 构造 | 语义 | 位置 |
| --- | --- | --- |
| `Var(ArgumentCreation)` | 创建 kernel/callable 形参：调用 `Function::argument(Type::of<T>())`，把 `Variable` 放入 `arguments_`，返回 `RefExpr` | [`var.h:58`](../src/dsl/core/var.h#L58) |
| `Var(Arg&&)` | 创建局部变量：调用 `Function::local(Type::of<T>())` 得到 `RefExpr`，再对传入表达式生成 `AssignStmt` | [`var.h:56`](../src/dsl/core/var.h#L56) |
| `Var()` 默认构造 | 只创建局部变量并登记到当前 `ScopeStmt::local_vars_`，不产生赋值语句 | [`var.h:34`](../src/dsl/core/var.h#L34) |

因此，`Var<T>` 的表达式构造会向当前作用域注册一个局部变量并生成一条
`AssignStmt`；它的默认构造只注册局部变量，供后续显式赋值使用。这一点直接决定了
后面生成的 Slang 会呈现为大量 `vNNN = ...` 的三地址形式。

### 2.2 Kernel 的构造：`Kernel` → `Function::define_kernel`

示例中的 kernel 是这样声明的：

```cpp
auto make_raymarch_kernel() {
    return Kernel{[](Texture2DVar output) {
        Uint2 pixel = dispatch_idx().xy();
        ...
        output.write(make_float4(color, 1.f), pixel.x, pixel.y);
    }};
}
```

类型推导和处理链如下：

1. lambda 的类型通过
   [`Kernel` 的 CTAD 指南](../src/dsl/api/func.h#L347) 进入
   `Kernel<detail::dsl_function_t<...>>`。
2. [`detail::dsl_function`](../src/dsl/api/func.h#L157) 把 `Texture2DVar`
   （即 `Var<Texture2D>`）转换成 prototype 类型 `Texture2D`，所以最终
   `Kernel` 的类型是 `Kernel<void(Texture2D)>`。
3. [`Kernel<void(Args...)>` 构造函数](../src/dsl/api/func.h#L331) 调用
   [`Function::define_kernel()`](../src/ast/function.h#L266)。
4. `define_kernel()` 内部的 [`Function::_define()`](../src/ast/function.h#L127)：
   - `make_shared<Function>(Tag::KERNEL)` 创建函数对象；
   - `push(function)` 压入 `Function::_function_stack()`，使
     `Function::current()` 指向它；
   - `ret->with(ret->body(), lambda)` 进入函数体 `ScopeStmt`，然后执行 lambda；
   - `pop(function)` 恢复函数栈；
   - 返回创建好的 `Function`。
   `_define()` 返回后，`define_kernel()` 再调用 `function->correct()`。
5. lambda 的形参由
   [`create_argument_definition()`](../src/dsl/api/func.h#L113) 构造：
   `detail::create<Args...>()` 为每个 `prototype_to_var_t<Arg>` 创建 `Var`；
   `Var<Texture2D>{ArgumentCreation}` 最终调用
   [`Function::argument()`](../src/ast/function.cpp#L334)，根据
   `Type::Tag::TEXTURE2D` 把 `Variable::Tag` 设为 `TEXTURE2D`，追加到
   `arguments_`，并返回 `RefExpr`。
6. lambda 的函数体在 `Function::with(body, lambda)` 的作用域中运行；
   `$if`、`$for` 内部又会临时 push 新的 `ScopeStmt`，所以局部变量会归入正确的
   scope。

这里的关键是：**lambda 只在构造 Kernel 时执行一次**。此后 `device.compile()`
只是读取已经建好的 AST，不会重新执行 C++ lambda。

### 2.3 表达式如何落成节点

示例中的普通 C++ 运算符会落到 `Function` 的节点工厂：

| C++ 写法 | 产生的 AST | 代码位置 |
| --- | --- | --- |
| `a + b`、`a * b`、`a < b` | `BinaryExpr` | [`operators.h:51`](../src/dsl/api/operators.h#L51) → [`Function::binary()`](../src/ast/function.cpp#L397) |
| `-x`、`!x` | `UnaryExpr` | [`operators.h:21`](../src/dsl/api/operators.h#L21) → [`Function::unary()`](../src/ast/function.cpp#L392) |
| `0.001f`、`96u` 等 host 字面量 | `LiteralExpr` | [`extract_expression()`](../src/dsl/core/expr.h#L18) → [`Function::literal()`](../src/ast/function.cpp#L388) |
| `cast<float>(x)` | `CastExpr` | `CastExpr` 构造见 [`expression.h:202`](../src/ast/expression.h#L202) |
| `dispatch_idx()` | `RefExpr`，指向 `Variable::Tag::DISPATCH_IDX` | [`Function::dispatch_idx()`](../src/ast/function.cpp#L330)、[`_builtin()`](../src/ast/function.cpp#L291) |
| `pixel.x`、`pixel.xy` | `MemberExpr`（成员或 swizzle） | [`Function::swizzle/member()`](../src/ast/function.cpp#L422) |
| `make_float3(x, y, z)` | `CallExpr(CallOp::MAKE_FLOAT3)` | [`builtin.h:365`](../src/dsl/api/builtin.h#L365) → [`Function::call_builtin()`](../src/ast/function.cpp#L445) |
| `normalize(v)`、`length(v)`、`min(a,b)` | `CallExpr(对应 CallOp)` | [`builtin.h:133`](../src/dsl/api/builtin.h#L133) |
| `output.write(value, x, y)` | `CallExpr(CallOp::TEX2D_WRITE)` | [`ref_func.h:356`](../src/dsl/core/ref_func.h#L356) |

注意 2.1 节所说的“局部变量物化”。以：

```cpp
Float sphere_distance = length(position) - 1.f;
```

为例，host 侧大致发生：

```text
1. 局部变量 position 已经是 RefExpr。
2. length(position)：
     Function::call_builtin(LENGTH, {position}) → CallExpr
     eval<float>(CallExpr) → Var<float>
3. Var<float> 构造：
     Function::local(float) → RefExpr + 当前 ScopeStmt::local_vars_
     assign(local, CallExpr) → AssignStmt
4. `- 1.f`：
     Function::literal(float, 1.0f) → LiteralExpr
     Function::binary(SUB, local, literal) → BinaryExpr
     eval<float>(BinaryExpr) → 又一个 Var<float> 局部变量 + AssignStmt
```

最终形成的不是一棵纯粹嵌套的表达式树，而是“表达式树 + 局部变量 + 赋值语句”的
混合结构。生成 Slang 时，`_emit_local_var_define()` 先为 scope 里的局部变量生成
默认定义，再按语句顺序生成赋值；这也解释了生成源码里为什么有大量
`float vN = {};` 和 `vN = ...`。

### 2.4 语句、作用域和控制流

[`Function::create_statement()`](../src/ast/function.h#L169) 做两件事：

1. 用 `make_unique<Stmt>` 创建节点，并设置 `context = this`；
2. 把节点加入 `all_statements_`，并调用
   `current_scope()->add_stmt(ret)` 挂到当前 `ScopeStmt`。

当前作用域由 [`Function::current_scope()`](../src/ast/function.cpp#L279) 决定：
函数体自身是 `body_`，进入 `$if`/`$for` 时通过
[`Function::with()`](../src/ast/function.h#L261) 临时 push 对应的 `ScopeStmt`。

#### `$if`

宏定义在 [`syntax.h:13`](../src/dsl/api/syntax.h#L13)。展开后大致是：

```cpp
IfStmtBuilder::create_with_source_location(...) / [&] { ... }
```

- [`IfStmtBuilder::create()`](../src/dsl/api/stmt_builder.h#L130) 调用
  `Function::if_(extract_expression(condition))`，生成 `IfStmt`；条件表达式
  如果是 host `bool` 会变成 `LiteralExpr`。
- `/ [&]` 在 [`IfStmtBuilder::operator/`](../src/dsl/api/stmt_builder.h#L142)
  中执行 `Function::with(if_->true_branch(), body)`。
- `$else` 展开为 `% [&]`，走
  [`IfStmtBuilder::operator%`](../src/dsl/api/stmt_builder.h#L153) 进入 false 分支。
- `IfStmt` 内部有 `condition_`、`true_branch_`、`false_branch_` 两个
  `ScopeStmt`（[`statement.h:172`](../src/ast/statement.h#L172)）。

#### `$for`

宏定义在 [`syntax.h:47`](../src/dsl/api/syntax.h#L47)，展开为
`range_with_source_location(...)`。示例：

```cpp
$for(step, 96u) {
    ...
    $break;
};
```

处理链：

1. [`range_with_source_location()`](../src/dsl/api/stmt_builder.h#L486) 调用
   `range(96)`，进入
   [`ForStmtBuilder`](../src/dsl/api/stmt_builder.h#L420)。
2. `ForStmtBuilder` 创建循环变量 `Var<T>`、上界 `end_`、步长 `step_`；
   它生成比较表达式和“负步长时反向比较”的 `ConditionalExpr`，最后调用
   [`Function::for_()`](../src/ast/function.cpp#L558) 生成 `ForStmt`。
3. `set_var_symbol("step")` 把循环变量命名为 `step`。
4. `/ [&]` 在 `ForStmt::body_` 作用域中执行 body；`step` 作为参数传入 lambda。
5. `$break` 展开为 `break_("break: ...")`（[`syntax.h:28`](../src/dsl/api/syntax.h#L28)），
   最终调用 [`Function::break_()`](../src/ast/function.cpp#L566)，在当前作用域生成
   `BreakStmt`。

`ForStmt` 的节点字段是 `var_`、`condition_`、`step_`、`body_`
（[`statement.h:256`](../src/ast/statement.h#L256)）。

#### 纹理写入

`output.write(...)` 不走 `AssignStmt`。它由
[`EnableTextureReadAndWrite::write()`](../src/dsl/core/ref_func.h#L356) 完成：

```cpp
const CallExpr *expr = Function::current()->call_builtin(
    nullptr, CallOp::TEX2D_WRITE,
    {self()->expression(), OC_EXPR(elm), OC_EXPR(x), OC_EXPR(y)});
self()->expression()->mark(Usage::WRITE);
Function::current()->expr_statement(expr);
```

所以 AST 中是：

```text
ExprStmt
└─ CallExpr(TEX2D_WRITE)
   ├─ RefExpr(output)        // Variable::Tag == TEXTURE2D
   ├─ make_float4(...)       // 颜色值
   ├─ MemberExpr(pixel.x)    // x
   └─ MemberExpr(pixel.y)    // y
```

纹理参数本身是 kernel 的显式形参，`Variable::Tag::TEXTURE2D` 会让后面的参数 ABI
把它当作资源描述符处理。

### 2.5 节点所有权和作用域树

[`Function`](../src/ast/function.h#L52) 是 AST 的所有者：

```text
Function
├─ all_expressions_  : vector<unique_ptr<Expression>>
├─ all_statements_   : vector<unique_ptr<Statement>>
├─ arguments_        : vector<Variable>
├─ builtin_vars_     : vector<Variable>
├─ shared_vars_      : vector<Variable>
├─ body_             : ScopeStmt
└─ ...
```

- `Expression` 节点通过
  [`Function::create_expression()`](../src/ast/function.h#L159) 创建，
  设置 `context = this` 后放入 `all_expressions_`。
- `Statement` 节点通过
  [`Function::create_statement()`](../src/ast/function.h#L169) 创建，
  设置 `context = this` 后放入 `all_statements_`，并挂到当前 scope。
- `Var<T>`/`Ref<T>` 只持有裸指针，不参与生命周期管理；`Kernel` 和
  `Shader<T>` 持有 `shared_ptr<Function>`，从而保证 AST 在编译期内有效。
- [`ASTNode::check_context()`](../src/ast/ast_node.h#L24) 用 `context_` 检查节点是否
  属于当前函数。`FunctionCorrector` 用它检查表达式/语句是否被错误地跨函数引用。

本示例只构造了一个 `Function`，没有 `Callable`/`Lambda`，也没有捕获资源，因此
作用域树就是：

```text
Function(KERNEL)
└─ ScopeStmt(body)
   ├─ local: Uint2 pixel
   ├─ local: Uint2 size
   ├─ local: Float2 uv
   ├─ ...
   ├─ ForStmt(step, cond, step)
   │  └─ ScopeStmt(body)
   │     ├─ local: Float3 position
   │     ├─ ...
   │     ├─ IfStmt(distance < 0.001f)
   │     │  ├─ ScopeStmt(true)
   │     │  │  ├─ IfStmt(sphere_distance < plane_distance)
   │     │  │  │  ├─ ScopeStmt(true)  // sphere normal/albedo
   │     │  │  │  └─ ScopeStmt(false) // floor checker
   │     │  │  ├─ AssignStmt(hit, true)
   │     │  │  └─ BreakStmt
   │     │  └─ ScopeStmt(false) // empty
   │     └─ AssignStmt(t, t + distance)
   ├─ IfStmt(hit)
   │  ├─ ScopeStmt(true)  // shading
   │  └─ ScopeStmt(false) // sky
   └─ ExprStmt(CallExpr TEX2D_WRITE)
```

实际节点数量比上图多，因为每个中间表达式会物化成局部变量和 `AssignStmt`。但
“函数体 → scope → statement/expression”的结构不变。

## 3. AST 校正：`Function::correct()`

lambda 执行完后，`Function::define_kernel()` 立即调用
[`Function::correct()`](../src/ast/function.cpp#L48)：

```cpp
void Function::correct() noexcept {
    FunctionCorrector().apply(this);
}
```

[`FunctionCorrector`](../src/ast/function_corrector.h#L11) 的工作可以分成三层：

### 3.1 遍历函数体

[`apply()`](../src/ast/function_corrector.cpp#L47) 先把 kernel 压入内部函数栈，
然后调用 [`traverse()`](../src/ast/function_corrector.cpp#L11)：

```text
apply(kernel)
├─ push(kernel)
├─ traverse(kernel)
│  ├─ visit(kernel.body())
│  ├─ correct_used_structures()
│  └─ 检查 context
├─ 如果是 kernel：
│  ├─ stage = SplitParamStruct
│  ├─ splitting_arguments()
│  └─ traverse(kernel) 第二次
├─ check_context(kernel)
└─ pop()
```

第一次遍历负责按语句/表达式访问整棵最终语句树；第二次遍历发生在
`splitting_arguments()` 之后，用来处理参数结构拆分后的替换。

### 3.2 资源使用和捕获

- [`visit_expr()`](../src/ast/function_corrector.cpp#L83) 对 `RefExpr`/`MemberExpr`
  调用 `variable().mark_used()`，并进入
  [`process_capture()`](../src/ast/function_corrector.cpp#L69)。
- `process_capture()` 判断表达式属于当前函数还是调用者：
  - 当前函数：直接返回；
  - 来自调用者：`capture_from_invoker()` 把外层表达式映射成当前函数的隐藏参数；
  - 来自被调用者：`output_from_invoked()` 把输出引用一路传播到 kernel 的局部变量。
- 本示例是单 kernel、无 callable，因此 `process_capture()` 基本只走“当前函数”
  分支；捕获逻辑处于休眠状态。
- `visit(CallExpr)` 会递归访问实参；如果它调用的是自定义 `Function`，则进入被调用
  函数继续 `apply()`，最后用 `correct_usage()` 把实参的读写 usage 合并到形参。
  内建 `CallOp`（本示例的 `normalize`、`TEX2D_WRITE` 等）没有 `function_`，因此
  直接返回。

### 3.3 参数结构拆分和上下文校验

- [`Function::splitting_arguments()`](../src/ast/function.cpp#L179) 只对
  `Type::is_param_struct()` 的形参展开成员，把结构体参数拆成多个平坦参数。
  示例的 `Texture2D` 不是 param struct，所以保持不变。
- `traverse()` 会对 callable 检查 `Function::check_context()`；`apply()` 最后再对
  kernel 检查一次。节点 context 不一致会直接报错，避免把别的函数的 AST 节点挂到
  当前 kernel 上。
- `correct_used_structures()` 把参数、附加参数、捕获资源使用的结构类型收集起来，
  供 Slang 生成时先输出结构定义。

校正后的 AST 才是后端真正读取的 AST。生成器不会重新执行 C++ 运算符，也不会重新
推导局部变量，只按节点树访问。

### 3.4 函数身份和哈希

[`Function::compute_hash()`](../src/ast/function.cpp#L612) 把返回类型、headers、
存储策略、tag、参数、builtin、shared、captured resources 和函数体全部纳入哈希；
[`Function::func_name()`](../src/ast/function.cpp#L586) 基于这个哈希生成
`kernel__<hash>` 之类的名字。后续 Slang 缓存、shader 描述和日志都用这个稳定
身份来区分 kernel。

## 4. Slang 生成：从 AST 到源码

### 4.1 两个生成入口

示例里有两次生成：

1. **显式生成**：示例调用
   [`AstToSlangSource::generate()`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L221)
   拿到 kernel 的 Slang body，然后由
   [`compose_slang_source()`](https://github.com/CoronaEngine/Horizon/blob/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/example_ocarina_dsl/example_ocarina_dsl.cpp#L210)
   补 prelude、include，保存为 `.slang`。这是为了让读者看到生成结果。
2. **实际编译生成**：`device.compile()` 内部再次调用同一个
   `AstToSlangSource::generate()`，然后继续走 Slang 编译器。两次生成使用同一套
   visitor；对本示例而言结果一致，保存的 `.slang` 与后端实际编译的源码只差两行
   说明注释。

   生产代码通常只需要第 2 条；示例保留第 1 条是为了教学和调试。示例显式调用使用
   `obfuscation=false`、`supports_warp32=false`；本 kernel 不使用 warp 操作和自定义
   header，因此这两个参数不影响生成结果。

### 4.2 `AstToSlangSource::generate()` 的阶段

`generate()` 的骨架：

```cpp
EmittedSlangSource AstToSlangSource::generate(const Function &function,
                                              const ShaderCompilationContext &context,
                                              bool obfuscation,
                                              bool supports_warp32) {
    static std::mutex emission_mutex;
    std::lock_guard lock(emission_mutex);

    AstToSlangSource emitter{context, obfuscation, supports_warp32};
    auto requirements = analyze_function_requirements(function);
    ...
    emitter.reachable_functions_ = std::move(requirements.reachable_functions);
    emitter.emit(function);
    return {string(emitter.scratch().view()), function.func_name(),
            emitter.requires_half_rte_};
}
```

阶段说明：

1. **串行化**：AST/Type 的哈希是惰性缓存的，多个编译器实例可能共享同一函数图，
   因此用静态 mutex 串行化发射过程。
2. **需求分析**：[`analyze_function_requirements()`](../src/ast/function_requirements.cpp#L129)
   遍历最终语句树，而不是 `all_call_expr()`，因为 `all_expressions_` 里可能包含
   被丢弃的表达式，`all_call_expr()` 记录的是调用者而非被调用者。
   它收集：
   - `uses_trace`、`requires_compute_workgroups`、`uses_warp` 等能力需求；
   - `reachable_functions`：从 kernel 可达的函数集合；
   - `diagnostic_sources`：用于错误定位的描述。
   对普通 compute 路径，如果发现 trace 且执行模式是 `Compute`，直接报错。
3. **可达性过滤**：`emitter.reachable_functions_` 决定 `emit()` 是否输出某个
   callable。示例没有 callable，因此集合里只有 kernel。
4. **发射**：`emitter.emit(function)` 进入
   [`AstToSlangSource::emit()`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L239)，
   最终调用基类 `AstToCppSource::emit()`。
5. **返回**：`{body, function_name, requires_half_rte}`。
   `function_name` 来自 `Function::func_name()`，供缓存 key 使用；
   `requires_half_rte` 用于后续半精度 FMA 的校验。

### 4.3 Visitor 结构：`AstToSlangSource` 继承 `AstToCppSource`

```text
SourceEmitter
└─ AstToCppSource                 // 通用 C++ 风格 AST visitor
   └─ AstToSlangSource            // Slang 特化
        ├─ _emit_type_name()      // Ocarina Type → Slang 类型名
        ├─ visit(Unary/Binary/...) // 运算符、cast、literal 的 Slang 形式
        ├─ visit(CallExpr)        // CallOp → oc_* / Slang 内建
        ├─ _emit_kernel_params()  // push constant / BDA 参数 ABI
        ├─ _emit_builtin_vars_define()
        ├─ _emit_kernel_argument_loads()
        └─ _emit_function_body()
```

基类 [`AstToCppSource`](../src/generator/ast_to_cpp_source.h#L14) 提供：

- 语句 visitor：`ScopeStmt`、`IfStmt`、`ForStmt`、`AssignStmt`、`ReturnStmt` 等；
- 表达式 visitor：`RefExpr`、`LiteralExpr`、`BinaryExpr`、`MemberExpr`、
  `SubscriptExpr`、`CastExpr` 等；
- 作用域局部变量定义
  [`_emit_local_var_define()`](../src/generator/ast_to_cpp_source.cpp#L339)；
- 函数发射骨架 [`emit()`](../src/generator/ast_to_cpp_source.cpp#L468)：
  先输出 headers、结构定义、被引用的自定义函数，再输出当前函数。

`AstToSlangSource` 覆盖与 Vulkan/Slang 相关的部分；两者共用的 AST 遍历和语句结构
不需要复制。对示例来说，最重要的覆盖是：

- 类型名：`float`/`uint`/`float3`/`float2` 等直接映射到 Slang 名字，纹理映射为
  `OCTextureDesc`，buffer 映射为 `OCBuffer<T>`（[`_emit_type_name()`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L1459)）；
- kernel 入口：生成 `[shader("compute")]`、`numthreads` 和 `main`；
- 参数 ABI：`OCPushConstants`、`oc_param_*`、纹理描述符加载；
- 内建调用：`CallOp` 到 `oc_*` 的映射；
- 半精度和存储布局的特殊处理。

### 4.4 Kernel ABI：push constant 和纹理描述符

这是示例里最值得注意的 Slang 生成细节。`_emit_function_body()` 对 kernel 先调用
[`_emit_kernel_params()`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L1527)：

```text
OCPushConstants
├─ uint64_t params           // 参数 blob / BDA 地址
├─ uint dim_x/dim_y/dim_z    // 逻辑 dispatch 尺寸
├─ uint group_base_x/y/z     // 大 dispatch 分块时的 group 偏移
└─ uint words[N]             // 当参数能塞进 128B push constant 时
```

布局由 [`VkShaderParameterLayout`](../src/backends/vulkan_compute/vk_shader_parameters.h#L14)
统一决定：

- `params_offset = 0`，`dim_offset = 8`，`group_base_offset = 20`，
  `inline_offset = 32`，`max_push_bytes = 128`；
- `extent` 由 `Function::arguments()` + captured resources 按
  `VulkanComputeDevice::size/alignment` 累加；
- host ABI 顺序是 `Function::arguments()` → captured resources；
  `_emit_kernel_argument_loads()` 的注释也明确说明了这一点；
- 如果 `extent <= 96`，走 inline push constant，生成 `words[]` 和
  `oc_param_word/u64/bits`；
- 否则走 `params` 指向的 BDA，生成 `oc_param<T>(offset)`。

本示例只有一个 `Texture2D` 形参：

```text
TextureDesc 大小 24 字节
extent = 24
inline_offset = 32
inline_word_count = ceil(24/4) = 6
=> words[6]
```

因此生成源码里是：

```slang
struct OCPushConstants {
    uint64_t params;
    uint dim_x;
    uint dim_y;
    uint dim_z;
    uint group_base_x;
    uint group_base_y;
    uint group_base_z;
    uint words[6u];
};
[[vk::push_constant]] OCPushConstants oc_push;
uint oc_param_word(uint offset) { return oc_push.words[offset / 4u]; }
uint64_t oc_param_u64(uint offset) { ... }
uint oc_param_bits(uint word_offset, uint shift) { ... }
```

纹理描述符本身由
[`_emit_kernel_argument_loads()`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L1822)
和 [`_emit_parameter_load()`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L1730)
按 host ABI 解码。`TEXTURE2D` 的字段宽度是 `{8, 8, 4, 4}`，分别对应
`tex_index`、`surf_index`、`pixel_storage`、padding：

```slang
OCTextureDesc t2d0 = {
    oc_param_u64(0u),
    oc_param_u64(8u),
    oc_param_word(16u),
    oc_param_word(20u)
}; /* 24 bytes at offset 0 */
```

这段代码直接解释了为什么 host 端只需要往 push constant 写 24 字节，shader 端就能
拿到纹理句柄和存储格式。`VkShaderParameterLayout` 同时被 pipeline layout 和
dispatch 使用，保证生成端、布局端和提交端只有一份 ABI 定义。

### 4.5 语句和表达式如何变成 Slang

`_emit_function_body()` 对 kernel 生成：

```slang
[shader("compute")] [numthreads(OC_WORKGROUP_X, OC_WORKGROUP_Y, OC_WORKGROUP_Z)]
void main(uint3 oc_dispatch_thread_id : SV_DispatchThreadID,
          uint3 oc_group_id_in : SV_GroupID,
          uint3 oc_group_thread_id_in : SV_GroupThreadID) {
    uint3 oc_group_base = uint3(oc_push.group_base_x, ...);
    oc_group_id = oc_group_id_in + oc_group_base;
    oc_group_thread_id = oc_group_thread_id_in;
    uint3 d_dim = uint3(oc_push.dim_x, ...);
    uint3 d_idx = oc_dispatch_thread_id +
                  oc_group_base * uint3(OC_WORKGROUP_X, ...);
    if (any(d_idx >= d_dim)) { return; }
    OCTextureDesc t2d0 = { ... };
    ...
}
```

这个“入口 + 尺寸 + 越界保护”的代码由
[`_emit_builtin_vars_define()`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L1694)
生成。它把 Ocarina 的 builtin `Variable::Tag` 映射为局部变量：

| Ocarina builtin | Slang 局部变量 | 说明 |
| --- | --- | --- |
| `DISPATCH_DIM` | `d_dim` | `dispatch_dim()` 返回该值 |
| `DISPATCH_IDX` | `d_idx` | `dispatch_idx()` 返回该值；由线程 ID + group base 计算 |
| `BLOCK_IDX` | `oc_group_id` | 逻辑 group 坐标 |
| `THREAD_IDX` | `oc_group_thread_id` | 组内线程坐标 |
| `THREAD_ID` | `oc_thread_id(d_dim)` | 按需生成 helper |

语句层面的映射由基类 visitor 完成：

- `ScopeStmt`：`{ ... }`，先发局部变量声明，再发语句；
- `IfStmt`：`if (cond) { ... } else { ... }`；
  `AstToSlangSource` 额外做条件到 `bool` 的转换；
- `ForStmt`：C 风格 `for (; cond; var += step)`；
- `AssignStmt`：`lhs = rhs`；如果是 storage reference，则改用
  `oc_storage_store_*` helper；
- `ExprStmt`：直接发表达式（例如 `oc_tex2d_write(...)`）。

表达式层面的映射：

- `RefExpr` → 变量名；
- `LiteralExpr` → 字面量；
- `BinaryExpr` → `(lhs op rhs)`；
- `MemberExpr` → `parent.x`/`parent.xy` 或结构成员；
- `SubscriptExpr` → `range[index]`；
- `CastExpr` → 类型构造或位转换；
- `CallExpr` → 由 `CallOp` 选择 Slang 名字。

[`visit(CallExpr)`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L250) 是一张
大的 `CallOp` 分发表。例如：

| `CallOp` | Slang 输出 |
| --- | --- |
| `MAKE_FLOAT3` / `MAKE_FLOAT4` | `float3(...)` / `float4(...)` |
| `NORMALIZE` | `normalize(...)` |
| `LENGTH` | `length(...)` |
| `POW` | `pow(...)` |
| `TEX2D_WRITE` | `oc_tex2d_write(...)` |
| `TEX2D_READ` | `oc_tex2d_read_<type>(...)` |

`TEX2D_WRITE` 的分支在
[`ast_to_slang_source.cpp:617`](../src/backends/vulkan_compute/ast_to_slang_source.cpp#L617)：

```cpp
case CallOp::TEX2D_WRITE: OC_SLANG_WRAP(tex2d_write); break;
```

`OC_SLANG_WRAP` 会生成 `oc_tex2d_write` 并展开实参。因此 2.4 节的
`CallExpr(TEX2D_WRITE, {texture, value, x, y})` 最终变成：

```slang
oc_tex2d_write(t2d0, v412, v9.x, v9.y);
```

`oc_tex2d_write` 的实现在
[`slang_device_math.slang:1022`](../src/backends/vulkan_compute/builtin/slang/slang_device_math.slang#L1022)，
它先检查 `surf_index` 的边界，再按 `pixel_storage` 处理 normalized 格式，最后写入
bindless surface。

### 4.6 完整的 Slang prelude

`AstToSlangSource::generate()` 只返回 body，不包含编译宏和 include。实际编译时
[`SlangShaderCompiler::compile_program()`](../src/backends/vulkan_compute/slang_compiler.cpp#L434)
在 body 前面拼上：

```cpp
#define OC_RAY_QUERY 0
#define OC_RAY_PIPELINE 0
#define OC_PRIMITIVE_CULLING 0
#define OC_WORKGROUP_X 8
#define OC_WORKGROUP_Y 8
#define OC_WORKGROUP_Z 1
#include "slang_device_resource.slang"
#include "slang_device_math.slang"
<body>
```

示例的 [`compose_slang_source()`](https://github.com/CoronaEngine/Horizon/blob/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/example_ocarina_dsl/example_ocarina_dsl.cpp#L210)
复制了同样的 prelude，并额外加上 `OC_NATIVE_FLOAT32_FMA`（如果设备支持），这样保存的
`.slang` 可以直接阅读，也可以配合 `slangc -I <runtime>/slang` 单独编译。

本次运行生成的源码节选（`<output>/ocarina-dsl-raymarch.slang`）：

```slang
// Generated for the Ocarina DSL example.
// The Vulkan backend prepends this prelude before compiling the body.
#define OC_RAY_QUERY 0
#define OC_RAY_PIPELINE 0
#define OC_PRIMITIVE_CULLING 0
#define OC_WORKGROUP_X 8
#define OC_WORKGROUP_Y 8
#define OC_WORKGROUP_Z 1
#include "slang_device_resource.slang"
#include "slang_device_math.slang"

struct OCPushConstants {
    uint64_t params;
    uint dim_x;
    uint dim_y;
    uint dim_z;
    uint group_base_x;
    uint group_base_y;
    uint group_base_z;
    uint words[6u];
};
[[vk::push_constant]] OCPushConstants oc_push;
...
[shader("compute")] [numthreads(OC_WORKGROUP_X, OC_WORKGROUP_Y, OC_WORKGROUP_Z)]
void main(uint3 oc_dispatch_thread_id : SV_DispatchThreadID,
          uint3 oc_group_id_in : SV_GroupID,
          uint3 oc_group_thread_id_in : SV_GroupThreadID){
    ...
    OCTextureDesc t2d0 = {oc_param_u64(0u), oc_param_u64(8u),
                          oc_param_word(16u), oc_param_word(20u)};
    ...
    for (; ((v128 < uint(0u)) ? (v126_step > v127) : (v126_step < v127));
           v126_step += v128) {
        ...
    }
    ...
    oc_tex2d_write(t2d0, v412, v9.x, v9.y);
}
```

源码中的 `vNNN` 就是 2.3 节所说的局部变量物化结果，不是手写的变量名。

## 5. Slang 编译到 SPIR-V

`AstToSlangSource` 只负责“AST → Slang 文本”。真正编译发生在
[`SlangShaderCompiler::compile_program()`](../src/backends/vulkan_compute/slang_compiler.cpp#L434)：

1. **重新生成 body**：
   ```cpp
   auto [body, function_name, requires_half_rte] = AstToSlangSource::generate(
       function, context, Env::code_obfuscation(), device_->supports_warp32());
   ```
2. **拼完整源码**（prelude + includes + body），并检查半精度 RTE 能力。
3. **计算缓存 key**：源码、描述、stage、Builtin `slang_device_*` 文件、Slang
   build tag、SPIR-V target、float 精度/rounding 选项等都参与
   [`hash64`](../src/backends/vulkan_compute/slang_compiler.cpp#L464)。
4. **准备缓存目录** `cache_directory()/slang_cache`，写 `.slang` 诊断文件，
   尝试读同名 `.spv`。
5. **Slang session**：`create_session()` 以 `SLANG_SPIRV`/`spirv_1_5` 为目标，
   开启 precise floating point 和所需 capability；builtin `slang/` 目录作为
   search path。
6. **加载模块**：`session->loadModuleFromSourceString(program_key, nullptr,
   source, ...)`。
7. **编译入口**：`compile_entry()` 找到 `main`，把 module + entry 组合成
   composite，调用 `getEntryPointCode()` 得到 SPIR-V。
8. **后处理**：按设备能力写 float control execution mode、给 FMA 加
   `NoContraction`、处理 ray query 扩展等。
9. **写缓存**：`.spv` 原子写入；下次相同 key 直接命中。

本次运行首次是 SPIR-V cache miss，日志里能看到：

```text
shader SPIR-V cache miss: kernel__..._compute_main.spv
task Slang compile ... is take <N> ms
```

第二次运行会变成 cache hit。

## 6. Vulkan 执行（简略）

`device.compile()` 进入
[`VulkanComputeDevice::create_shader()`](../src/backends/vulkan_compute/vk_compute_device.cpp#L921)：

1. `analyze_function_requirements()` 检查 subgroup、trace 等能力；
2. `select_trace_mode()` 选择执行模式；示例没有 trace，因此是普通 `Compute`；
3. [`workgroup_size()`](../src/backends/vulkan_compute/vk_compute_device.cpp#L899)
   读取 `Function::block_dim()`，校验设备限制，得到 `8x8x1`；
4. 调用 `compiler_->compile_program()` 得到 `VkShaderProgram`；
5. `VkShaderFactory::create()` 创建 `VkComputeShader`，其中包含 pipeline layout
   和 compute pipeline。

渲染侧只有几步：

```cpp
auto shader = device.compile(kernel, shader_name);
auto texture = device.create_texture2d(resolution, PixelStorage::FLOAT4, shader_name);
std::vector<float4> pixels(width * height);
auto stream = device.create_stream();
stream << shader(texture).dispatch(resolution)
       << texture.download(pixels.data())
       << synchronize()
       << commit();
Image::save_image(image_path, PixelStorage::FLOAT4, resolution, pixels.data());
```

- `create_texture2d` 创建 `FLOAT4` storage texture；
- `shader(texture)` 生成 `ShaderArgumentPack`，`dispatch(resolution)` 生成
  `ShaderDispatchCommand`（[`shader.h:280`](../src/rhi/resources/shader.h#L280)）；
- stream 提交后，后台执行 compute pipeline；
- `texture.download()` 把像素读回 host；
- `Image::save_image()` 转 8-bit RGBA 并写 PNG。

示例的分辨率是 960x540，workgroup 是 8x8，540 不能被 8 整除；生成代码里的
`if (any(d_idx >= d_dim)) { return; }` 负责尾部线程的越界保护。

## 7. Shader 逻辑（简略）

kernel 代码在 [`example_ocarina_dsl.cpp:134`](https://github.com/CoronaEngine/Horizon/blob/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/example_ocarina_dsl/example_ocarina_dsl.cpp#L134)：

- 用 `dispatch_idx().xy()` 得到像素坐标，`dispatch_dim().xy()` 得到尺寸；
- 生成 NDC 坐标，翻转 y，保持图像方向；
- 固定相机 `(0, 0.35, 3.2)` 向 `-z` 看；
- `$for(step, 96u)` 做 96 次 sphere tracing：
  - 球体 SDF：`length(position) - 1`；
  - 地面 SDF：`position.y + 1`；
  - 取较小距离，命中后设置 normal/albedo，`$break`；
- 命中后做简单 lambert + specular + distance fog；
- 未命中时用 `ndc.y` 做天空渐变；
- gamma 校正后写入 `output`。

这段效果不是本文重点；重要的是它覆盖了局部变量、循环、嵌套分支、向量/标量运算、
内建函数和纹理写入，足以展示 DSL 到 Slang 的各类节点。

## 8. 运行时可观察点

| 产物/信号 | 位置 | 用途 |
| --- | --- | --- |
| 示例保存的 Slang | `<output>/ocarina-dsl-raymarch.slang` | 阅读 `AstToSlangSource` 的完整输出 |
| 后端 Slang 诊断缓存 | `<runtime>/.cache/slang_cache/*.slang` | 实际编译的完整源码 |
| SPIR-V 缓存 | `<runtime>/.cache/slang_cache/*.spv` | 命中/未命中判断 |
| 渲染结果 | `<output>/ocarina-dsl-raymarch.png` | 验证 dispatch 和 readback |
| `FunctionCorrect` 日志 | 运行输出 | 校正阶段耗时 |
| `function kernel__... generated` 日志 | 运行输出 | AST → Slang 耗时 |
| `shader SPIR-V cache miss/hit` | 运行输出 | 编译/缓存状态 |

可用参数：

```text
--output <dir>       输出目录
--runtime-dir <dir>  后端和 slang/ 所在目录
--size <WxH>         输出分辨率
--print-slang        打印完整 Slang
```

## 9. 关键文件索引

| 文件 | 作用 |
| --- | --- |
| [`examples/example_ocarina_dsl/example_ocarina_dsl.cpp`](https://github.com/CoronaEngine/Horizon/blob/4177351f122a3e4fa92144af3b3ddb0ef9c5034f/examples/example_ocarina_dsl/example_ocarina_dsl.cpp) | 示例入口、kernel、Slang 保存、dispatch |
| [`src/dsl/core/var.h`](../src/dsl/core/var.h) | `Var<T>`、局部变量、`Texture2DVar` 等类型别名 |
| [`src/dsl/core/ref.h`](../src/dsl/core/ref.h) | `Ref<T>` 表达式包装、纹理/资源代理 |
| [`src/dsl/core/expr.h`](../src/dsl/core/expr.h) | `Expr<T>` 和 host 值到表达式的转换 |
| [`src/dsl/api/func.h`](../src/dsl/api/func.h) | `Kernel`、`Callable`、参数构造 |
| [`src/dsl/api/operators.h`](../src/dsl/api/operators.h) | 运算符到 `BinaryExpr`/`UnaryExpr` |
| [`src/dsl/api/builtin.h`](../src/dsl/api/builtin.h) | DSL 内建函数、`make_*` |
| [`src/dsl/api/syntax.h`](../src/dsl/api/syntax.h) | `$if`/`$for`/`$break` 宏 |
| [`src/dsl/api/stmt_builder.h`](../src/dsl/api/stmt_builder.h) | 语句 builder、`eval`、`assign` |
| [`src/ast/function.*`](../src/ast/function.h) | `Function`、节点 arena、参数、builtin |
| [`src/ast/statement.h`](../src/ast/statement.h) | `ScopeStmt`、`IfStmt`、`ForStmt` 等 |
| [`src/ast/expression.h`](../src/ast/expression.h) | `Expression`、`CallExpr`、`RefExpr` 等 |
| [`src/ast/variable.h`](../src/ast/variable.h) | `Variable::Tag`、uid、Usage |
| [`src/ast/function_corrector.*`](../src/ast/function_corrector.h) | 捕获、usage、参数拆分、context 校正 |
| [`src/ast/function_requirements.*`](../src/ast/function_requirements.h) | 可达函数和能力分析 |
| [`src/backends/vulkan_compute/ast_to_slang_source.*`](../src/backends/vulkan_compute/ast_to_slang_source.h) | AST → Slang |
| [`src/backends/vulkan_compute/slang_compiler.*`](../src/backends/vulkan_compute/slang_compiler.h) | Slang → SPIR-V、缓存 |
| [`src/backends/vulkan_compute/vk_shader_parameters.h`](../src/backends/vulkan_compute/vk_shader_parameters.h) | push constant/BDA 参数 ABI |
| [`src/backends/vulkan_compute/builtin/slang/slang_device_math.slang`](../src/backends/vulkan_compute/builtin/slang/slang_device_math.slang) | `oc_*` 设备端 helper |

## 10. 验证记录

本次实机验证使用 `RelWithDebInfo`：

```powershell
python build_tools/dev.py build HorizonExamples --configuration RelWithDebInfo --with-ocarina
.\build\conan\examples\relwithdebinfo\bin\RelWithDebInfo\HorizonExamples.exe `
    ocarina_dsl --size 960x540 --output .\build\ocarina_dsl_integrated_output
```

结果：

- kernel 参数数量为 1，workgroup 为 `8x8x1`，分辨率为 `960x540`；
- 生成 Slang 404 行，保存为 `ocarina-dsl-raymarch.slang`；
- 首次运行 SPIR-V cache miss，编译后生成 960x540 PNG；
- 保存的 `.slang` 与后端 `.cache/slang_cache` 中实际编译的源码一致，
  仅多两行说明注释；
- 该 `.slang` 已用 `slangc` 配合运行时 `slang/` include path 单独编译通过。
