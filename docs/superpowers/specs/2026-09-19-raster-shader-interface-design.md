# VS/FS AST 与 DSL 接口设计

> 状态：待实施设计；本文中的新增 API 尚不存在。
>
> 调研基线：`refactor_dsl`，`8d015222a37dc46d8355c0d2ab964758aeb555c0`。
>
> 本次交付仅为设计、实施计划及其提交推送；功能代码在另一台机器执行。

配套执行文档：[实施计划](../plans/2026-09-19-raster-shader-interface.md)。
架构依据：[语言层架构](../../architecture/overview.md)、
[GPU 编程架构](../../architecture/gpu-programming-stack.md)。

## 1. 目标与边界

用户希望参照 Helicon，在新的 `horizon::ast` / `horizon::dsl` 中定义 Vertex Shader
和 Fragment Shader，并通过类型化接口连接两个阶段。成功标准是用户可以构造、检查和
校验 VS/FS 的唯一一套 AST，得到可供未来编译层消费的阶段接口描述。

本期实现：

- `VertexShader<Ret(Args...)>`、`FragmentShader<Ret(Args...)>` 及 lambda 签名推导。
- `RasterShader{vs, fs}` 组合、类型匹配和 AST 接口匹配。
- 顶点属性、varying、单颜色/MRT/无颜色输出、六个光栅化 builtin 与 `discard()`。
- 自动 location、默认插值、节点所有权、调用链校验、诊断和结构 hash。

本期不实现 shader 编译、IR、Slang emitter、SPIR-V、pipeline、draw、真实 GPU 资源捕获、
纹理采样扩展、导数、深度写出、sample mask、显式 location/插值覆盖、geometry/tessellation/mesh
阶段。VS/FS 构造成功不表示 shader 已通过后端编译或 GPU 验证。

### 全局约束

- 使用 C++20；主验证平台为 Windows x64、MSVC、Ninja Multi-Config。
- 生产代码修改限于 `src/ast/` 和 `src/dsl/`；配套测试与文档单独列入执行范围。
- 依赖保持 `dsl -> ast -> math -> core`，不新增第三方依赖。
- AST 只维护一套模型；所有节点由所属 `ast::Function` 创建和持有。
- 新符号使用 `horizon::ast` / `horizon::dsl`；类型 CamelCase，函数与变量 snake_case。
- 不查阅或修改 `modules/ocarina/`；Helicon 仅作本任务已获授权的只读参考。
- 不修改 Helicon、RHI、Runtime、Conan 或目标族配置，不把 `Helicon` 链接到 `horizon-dsl`。
- 所有构建命令从仓库根目录执行，使用独立的 `core` 构建目录。
- 实现期遵守执行时的 `AGENTS.md`；本文不自动授权未来代码提交或推送。

## 2. 当前实现与复用决策

| 当前文件 | 已有能力 | 本期处理 |
| --- | --- | --- |
| `src/dsl/api/func.h` | FuncWrapper、Callable、Kernel、参数构造及签名转换 | 复用 traits 和构造方法，新增独立 raster 包装，不改变现有 Kernel API |
| `src/dsl/types/struct.h` | OC_STRUCT 与 Var 的结构体成员访问 | 继续使用，无需第二套 shader 结构体宏 |
| `src/ast/function.h` | Kernel/Callable、节点所有权、Context、返回值、builtin 工厂 | 增加 Vertex/Fragment 入口和接口元数据 |
| `src/ast/function_corrector.cpp` | 捕获校正、Kernel 参数结构体拆分 | raster 执行 Context/捕获校正，但不走 Kernel 参数拆分 |
| `src/Helicon/Codegen/RasterizedPipelineObject.h` | VS/FS lambda 配对、返回值输出、MRT | 借鉴使用方式和语义，重写为新 AST 构造 |
| `src/Helicon/Codegen/BuiltinVariate.h` | position、front face、vertex/instance/draw index | 保留语义，改为 snake_case 并区分 VS position 与 FS position |
| `src/Helicon/Codegen/Generator/SlangGenerator.cpp` | LOCATION、SV_TARGET 和系统语义映射 | 用于确认语义边界；本期不生成这些字符串 |

三种可行路线中，选择“独立 VS/FS 包装 + 单一 Function AST”。整体复制旧 pipeline object
会引入旧 AST、编译和物理资源状态；仅给 `Kernel<void(...)>` 增加阶段标记则不能清楚表达
varying 返回值与片元颜色输出。所选路线保留 Helicon 的 lambda 体验，同时保持新模块边界。

旧资源 proxy 保存 `HardwareImage` 等物理对象，旧 AST 参与源码生成，均不迁入。
旧 MRT 只有一层成员展开；本设计对允许的嵌套结构体递归展开。

