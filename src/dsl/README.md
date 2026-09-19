# Horizon DSL

DSL 将 C++ 的 `Var<T>`、运算和语句构造为唯一的 `horizon::ast::Function` AST。
`horizon::dsl::detail::Expr<T>` 是内部接口，用户无需直接使用。

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
| `discard()` | `void` | FS | 丢弃当前片元 |

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

## 错误处理与首轮边界

阶段和组合构造器可能抛出 `horizon::ast::RasterValidationError`，其 `diagnostics()` 提供
稳定错误码和上下文信息：InvalidStage、InvalidInterfaceType、InterfaceMismatch、
InvalidBuiltinStage、ReadOnlyWrite、InvalidReturn、MissingVertexPosition、
PhysicalResourceCapture、RecursiveCall。阶段 lambda 及其 if/switch/loop/for/scope 回调抛异常后
会恢复 Function/Scope 构造栈；语法宏不会再强制将这些回调标为 noexcept。
现有 Callable/Kernel 构造器的 noexcept 合同保持不变。
在没有 current Function 时调用上述 builtin 或 discard，会抛出 `std::logic_error`。

Callable 的阶段限制和引用实参权限按实际入口、每个调用点重新检查。VS/FS 禁止计算阶段
builtin、线程组同步、光追操作、递归调用和物理 GPU 资源捕获；Kernel 也拒绝光栅化 builtin/discard。
现有 Kernel 的错误处理仍使用原来的 fatal 路径。

低层 AST 也拒绝引用型阶段参数；ForStmt 的隐式步进按写入检查。
非 void 函数须出现值返回或可达的片元 discard；当前仍不做完整 CFG 路径证明。
阶段最终化会清除可达语句、表达式、作用域和函数的 hash 缓存，构造期查询 hash 不影响最终结果。

当前不支持接口中的 bool、half、64 位类型、数组、矩阵、资源和空结构体；不提供显式
location/插值覆盖、导数、深度写出、sample mask、纹理采样扩展或其他图形阶段。
不包含 IR、Slang emitter、SPIR-V、pipeline、draw 或 Runtime 资源绑定。

`horizon-test-ast-raster` 和 `horizon-test-dsl-raster` 验证 host AST 行为与上述示例用法。
构建命令见仓库 [README](../../README.md)，实现约定见[设计文档](../../docs/superpowers/specs/2026-09-19-raster-shader-interface-design.md)。
