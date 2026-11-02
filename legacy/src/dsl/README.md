# Horizon DSL

DSL 将 C++ 的 `Var<T>`、运算和语句构造为唯一的 `horizon::ast::Function` AST。
`horizon::dsl::detail::Expr<T>` 是内部接口，用户无需直接使用。
DSL 不提供 `print()` / `prints()` 或对应的 AST 打印语句；调试 host 构造过程使用主机日志接口。

`OC_STRUCT` 的动态尺寸注册宏 `OC_MAKE_STRUCT_IS_DYNAMIC` 统一定义在 `dsl/types/struct.h`，
为 `horizon::math::is_dynamic_size` 生成特化：任一成员为动态尺寸时，结构体也为动态尺寸，
支持已注册的嵌套结构体。该宏不再由 Core 的 `type_desc.h` 提供，Core 不依赖 Math trait。

## VS / FS 当前实现

`dsl/api/raster.h` 可独立包含，也由 `dsl/dsl.h` 聚合。当前可构造、检查和组合 VS/FS，
生成逻辑接口布局并校验阶段、类型、写入权限和调用链。**尚未接入 shader 编译或 GPU 执行**。

```cpp
#include "dsl/api/raster.h"

namespace sample {
struct Varyings {
    horizon::math::float3 color;
};
}
OC_STRUCT(sample, Varyings, color) {};

void build_shader() {
    using namespace horizon::dsl;
    using horizon::math::float3;

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

阶段参数使用 `Var<T>` 或 `const Var<T> &`，非 void 返回值必须为 `Var<T>`。
可显式写 `VertexShader<Ret(Args...)>` / `FragmentShader<Ret(Args...)>`；泛型 lambda 需要显式签名。
拒绝裸指针和 mutable 引用参数。阶段可复制、移动，持有共享的 const Function 句柄，
不支持默认空构造或 host 调用。lambda 仅在构造阶段执行一次；组合持有阶段的值副本。

VS 可以没有属性，使用索引构造几何。VS 返回 `T` 时，FS 恰好接受一个同类型 `T`；
VS 返回 void 时，FS 无参数。FS 可返回颜色值、嵌套结构体 MRT 或 void。
`is_raster_pair_compatible_v<VS, FS>` 检查 C++ 签名，组合时再次校验实际 AST 布局。
同一 FS 可用于多个兼容组合，组合不修改阶段的元数据或 hash。

## 默认接口布局

- 参数按声明顺序、结构体按注册成员顺序递归展开；每个叶子占一个 location。
- 各阶段输入和输出各自从 location 0 开始；builtin 单独记录，不占普通 location。
- 叶子仅接受 32 位 float/int/uint 及其 2/3/4 维向量。非空嵌套结构体的全部叶子都须合法。
- VS 输出和 FS 输入的浮点叶子默认 Smooth，整型叶子默认 Flat；VS 输入和 FS 输出为 None。
- FS 输出叶子依次对应颜色 attachment；void 没有颜色输出。空结构体不等同于 void。
- `real` 按阶段精度策略解析后检查；float32 可以使用，half 被拒绝。

布局可从 `function()->shader_interface()` 读取。每个 slot 记录实际类型、参数 root_index、
嵌套 member_path、location 和 interpolation；输出 root_index 恒为 0。

## Builtin 与写入权限

| API | 返回值 | 阶段 | 权限 / 作用 |
| --- | --- | --- | --- |
| `vertex_position()` | `Float4` | VS | 可读写，齐次裁剪空间输出；必须存在可达的写入 |
| `vertex_index()` | `Uint` | VS | 只读顶点索引 |
| `instance_index()` | `Uint` | VS | 只读实例索引 |
| `draw_index()` | `Uint` | VS | 只读 draw 标识，保留 Slang 扩展语义 |
| `fragment_coord()` | `Float4` | FS | 只读片元坐标 |
| `front_facing()` | `Bool` | FS | 只读正面标记 |
| `primitive_index()` | `Uint` | FS | 只读图元编号，表达 `SV_PrimitiveID` |
| `fragment_depth()` | `Float` | FS | 可读写深度输出，表达 `SV_Depth` |
| `fragment_depth_greater_equal()` | `Float` | FS | 可读写保守深度输出，表达 `SV_DepthGreaterEqual` |
| `fragment_depth_less_equal()` | `Float` | FS | 可读写保守深度输出，表达 `SV_DepthLessEqual` |
| `sample_index()` | `Uint` | FS | 只读 MSAA 采样编号，表达 `SV_SampleIndex` |
| `sample_mask()` | `Uint` | FS | 只读 32 位输入覆盖掩码，表达 `SV_Coverage` 输入 |
| `sample_mask_output()` | `Uint` | FS | 可读写 32 位输出覆盖掩码，表达 `SV_Coverage` 输出 |
| `clip_distances<N>()` | `Var<array<float, N>>` | VS / FS | VS 可读写，FS 只读，表达 `SV_ClipDistance` 分量数组 |
| `cull_distances<N>()` | `Var<array<float, N>>` | VS / FS | VS 可读写，FS 只读，表达 `SV_CullDistance` 分量数组 |
| `render_target_array_index()` | `Uint` | VS / FS | VS 可读写，FS 只读，表达 `SV_RenderTargetArrayIndex` |
| `viewport_array_index()` | `Uint` | VS / FS | VS 可读写，FS 只读，表达 `SV_ViewportArrayIndex` |
| `stencil_ref()` | `Uint` | FS | 可读写模板参考值输出，表达 `SV_StencilRef` |
| `shading_rate()` | `Uint` | VS / FS | VS 可读写，FS 只读，表达 `SV_ShadingRate` |
| `discard()` | `void` | FS | 丢弃当前片元 |

这里的 HLSL 语义仅描述目标映射，当前仍只生成 AST。图元编号、目标层、视口、模板参考值、
着色速率等功能的设备能力、扩展、pipeline 状态与具体编码留给未来 compiler/RHI 校验。
本次没有新增几何、细分曲面、Mesh 等 shader 阶段。

输入和输出的覆盖掩码是两个独立 builtin，可以写 `sample_mask_output() = sample_mask()`。
三个深度输出入口在同一入口的可达调用链中互斥，不能通过不同 Callable 混用模式。
声明系统输出后必须有可达写入；和 VS position 一样，这只是结构检查，不证明每条执行路径、
每个数组元素或读取之前都已完成赋值，也不证明保守深度值满足其大小约束。

距离数组的 `N` 必须在 1 到 8 之间，同一入口的可达调用链中 clip/cull 分量总数不得超过 8。
同一种距离 builtin 在所有可达函数中的数组长度必须一致。FS 使用距离数组时，配对 VS 必须
提供同种、同长度的输出；VS 可以提供 FS 不读取的距离数组。这些数组不占用普通 location，
也不改变普通阶段参数仍禁止数组的规则。低层入口是 `Function::clip_distances(count)` 和
`Function::cull_distances(count)`，无效或重复不一致的长度会抛出 `RasterValidationError`。
内部共用 `Function::array_builtin(tag, element_type, count)` 构造和复用数组型 builtin，
检查解析后的元素类型和长度是否一致；clip/cull 的长度限制由专属校验负责。

```cpp
VertexShader vs{[] {
    vertex_position() = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
    auto clip = clip_distances<2>();
    clip[0] = 1.0f;
    clip[1] = 1.0f;
}};
FragmentShader fs{[] {
    sample_mask_output() = sample_mask();
    fragment_depth() = fragment_coord().z;
    return make_float4(clip_distances<2>()[0]);
}};
RasterShader shader{vs, fs};
```

VS 的索引设计沿用已有 Helicon 语义：indexed draw 的 vertex index 包含 vertexOffset，
instance index 包含 firstInstance。这里仅记录 AST builtin，实际取值和 HLSL/Slang 映射
须由未来后端实现并验证；完整映射及未覆盖项见[实施计划](../../docs/superpowers/plans/2026-09-19-raster-shader-interface.md)。

getter 直接返回 builtin 的 `Var` 包装，`auto` 接收或移动包装不会改变 AST 写权限。
只读规则由 AST tag 校验；直接赋值、`set`、成员/swizzle、下标，以及通过 Callable 引用参数
写入同一只读根变量都会被拒绝。阶段输入也只读。从已有左值复制会创建可写 Local：

```cpp
FragmentShader fs{[] {
    Float4 coord = fragment_coord(); // 直接引用只读 builtin
    Float4 local = coord;            // 从左值复制，生成独立 Local
    local.x = 0.0f;                  // 合法，不写回 builtin
    return local;
}};
```

直接执行 `fragment_coord().x = 0.0f` 或修改 `coord` 会在阶段构造结束时抛出诊断。
VS position 的检查只要求 AST 可达调用链中存在写入，不证明每条运行路径都完整写出位置。

## 片元导数

`ddx(value)`、`ddy(value)` 和 `fwidth(value)` 由 `dsl/api/builtin.h` 提供；分别生成
`CallOp::Ddx`、`CallOp::Ddy`、`CallOp::Fwidth`。`fwidth` 表达 `abs(ddx(value)) + abs(ddy(value))`。
参数为 DSL 的 float/half/real 标量或 2/3/4 维向量，支持成员和 swizzle，返回同形状的 `Var`。
返回值会立即保存为可写 Local；后续修改输入不会重新计算此前的导数结果。
整数、bool、矩阵和数组不支持；real 按当前 Function 的精度策略解析为 float 或 half。

```cpp
FragmentShader fs{[](Float2 uv) {
    auto dx = ddx(uv);
    auto dy = ddy(uv);
    auto width = fwidth(uv);
    return make_float4(dx.x, dy.y, width.x, width.y);
}};
```

导数只能从 FS 入口使用，也可封装在 Callable 内；从 VS 或 Kernel 间接调用仍被拒绝。
低层 AST 同样检查参数数量、浮点类型与返回形状。不支持依赖额外设备特性的 Compute 导数，
也暂不区分 fine/coarse 变体；非一致控制流中的导数语义留给后续编译器处理。
无 current Function 时调用导数函数会抛出 `std::logic_error`。

## 错误处理与首轮边界

阶段和组合构造器可能抛出 `horizon::ast::RasterValidationError`，其 `diagnostics()` 提供
稳定错误码和上下文信息：InvalidStage、InvalidInterfaceType、InterfaceMismatch、
InvalidBuiltinStage、ReadOnlyWrite、InvalidReturn、MissingVertexPosition、
PhysicalResourceCapture、RecursiveCall，以及新增的 InvalidBuiltinType、InvalidBuiltinConfiguration、
MissingBuiltinWrite。阶段 lambda 及其 if/switch/loop/for/scope 回调抛异常后
会恢复 Function/Scope 构造栈；语法宏不会再强制将这些回调标为 noexcept。
现有 Callable/Kernel 构造器的 noexcept 合同保持不变。
在没有 current Function 时调用上述 builtin 或 discard，会抛出 `std::logic_error`。

诊断中的函数类型名称统一取自 AST 的 `Function::tag_name()`，返回指向静态字符串的
`string_view`；完整诊断消息仍使用拥有存储的 `string`。

Callable 的阶段限制和引用实参权限按实际入口、每个调用点重新检查。VS/FS 禁止计算阶段
builtin、线程组同步、光追操作、递归调用和物理 GPU 资源捕获；Kernel 也拒绝光栅化 builtin/discard。
现有 Kernel 的错误处理仍使用原来的 fatal 路径。

低层 AST 也拒绝引用型阶段参数；ForStmt 的隐式步进按写入检查。
非 void 函数须出现值返回或可达的片元 discard；当前仍不做完整 CFG 路径证明。
阶段最终化会清除可达语句、表达式、作用域和函数的 hash 缓存，构造期查询 hash 不影响最终结果。

当前不支持普通接口中的 bool、half、64 位类型、数组、矩阵、资源和空结构体；不提供显式
location/插值覆盖、纹理采样扩展或其他图形阶段。
不包含 IR、Slang emitter、SPIR-V、pipeline、draw 或 Runtime 资源绑定。

`horizon-test-ast-raster` 和 `horizon-test-dsl-raster` 验证 host AST 行为与上述示例用法。
新增 builtin 与导数的回归范围如下：

| 范围 | 已有用例 |
| --- | --- |
| 系统值 | VS/FS/Kernel 阶段限制、只读写入、全部新增系统输出的缺失写入诊断、嵌套 Callable 引用写入、仅在 Callable 内声明的输出 |
| 深度模式 | 三种模式的全部配对、同模式复用、未调用 Callable 不影响入口 |
| 距离数组 | 1/8 合法边界、0/9 非法长度、重复复用、冲突后原声明保留、跨 Callable 长度一致性和去重计数、总数恰好 8/超过 8、VS/FS 配对、显式 float 不随精度策略变化 |
| 导数类型 | scalar/vector 和 swizzle、swizzle 读写使用跨平台一致的 uint 分量下标、结果局部变量、输入修改后的求值顺序、half/real 的 48 个操作/形状/策略组合、低层 AST 的非法类型、空或多余实参、模板参数、返回类型不匹配 |
| 构造错误 | 全部新增 getter 和三个导数函数在无 current Function 时抛错、缺失输出写入后的构造栈恢复 |

这些是行为回归用例，不是行覆盖率或分支覆盖率报告。当前不验证 HLSL/SPIR-V 生成或 GPU 数值。
私有 `array_builtin` 目前只有 float 距离数组调用者；其他元素类型的通用能力将在增加实际调用者时补测。
构建命令见仓库 [README](../../README.md)，实现约定见[设计文档](../../docs/superpowers/specs/2026-09-19-raster-shader-interface-design.md)。