## 3. 公开 DSL

新入口：`dsl/api/raster.h`；由 `dsl/dsl.h` 聚合。
下列代码为实现后的目标用法，并非已验证可编译的当前 API：

```cpp
#include "dsl/dsl.h"

namespace sample {
struct Varyings {
    horizon::math::float3 color;
};
}
OC_STRUCT(sample, Varyings, color) {};

void build_shader() {
    using namespace horizon::dsl;
    using horizon::math::float3;
    using horizon::math::float4;

    VertexShader vs{[](Var<float3> pos, Var<float3> color) {
        vertex_position() = make_float4(pos, 1.0f);
        Var<sample::Varyings> out;
        out.color = color;
        return out;
    }};
    FragmentShader fs{[](Var<sample::Varyings> in) {
        return make_float4(in.color, 1.0f);
    }};
    RasterShader shader{vs, fs};
    const auto vertex_function = shader.vertex().function();
    const auto fragment_function = shader.fragment().function();
}
```

### 3.1 类型与生命周期

```cpp
template<typename Signature> class VertexShader;
template<typename Signature> class FragmentShader;
template<typename VS, typename FS> class RasterShader;
```

同时公开 bool 变量模板 `is_raster_pair_compatible_v<VS, FS>`，规则见 3.2 节。
阶段类只对函数签名特化；
具有 `using signature = Ret(Args...)`、`using return_type = Ret`，以及
`function() const noexcept -> horizon::core::shared_ptr<const ast::Function>`。
`RasterShader::vertex()` / `fragment()` 返回 `const VS &` / `const FS &`。

- 支持显式签名与非泛型 lambda 的 CTAD；泛型 lambda 必须提供显式签名。
- 参数沿用 `Var<T>` / `const Var<T> &` 的定义方式；拒绝 mutable 引用参数与裸指针参数。
- 非 void 返回值须为 `Var<T>`；void 分支支持无参数/无 varying/无颜色输出。
- 阶段对象可复制、移动，共享 Function 所有权；组合对象持有阶段对象的值副本。
- 不提供空的默认阶段对象，也不提供 stage 的 host 调用 `operator()`。
- 构造 lambda 执行一次；组合与查询不重跑 lambda。
- 新包装仅公开 const Function 句柄。可以复用 FuncWrapper 内部思想，但不继承其公开的
  mutable `function()` 接口。现有 Callable/Kernel 的接口保持原样。
- 新构造器可能抛出诊断异常，不标为 `noexcept`。不能让异常穿过现有 `_define()` 的
  `noexcept` 边界。

### 3.2 VS/FS 签名约束

- VS 可以有零个或多个参数；参数都是顶点输入。零参数 VS 可通过 `vertex_index()` 构造几何。
- VS 返回 `T` 时，FS 恰有一个 `T` 参数；VS 返回 void 时，FS 没有 varying 参数。
- C++ 层要求同一个值类型，不接受仅布局相同但类型不同的结构体。
- AST 层再次比较解析后的叶子类型、路径、location、插值；不能只依赖 C++ 类型相同。
- `is_raster_pair_compatible_v<VS, FS>` 对错误阶段顺序、错误参数数目、错误值类型返回 false。
  `RasterShader` 构造器使用相同条件约束，形成可测试的编译期拒绝。
- 单独构造的 FS 可被多个兼容 VS 组合使用；组合不得改写它的输入布局。

普通 C++ 值捕获沿用现有 DSL 构造方式。阶段参数/返回类型不得是 GPU resource，阶段也不得
带有 `CapturedResource` 物理资源记录；后续逻辑资源绑定是另一个任务。由现有 Lambda 机制
校正的纯 DSL 局部变量捕获可以使用，仍须满足 Context 和权限校验。

## 4. 阶段接口布局

新建 `src/ast/shader_interface.h/.cpp`，表示逻辑接口而非 CPU 内存布局：

```cpp
enum class Interpolation : uint8_t { None, Smooth, Flat };

struct ShaderInterfaceSlot {
    const horizon::core::Type *type{};
    uint32_t root_index{};
    horizon::core::vector<uint32_t> member_path;
    uint32_t location{};
    Interpolation interpolation{Interpolation::None};
};

struct ShaderInterface {
    horizon::core::vector<ShaderInterfaceSlot> inputs;
    horizon::core::vector<ShaderInterfaceSlot> outputs;
};
```

input 的 `root_index` 是声明参数下标；output 的 `root_index` 恒为 0，表示返回值。
`member_path` 依次记录嵌套结构体成员下标，标量/向量根的路径为空。类型指针引用现有 Core
类型系统，不拥有类型。 builtin 保存在 `builtin_vars()` 中，不混入 location 列表。

布局算法：

1. 输入按参数顺序，结构体按注册成员顺序进行深度优先展开；输出按返回类型同样展开。
2. 首轮叶子仅允许解析后的 32 位 float/int/uint，以及这些标量组成的 2/3/4 维向量。
   嵌套结构体必须非空且全部叶子合法。bool、half、64 位类型、数组、矩阵、资源等显式拒绝。
   `real` 若先按 Function 精度策略解析为合法的 float32 则接受，解析为 half 则拒绝。
3. 每个叶子占一个 location，各阶段输入、输出独立从 0 顺序递增。
4. VS output 与 FS input 中，浮点叶子为 Smooth，整型叶子为 Flat；VS input 和 FS output
   为 None。builtin 不占普通 location。
5. FS 返回叶子即 attachment 0；返回结构体即逐叶子的 MRT。void 返回得到空 output 列表。
   返回的是颜色值，不是纹理对象。空结构体不等同于 void。

本期没有显式 location 或插值 setter；元数据只由构造流程生成，避免构造后修改布局与
缓存 hash 不一致。枚举与描述位于 AST，不修改 `core::Type`，不把 CPU offset/stride、
VkFormat 或 descriptor set 当成 stage interface。

## 5. AST 构造与校正

- 在 `Function::Tag` 尾部追加 `Vertex`、`Fragment`，保留 Kernel/Callable 原枚举值。
- 增加 `is_vertex()`、`is_fragment()`、`is_raster()`、`is_entry_point()`。
  `is_kernel()` 仍只表示原 Kernel，`is_general_kernel()` 不包含 raster。
- 增加 `shader_interface() const noexcept -> const ShaderInterface &`。
- 新建 `Function::define_raster(Tag tag, const Type *return_type, Func &&func)`，
  返回 `shared_ptr<Function>`，tag 只接受 Vertex/Fragment；void 通过 nullptr 表达。
- 工厂固定签名声明的返回类型；raster 的 `return_()` 不用后出现的 return 覆盖它。
  每一个 ReturnStmt 都与声明类型核对，包括 `$return()`。
- 工厂以 RAII 保证 Function 栈和 Scope 栈恢复：构造、调用图预检、校正、接口展开、验证成功后才返回。
  验证失败或用户 lambda 抛异常均不污染下一个 shader 的构造上下文。
  递归环预检必须早于会递归遍历调用图的 Corrector，不能等栈溢出后再诊断。
- 复用现有节点、CallExpr、ReturnStmt、AssignStmt。stage input 使用新增的 `Variable::Tag::StageInput`；
  input 根不可写，但拷贝到新的局部 `Var` 后局部变量可写。
- Corrector 对 raster 做纯 DSL 捕获和 Context 校正；`splitting_arguments()` 仍仅针对原 Kernel。
  新接口展开是 metadata 操作，不拆改 AST 参数和返回结构体。
- 检查 callable/lambda 的返回值和附加参数映射在 raster 根处仍然成立。

`Function::compute_hash()` 纳入接口的有序字段、路径、location、插值和阶段；不得对包含
vector/pointer 的整个 metadata struct 做原始内存 hash。布局最终确定后使 Function hash
缓存失效；阶段包装公开前不得计算并保留不完整的 hash。现有 hash 不承诺跨编译器/机器
稳定，本期也不把它宣称为持久化 ShaderArtifact key。

## 6. builtin 与 discard

新建 `src/dsl/api/raster_builtin.h`，由 raster.h 引入。

| DSL API | DSL 返回类型 | AST Variable::Tag | 允许入口与权限 |
| --- | --- | --- | --- |
| `vertex_position()` | `Var<math::float4>` | VertexPosition | VS，可读写的 builtin 输出 |
| `vertex_index()` | `Expr<uint>` | VertexIndex | VS，只读 |
| `instance_index()` | `Expr<uint>` | InstanceIndex | VS，只读 |
| `draw_index()` | `Expr<uint>` | DrawIndex | VS，只读 |
| `fragment_coord()` | `Expr<math::float4>` | FragmentCoord | FS，只读 |
| `front_facing()` | `Expr<bool>` | FrontFacing | FS，只读 |
| `discard()` | void | DiscardStmt | FS |

AST Function 增加对应同名 builtin 工厂，返回 `const RefExpr *`，通过现有 `_builtin()`
在所属函数内去重。`discard()` 创建无子表达式的 DiscardStmt，补齐 Tag、虚函数 Visitor、
Context 检查与语义 hash，不能退化成字符串或 ReturnStmt。

position 使用 `Var<float4>{builtin_expr}` 的显式表达式构造器直接引用 builtin；不使用
`eval()` 生成局部副本。现有 Ref 的表达式构造器为 protected，且没有 Var 的赋值门面，
因此公开返回 Var 才能沿用示例中的赋值语法。只读输入返回 Expr，其成员别名的权限仍由 AST 校验。

无当前 Function 时，新 builtin API 抛 `std::logic_error`。Callable 中记录这些操作时
暂不要求具体入口阶段；在入口可达调用图上检验。禁止把共享 Callable 永久标成首次调用者
的阶段。Compute Kernel 调用带 raster 操作的 Callable 也必须诊断，不能绕过校验。

数值语义按 Helicon 的既有意图记录：vertex index 含 indexed draw 的 vertex offset，
instance index 含 first instance；draw index 为 draw 标识。本文不保证所有未来后端都
支持这些系统值，后端能力检测在编译集成时完成，不由本期伪造常量替代。

## 7. 校验与诊断

新建 `src/ast/raster_validation.h/.cpp`：

```cpp
enum class RasterDiagnosticCode : uint8_t {
    InvalidStage, InvalidInterfaceType, InterfaceMismatch,
    InvalidBuiltinStage, ReadOnlyWrite, InvalidReturn,
    MissingVertexPosition, PhysicalResourceCapture, RecursiveCall
};
struct RasterDiagnostic {
    RasterDiagnosticCode code;
    horizon::core::string message;
};

horizon::core::vector<RasterDiagnostic>
validate_raster_function(const Function &function);
horizon::core::vector<RasterDiagnostic>
validate_raster_pair(const Function &vertex, const Function &fragment);
```

增加 `RasterValidationError : public std::invalid_argument`，持有 diagnostics，
`diagnostics() const noexcept -> span<const RasterDiagnostic>`。DSL 构造/组合遇到非空诊断
抛此异常，消息包含阶段、函数 description 及可定位的参数/成员路径；AST 层结果可直接测试。
无效接口类型在展开时产生同一类诊断，不应依赖 assert 或后端报错。

验证细则：

- 遍历每个阶段可达的 Callable，并分别检查其自身 Context。DFS 使用 visiting/visited 集，
  检测递归环；不要把其他函数节点移入入口的 Context。
- 按入口阶段检查全部 builtin 和 discard；raster 中拒绝已有 compute 专属 builtin 与
  block 同步，ray tracing 操作保持不在本期 raster 支持范围。
- 检查 AssignStmt 左值的根变量，包括 MemberExpr、SubscriptExpr、swizzle；StageInput 和
  只读 builtin 不能写。对调用的引用参数逐调用点追踪实际参数及被调用者写入，不能只查直接赋值。
  `Expr<T>` 的 C++ const 性不是唯一防线。
- VS 要有 VertexPosition 输出及至少一次可达 AST 写入；只读取或创建该 builtin 不算写入。
  写入可以位于可达 Callable；阶段语义汇总自调用图，不需要把 Callable 的 builtin 节点复制到入口。
  首轮不做完整 CFG 的所有路径确定赋值证明，后续 compiler 仍需验证这种情况。
- 所有 ReturnStmt 与各自函数的声明类型一致，不能把被调用 Callable 的返回与入口返回混淆。
- 比较 VS/FS 普通接口叶子，报告第一处位置/路径/类型/插值差异；资源捕获记录非空则拒绝。
- VS/FS 配对校验为只读，不改变接口、builtin 列表、Function hash 或 Callable 状态。
- 原 Kernel 只新增对可达 raster 专属操作的拒绝，已有正常 compute/RTX 用法不得因此改变。
  若原 noexcept Kernel 构造链采用现有 fatal 诊断机制，使用独立进程测试其拒绝行为；不要求
  为此改造整个工程异常策略。

## 8. 验收与实施顺序

按配套计划依次完成：入口和诊断基础 → 接口布局 → builtin/discard/调用链校验 →
阶段 DSL → RasterShader 配对 → 完整兼容性与文档。

必须覆盖：

1. 标量/向量/嵌套结构体布局；整数 varying 为 Flat；MRT 与 void 输出。
2. 签名推导、显式签名、零顶点属性、VS void/FS 无参数、错误签名拒绝。
3. 不同解析精度产生的实际类型差异；不合法叶子、资源类型、空结构体拒绝。
4. builtin 去重、Context、输入成员/引用参数间接写入、位置未写入、discard Visitor。
5. 同一个 Callable 被多个入口复用时，合法调用保持合法，非法调用仍然拒绝。
6. 构造失败后的栈恢复、组合对象生命周期、只读接口及最终 hash。
7. 现有 AST、DSL、纹理 DSL 和模块依赖边界测试。

仅 host 验证，最终报告必须明确尚未进行 Slang/SPIR-V 编译与 GPU 验证。
