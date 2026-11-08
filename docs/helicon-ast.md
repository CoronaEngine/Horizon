## Helicon AST 迁移指南

本文描述当前 `src/Helicon` 已经落地的 Helicon AST（C++ EDSL）实现，并作为把
Helicon AST 的**节点语义和生成行为**扩展进 Ocarina AST（`src/ast`）的参考。
目标不是替换 Ocarina AST，而是在保持现有 Ocarina 功能、ABI 和生成结果不变的前提
下增加新能力。扩展范围只覆盖 AST 层：Helicon 的 C++ EDSL 前端（`VariateProxy`、
`ParseHelper`、`$IF/$ELSE` 宏等）不在范围内，只作为理解 AST 行为的参考。
第 1-4 节描述 Helicon 侧行为，第 5 节给出 Ocarina AST 的扩展边界、差异和建议；
第 1-4 节中的代理/宏不代表需要迁移前端。

### 代码结构

```text
src/Helicon/
├── Helicon.h                              # 对外总入口
├── Codegen/
│   ├── AST/
│   │   ├── Enum.hpp                       # ShaderStage、AccessPermissions
│   │   ├── Struct.hpp                     # EmbeddedShaderStructure、BranchInfo、BranchOutput
│   │   ├── Node.hpp / Node.cpp            # 节点定义与 parse 分发
│   │   ├── AST.hpp / AST.cpp              # 节点工厂、作用域、内建变量
│   │   └── Parser.hpp / Parser.cpp        # thread_local 解析状态、ParseOutput 提交
│   ├── Generator/
│   │   └── SlangGenerator.hpp / .cpp      # 节点树 -> Slang 源码
│   ├── VariateProxy.h                     # 值/数组/纹理/函数代理与运算符重载
│   ├── MathProxy.h                        # swizzle 代理
│   ├── BuiltinVariate.h                   # position()、dispatchThreadID() 等内建变量
│   ├── CustomLibrary.h                    # abs/pow/dot/texture 等到 Slang 内建的映射
│   ├── ControlFlows.h                     # $IF/$ELSE/$WHILE/$DO/$FOR/$DISCARD 与 include 宏
│   ├── TypeAlias.h                        # Float/Float4/Texture2D/Array/Aggregate 等别名
│   ├── ParseHelper.h / .cpp               # lambda 调用、参数输入、聚合展开状态
│   ├── RasterizedPipelineObject.h         # 光栅管线入口：VS/FS 解析、bindless 变体、auto-bind
│   └── ComputePipelineObject.h            # 计算管线入口
├── Compiler/
│   ├── ShaderCommon.h                     # ShaderLanguage/ShaderStage 等公共类型
│   ├── ShaderCodeCompiler.h / .cpp        # Slang session/module、compose/link、缓存
│   ├── ShaderLanguageConverter.h / .cpp   # Slang 低层封装、反射、离线模块编译
│   ├── ShaderHardcodeManager.h / .cpp     # Debug 写 hardcode、Release 读取
│   ├── ShaderTypeMapping.h / .cpp         # GLSL/HLSL 类型名 -> Slang/C++ 类型
│   └── ShaderUtils.h / .cpp               # 字面量序列化、路径清理
└── CMakeLists.txt
```

对应的构建期 shader 扫描逻辑在 `build_tools/cmake/HeliconShaderCompile.cmake`，
离线 shader 编译工具在 `tools/main.cpp`（目标名 `ShaderCompileScripts`）。

目标侧 Ocarina AST 的对应位置：

```text
src/ast/
├── ast_node.h                 # ASTNode：Function* context、check_context
├── expression.h / .cpp        # Expression/Ref/Literal/Binary/Member/Subscript/Call/Cast/Conditional
├── statement.h / .cpp         # Scope/If/Loop/For/Switch/Assign/Return/Break/Continue
├── function.h / .cpp          # Function：拥有节点、作用域、参数、捕获资源、builtin
├── variable.h / .cpp          # Variable：uid、Tag、Usage
├── op.h                       # UnaryOp/BinaryOp/CastOp/CallOp
├── function_corrector.*       # 捕获、参数结构拆分、usage 传播
└── layout_resolver.*          # Type 的 storage layout 解析
src/generator/ast_to_cpp_source.*              # C++ 源码访问器
src/backends/vulkan_compute/ast_to_slang_source.* # Slang 源码访问器
src/backends/vulkan_compute/slang_compiler.*     # Function -> SPIR-V
```

Ocarina AST 是**带 Function 上下文的类型化 AST**：节点由 `Function` 拥有，
`Expression` 持有 `const Type*`，`Variable` 用 uid/Tag/Usage 标识，语句通过
`ScopeStmt` 形成作用域树。Helicon AST 则是按 stage 组织的字符串生成节点树。
扩展不是复制类名，而是把语义落到 Ocarina 的 `Type`/`Variable`/`Expression`/
`Statement`/`Function` 上。

各层职责：

| 层 | 主要入口 | 职责 |
| --- | --- | --- |
| C++ EDSL | `TypeAlias.h`、`VariateProxy.h`、`CustomLibrary.h` | 提供可重载运算符的 shader 值类型、资源代理和函数代理 |
| AST | `Ast::AST`、`Ast::Parser`、`Node.hpp` | 构造 `Value`/`Statement`/`Type` 节点，维护当前作用域和解析状态 |
| 生成器 | `Generator::SlangGenerator` | 把节点树展开为 Slang 源码，并产出 branch 模块和 type header |
| 管线入口 | `RasterizedPipelineObject`、`ComputePipelineObject` | 调用 shader lambda、处理 VS/FS 衔接、bindless 变体、auto-bind |
| 编译器 | `ShaderCodeCompiler`、`ShaderLanguageConverter` | Slang session、模块加载、compose/link、反射和结果缓存 |
| 构建期 | `HeliconShaderCompile.cmake`、`ShaderCompileScripts` | 扫描 `#include HLSL/GLSL/SLANG(...)`，生成 C++ shader 头 |

AST 的核心数据模型：

| 基类 | 代表节点 | 说明 |
| --- | --- | --- |
| `Ast::Node` | 所有节点 | `generate()` 先做 `collectVariateReference()`，再虚调用 `parse()` |
| `Ast::Type` | `BasicType`、`VecType`、`MatType`、`ArrayType`、`TextureType`、`SamplerType`、`AggregateType`、`StageType`、`PushConstantType` | 类型声明及其访问权限 |
| `Ast::Value` | `Variate`、`BasicValue`、`VecValue`、`MatValue`、`AggregateValue`、`BinaryOperator`、`UnaryOperator`、`MemberAccess`、`ElementValue`、`CallFunc` | 右值和变量引用 |
| `Ast::Statement` | `DefineLocalVariate`、`DefineInputVariate`、`DefineOutputVariate`、`DefineUniformVariate`、`DefineUniversalArray`、`DefineUniversalTexture`、`DefineAggregateType`、`Assign`、`IfStatement`、`ElseStatement`、`WhileStatement`、`UniversalStatement`、`StringStatement`、`FunctionDeclaration` | 语句和声明 |

`Ast::EmbeddedShaderStructure` 按用途保存当前 stage 的节点：

```cpp
struct EmbeddedShaderStructure
{
    ShaderStage stage;
    std::vector<std::shared_ptr<Statement>> globalStatements;   // UBO、纹理、数组、聚合类型
    std::deque<std::shared_ptr<Statement>> inputStatements;     // stage 输入
    std::vector<std::shared_ptr<Statement>> outputStatements;   // stage 输出
    std::vector<std::shared_ptr<Statement>> localStatements;    // 入口函数体
    std::vector<std::shared_ptr<Statement>> shaderOnlyStatements;
    std::unordered_set<SlangModule*> slangModuleSource;         // 需要 import 的模块
};
```

`Ast::Parser` 用 `thread_local` 保存当前解析器，`SlangGenerator` 的舞台状态、成员
累加器和缩进层级也是 `thread_local`。因此“执行 shader lambda、构造 AST、生成
Slang 源码”这一串操作在单线程内完成；跨线程并行构建 shader 时，各线程有独立的
AST 状态，不会互相覆盖。

### 1.基础光栅化功能逻辑

#### 1.1 从 lambda 到两段 stage

光栅管线的入口是 `RasterizedPipelineObject::compile()`：

```cpp
auto vs = [&](Aggregate<VsIn> in) {
    position() = mul(mvp, Float4(in->position, 1.0f));
    Aggregate<VsOut> out;
    out->normal = in->normal;
    return out;
};

auto fs = [&](Aggregate<VsOut> in) {
    output_color << Float4(normalize(in->normal), 1.0f);
};

horizon::RasterizerPipeline pipeline(vs, fs);
```

`RasterizedPipelineObject::compile()` 的实际流程：

1. 先 `Ast::Parser::setBindless(false)`，解析一次非 bindless 变体。
2. `parse()` 用 `static_assert` 检查 VS 返回类型和 FS 输入参数匹配。
3. 开始 vertex stage：`beginShaderParse(ShaderStage::Vertex)`。
4. `ParseHelper::createParamTuple()` 构造 VS 参数；参数代理的构造函数会登记
   `InputVariate`。随后 `callLambda()` 执行 VS lambda。
5. 若 VS 有返回值，取返回值对应的 `Variate` 类型，在 location 0 定义 VS 输出并
   赋值。`position()` 会额外生成 `SV_POSITION` 输出。
6. `beginShaderParse(ShaderStage::Fragment)` 先结束 VS 的解析，再进入 FS；此时
   上一 stage 的 `output` 已经收集完毕。
7. `ParseHelper::createParam()` 构造 FS 输入；执行 FS lambda。
8. FS 有返回值时，`handleFragmentOutput()` 把返回值展开成输出：
   - 普通 `VariateProxy<T>`：生成 location 0 的单个输出；
   - `Aggregate<T>`：按成员拆成 `SV_TARGET0..N`，`TextureType` 成员取其 texel
     类型作为输出类型；
   - 也可以不返回，直接在 FS 中用 `texture_output << value` 生成输出。
9. `endPipelineParse()` 结束当前 stage：没有剪枝分支时把全局声明（UBO、push
   constant、parameter block 等）拼到 stage 的 `output` 前面；有剪枝分支时拼到
   `typeHeader` 前面。随后重置 global statement 的访问权限，供下一次（bindless）
   解析复用。
10. 如果 `CompilerOption::enableBindless` 为真，设置 bindless 标志后重新
    执行 2-9，得到 bindless 版本的源码、branch 和 type header。
11. 用 `ShaderCodeCompiler` 分别编译 VS/FS；最后 `updateAutoBind()` 从反射表
    里收集自动绑定条目。

`ParseOutput` 是 stage 到编译器的交接结构：

```cpp
struct ParseOutput
{
    std::string output;                       // 当前 stage 的 Slang 源码
    std::unordered_set<SlangModule*> sourceModule; // 该 stage 用到的预编译模块
    ShaderStage stage;
    std::vector<BranchOutput> branches;       // $IF 剪枝分支
    std::string typeHeader;                   // 分支共享的类型/全局声明
};
```

#### 1.2 代理、作用域与命名

`VariateProxy<T>` 的构造函数根据 `ParseHelper` 的当前状态决定节点类型：

| 构造位置 | 生成的节点 |
| --- | --- |
| shader lambda 的参数初始化阶段 | `InputVariate`，location 由 `getCurrentInputIndex()` 分配 |
| shader lambda 内部 | `LocalVariate`，语句插入当前作用域 |
| lambda 外部的全局对象 | `UniformVariate`，作为 UBO 成员或 push constant 成员 |
| `Aggregate<T>` 的成员 | 对聚合节点做 `MemberAccess` |
| `ArrayProxy<T>` | `UniversalArray` / `ArrayType` |
| `Texture2DProxy<T>` | `UniversalTexture` / `TextureType` |
| `FunctionProxy<...>` | `FunctionDeclaration`，调用时生成 `CallFunc` |

`Ast::AST` 用单调递增的索引生成名字：

- 局部/输入/输出变量：`var_N`；
- 全局资源：`global_var_N`；
- 聚合类型：`aggregate_type_N`。

这些名字是生成 Slang 源码和反射查找的键，不要手工依赖或硬编码。

访问权限由 `AccessPermissions` 位标志记录。`Ast::AST::assign()` 给左值标
`WriteOnly`、右值标 `ReadOnly`；运算符会给操作数标 `ReadOnly`。生成器据此：

- 没有被任何 shader 引用的全局资源（`permissions == None`）不生成；
- 需要写访问的纹理/数组生成 `RWTexture...` / `RWStructuredBuffer<...>`；
- `$IF` 剪枝分支函数的参数根据读写情况生成 `in` / `out` / `inout`。

#### 1.3 Slang 源码的拼装顺序

`SlangGenerator::getShaderOutput()` 的输出顺序是固定的：

1. 每个 `slangModuleSource` 的 `import <module>;`；
2. bindless 模式下先插入三张动态资源表和 `getDescriptorFromHandle()`；
3. `shaderOnlyStatements`（通过 `addShaderOnlyStatement` 登记、只出现在 shader
   中的语句）；
4. 当前 stage 的 `input` / `output` 结构体；如果存在分支，这部分会移入
   `type_header` 模块；
5. 当前分支栈的外部函数声明和 `import type_header;`；
6. 入口函数 `main`，带 `[shader("vertex"/"fragment"/"compute")]`；compute 还会
   带 `[numthreads(x,y,z)]`；
7. `main` 内部先声明 `output`（存在输出时），再按顺序展开 `localStatements`，
   最后 `return output;`（如果有输出）。

结构体的语义由生成器决定：

- `DefineInputVariate`：`type name : LOCATION<location>;`
- `DefineOutputVariate`：fragment 为 `type name : SV_TARGET<location>;`，其他
  stage 为 `type name : LOCATION<location>;`
- `DefineSystemSemanticVariate`：`position()`、`isFrontFace()`、
  `dispatchThreadID()` 等内建变量使用 `SV_POSITION`、`SV_IsFrontFace`、
  `SV_DispatchThreadID`、`SV_DrawIndex`、`SV_InstanceID`、`SV_VertexID`。

示意（实际变量名由生成器分配）：

```slang
struct vertex_input {
    float3 var_0 : LOCATION0;
};

struct vertex_output {
    float4 position_output : SV_POSITION;
    float3 var_2 : LOCATION0;
};

[shader("vertex")]
vertex_output main(vertex_input input) {
    ...
    return output;
}
```

#### 1.4 全局资源容器

`SlangGenerator::getGlobalOutput()` 负责生成全局资源容器：

| 资源 | 非 bindless | bindless |
| --- | --- | --- |
| 普通 `UniformVariate` | `ConstantBuffer<global_ubo_struct> global_ubo` 的成员 | UBO 仍在 `set=3`，通过 `[[vk::binding(0, 3)]]` 绑定 |
| `pushConstant` uniform | `global_push_constant_struct` 的成员 | push constant block；资源 handle 也放在这里 |
| `Texture2D` / `TextureCube` / `Array<T>` | `ParameterBlock<parameter_block_struct>` 的成员 | push constant 中的 handle，类型生成 `.Handle` |
| `Sampler` | UBO 成员 | push constant 中的 handle |

生成器在 bindless 模式下会生成三张表，分别对应 combined sampler、buffer 和
texture 句柄：

```slang
[vk::binding(0, 0)]
__DynamicResource<__DynamicResourceKind.General> combinedTextureSamplerHandles[];

[vk::binding(0, 1)]
__DynamicResource<__DynamicResourceKind.General> bufferHandles[];

[vk::binding(0, 2)]
__DynamicResource<__DynamicResourceKind.General> textureHandles[];
```

`getDescriptorFromHandle<T>()` 负责把 `DescriptorHandle<T>` 转成实际资源。该函数
只在 SPIR-V/GLSL target 下走自定义分支，其他 target 回退到 Slang 默认实现。

#### 1.5 bindless 与非 bindless 双变体

`CompilerOption::enableBindless` 默认是 `true`；为 `true` 时管线编译会解析两次：

- 第一次用非 bindless 模式，生成 `ParameterBlock` 版本；
- 第二次用 bindless 模式，生成 handle 表和 push constant 版本。

`ShaderCodeCompiler` 内部维护两套 `session` 和模块：

```cpp
Slang::ComPtr<slang::ISession> session;          // 非 bindless
Slang::ComPtr<slang::ISession> bindlessSession;  // bindless
SlangStageModules stageModules;
SlangStageModules bindlessStageModules;
```

`getShaderCode(language, bindless, conditionInfo)` 的 `bindless` 参数决定从哪套
缓存取结果。适配自定义 Helicon 管线时，需要保证创建和查询使用的 `enableBindless` 一致。

#### 1.6 反射与 auto-bind

编译完成后，`RasterizedPipelineObject::updateAutoBind()` / 
`ComputePipelineObject::updateAutoBind()` 会遍历全局 statement，把代理侧记录的
资源指针和反射表对应起来：

- `UniformVariate::boundValueRef`：指向代理保存的宿主值；
- `UniversalTexture::boundResourceRef`：指向代理保存的 `HardwareImage*`；
- `UniversalArray::boundResourceRef`：指向代理保存的 `HardwareBuffer*`；
- `dirtyVersion`：宿主值变化时递增，供绑定时判断是否需要重写；
- `renderTargetLocation >= 0`：该纹理在 FS 中被 `<<` 当作渲染目标使用。

生成 `AutoBindEntry` 时会读取反射得到的 `byteOffset`、`typeSize`、`bindType`、
`location`。bindless 下，push constant 中的 handle 在反射里会报成
`pushConstantMembers`，因此需要按资源种类重映射：

| 反射结果 | 资源 | 重映射后的 bind type |
| --- | --- | --- |
| `pushConstantMembers` | 采样纹理 | `sampledImages` |
| `pushConstantMembers` | 读写纹理 | `storageTexture` |
| `pushConstantMembers` | `Array<T>` | `storageBuffer` |
| `stageOutputs` | `texture << value` 的输出 | `stageOutputs`，location 为 `SV_TARGET` 序号 |

`ShaderCodeModule::ShaderResources` 保存反射结果，主要字段包括
`pushConstantSize/Name`、`uniformBufferSize/Name`、`bindInfoPool` 和
`entryPointInfoPool`。CPU 侧通过 `findShaderBindInfo(name)` 按变量名查找。

### 2.Shader Including 功能逻辑

Helicon 里有两条不同的“包含”路径，理解 AST 行为时要区分：

1. **C++ 源码包含**：`#include HLSL/GLSL/SLANG(path)`，由 CMake 调
   `ShaderCompileScripts` 预编译成 C++ 头；
2. **Slang 模块导入**：生成的 Slang 源码里的 `import <module>;`，由
   `FunctionProxy` 触发的 `slangModuleSource` 和分支 `type_header` 使用。

#### 2.1 `#include HLSL/GLSL/SLANG` 宏

宏定义在 `Codegen/ControlFlows.h`：

```cpp
#define HELICON_STRINGIZE_(X) #X
#define HLSL(path) HELICON_STRINGIZE_(path.hpp)
#define GLSL(path) HELICON_STRINGIZE_(path.hpp)
#define SLANG(path) HELICON_STRINGIZE_(path.hpp)
```

因此：

```cpp
#include GLSL(shaders/my_vert.glsl)
```

预处理后等价于：

```cpp
#include "shaders/my_vert.glsl.hpp"
```

生成的 `.hpp` 位于构建目录的 `helicon_generated/<target>/` 下，并保持 shader
的相对目录结构。它由 `helicon_compile_shaders(target)` 在 CMake 配置阶段扫描
目标源文件得到：

1. 只扫描 `.cpp/.c/.h/.hpp/.cxx/.cc`；
2. 用正则匹配 `#include HLSL(...)`、`#include GLSL(...)`、`#include SLANG(...)`；
3. 为每个 shader 生成一条 `add_custom_command`，调用
   `ShaderCompileScripts -l <lang> -s <source> -o <outdir> -output-file-extension <ext>.hpp`；
4. 把含 shader include 的源文件加入 `CMAKE_CONFIGURE_DEPENDS`，改名/新增
   shader 时会重新配置；
5. 把生成的 `.hpp` 设为 `GENERATED`，挂到目标的 `OBJECT_DEPENDS` 和
   `target_sources` 上，并把输出根目录以 `BEFORE PUBLIC` 加进 include path。

当前仓库里只有 `HorizonExamples` 调用了 `helicon_compile_shaders`；新 target
需要自己显式调用，不能只在源码里写 `#include GLSL(...)`。

`ShaderCompileScripts`（`tools/main.cpp`）的离线流程：

1. 读取 `.glsl/.hlsl/.slang`，若没有显式 `-t`，按扩展名、文件名关键字、源码
   特征推断 stage；
2. 若源码没有入口点，按 module-only 处理：仍然生成模块和声明级 C++ 包装，但
   不生成 `ShaderResources` 反射；
3. `ShaderLanguageConverter::slangModuleCompiler()` 用 `SLANG_CPP_SOURCE` target
   加载源码，打开 `NoMangle`、`IncompleteLibrary`，按 `GLSL/HLSL/Slang` 设置
   `Language` option，并把源码序列化成 `SlangModule`；
4. 无论是否有入口点，都会用 `slangModuleReflection()` 遍历声明生成 C++ 包装；
   有入口点时额外用 `slangModuleReflectShaderResource()` 得到 `ShaderResources`。
   生成内容包括：
   - `static inline ::EmbeddedShader::SlangModule slang_module_<name>`；
   - `static inline auto& slangModule` 别名；
   - 非入口函数的 `FunctionProxy<...>`；
   - `BindingKey`、`ResourceBindings<P>`、`OutputBindings<P>`、`Bindings<P>`；
5. 输出 C++ 头，外层结构名由 `sanitizePath()` / `sanitizeFilename()` 清理。

生成的头通常这样使用：

```cpp
#include GLSL(shaders/my_helpers.glsl)

auto fs = [&](Aggregate<Varyings> in) {
    // my_helpers.glsl 中非入口函数被包装成 FunctionProxy
    return tonemap(in->color);
};
```

注意：带 `main` 的入口函数不会被包装成 `FunctionProxy`；这类 shader 应该用
`RasterizerPipeline<VertShader, FragShader>` / `ComputePipeline<Shader>` 通过
生成头里的 `slangModule` 来使用，而不是在 EDSL lambda 里直接调用 `main`。

#### 2.2 生成头里的模块和绑定代理

生成的 shader 头同时承担两件事：

- 把 `SlangModule`（模块名、路径、序列化 IR）暴露给管线；
- 把反射得到的绑定信息生成为 `BindingKey` 和 `Bindings<P>`，供
  `pipeline.<UniformBlock>.<member> = value` 之类的直接成员访问使用。

旧版 `Horizon_old.h` 的 `RasterizerPipeline<VS, FS>` 会静态检查：

```cpp
static_assert(VS::pushConstantBlockSize == FS::pushConstantBlockSize);
static_assert(VS::uniformBufferBlockSize == FS::uniformBufferBlockSize);
```

当前 `horizon.h` 的 typed pipeline 不再重复这两条 `static_assert`，但同样要求
VS/FS 的反射契约一致；如果只改了一侧 shader，需要重新生成另一侧或确认布局仍然
一致。

#### 2.3 Slang 模块导入

`FunctionProxy` 在构造时持有一个可选的 `SlangModule* sourceModule`。调用函数时：

```cpp
if (sourceModule)
    Ast::AST::getEmbeddedShaderStructure().slangModuleSource.insert(sourceModule);
```

`SlangGenerator::getShaderOutput()` 会为每个模块生成：

```slang
import <module.name>;
```

`ParseOutput::sourceModule` 把这个集合传给管线入口，管线再把它追加到
`CompilerOption::slangModules`。`ShaderCodeCompiler::compile()` 在加载 core
源码之前，先用 `ShaderLanguageConverter::loadModule(session, *dep)` 从序列化 IR
加载依赖模块；bindless 变体有独立的依赖列表。

分支的 `type_header` 也走同一条 import 路径：分支模块和 core 都会
`import type_header;`，从而共享全局声明、输入/输出结构体。

#### 2.4 约束与注意事项

- **模块名必须唯一**：`import` 按 `SlangModule::name` 解析；不同 shader 的模块
  重名会互相覆盖或产生歧义。
- **源文件变更的依赖跟踪有限**：CMake 的 `DEPENDS` 只包含顶层 shader 文件；
  shader 内部再 `#include` 的文件变化不会自动触发重新生成，需要清理构建产物
  或重新配置。Slang 自身会相对 `modulePath` 解析 shader 内部的 include。
- **模块必须先注册**：只有调用过带 `sourceModule` 的 `FunctionProxy`，或显式把
  模块放进 `CompilerOption::slangModules`，生成代码里才会出现 `import`。
- **bindless 双变体**：管线会重新解析并重新收集 `sourceModule`；使用预编译模块
  的管线要分别验证 bindless 和非 bindless 的依赖是否完整。

### 3.Branch Pruning 功能逻辑

#### 3.1 为什么需要 Branch Pruning

普通的 C++ `if` 只会在生成 AST 时选择一个分支：

```cpp
if (cpu_red)
    color = Float3(1, 0, 0);
else
    color = Float3(0, 1, 0);
```

切换 `cpu_red` 后，需要重新构造 AST、重新编译 Slang、重新创建 Vulkan 管线。
`$IF` 的目标是：首次编译时把 true/false 两个分支都生成为独立 Slang 模块，运行
时只做 compose + link 和管线重建，不重新解析 AST、不重新加载模块。

`examples/example_edsl_compile_comparison/example_edsl_compile_comparison.cpp`
就是这两种方式的对比基准。

#### 3.2 语法

```cpp
bool use_pbr = true;

auto fs = [&](Aggregate<Varyings> in) {
    Float4 color;
    $IF (use_pbr)
    {
        color = pbr(in);
    }
    $ELSE
    {
        color = lambert(in);
    };
    output_color << color;
};
```

`$IF` 的条件可以是：

- `bool`：走 branch pruning，生成 true/false 两个模块；
- `VariateProxy<bool>`：走 `GPU_IF_BRANCH` 的代理重载，生成一个带设备端条件的
  `IfStatement`，不做剪枝。

`$ELSE` 可选；没有 `$ELSE` 时，生成器会补一个空的 false 分支。

#### 3.3 宏展开与重复扫描

`ControlFlows.h` 用一组 `for` + `while` 宏反复扫描同一段 C++ 代码，直到分支
计数稳定：

```cpp
#define GPU_PUSH_VARIATE(define, name) \
    for (define; name.index < 1; ++name.index)

#define GPU_IF_CONDITION \
    TheIfElseStatementMustBeGuidedByIf.currentIndex++ == \
            TheIfElseStatementMustBeGuidedByIf.maxCount \
        ? (++TheIfElseStatementMustBeGuidedByIf.maxCount, true) \
        : (++TheIfElseStatementMustBeGuidedByIf.lastMaxIndex, false)

#define $IF(condition) \
    GPU_PUSH_VARIATE(GPU_IF TheIfElseStatementMustBeGuidedByIf, \
                     TheIfElseStatementMustBeGuidedByIf) \
    while (TheIfElseStatementMustBeGuidedByIf.currentIndex = 0, \
           TheIfElseStatementMustBeGuidedByIf.lastMaxIndex < \
               TheIfElseStatementMustBeGuidedByIf.maxCount) \
    if (GPU_IF_CONDITION) \
    GPU_PUSH_VARIATE(GPU_IF_BRANCH gpuIfBranch{...}, gpuIfBranch) // 条件/lambda 实参从略
```

执行过程：

1. 第一次 `GPU_IF_CONDITION` 命中，构造 `GPU_IF_BRANCH`，调用
   `Ast::AST::beginIf(condition, detector)`，记录 true body 的 AST；
2. 后续扫描中外层 `if` 为假，`$ELSE` 里的
   `GPU_PUSH_VARIATE_WITH_CONDITION` 会再求值一次 `GPU_IF_CONDITION`，命中时
   构造 `GPU_ELSE_BRANCH`，调用 `Ast::AST::beginElse()`，记录 else body；
3. `maxCount` / `lastMaxIndex` 随扫描递增，直到 `lastMaxIndex == maxCount`，
   while 退出。

由此带来的硬性约束：

- 宏会反复扫描 `$IF`/`$ELSE` 区域：true body 和 else body 各执行一次，但条件
  表达式会被多次求值；分支体内的宿主侧副作用会在所属的那一次执行，不要依赖
  扫描次数；
- `condition` 必须稳定、无副作用，否则不同扫描可能得到不同结果；
- `$IF` 只能出现在 shader lambda 内部，因为它依赖当前 `localStatementStack`。

#### 3.4 AST 与生成器如何处理剪枝

`Ast::AST::beginIf(condition, detector)` 在带 detector 时分配一个 branch index：

```cpp
ifStatement->index = Parser::getCurrentBranchIndex();
ifStatement->condition = std::move(condition);
ifStatement->conditionDetector = std::move(conditionDetector);
```

`SlangGenerator::getParseOutput(IfStatement*)` 遇到带 detector 的 `IfStatement`：

1. `setEnabledTypeHeader(true)`；
2. 在 `Parser::getBranchOutputs()` 里追加一个 `BranchOutput`；
3. 把当前 branch index 压入 `branchReferences`；
4. 用 `getBranchInfo(body)` 生成 body：
   - `beginCollectExternBranchVariate()` 开始收集 body 引用的变量；
   - `Variate::collectVariateReference()` 把变量加入当前集合；
   - `DefineLocalVariate` 会把局部变量登记下来，最后被过滤掉；
   - `getCurrentExternBranchVariateCollection()` 通过 `getRootVariate()` 把
     输入/输出/uniform/texture 映射到根容器；
5. 生成函数签名：按 `AccessPermissions` 把参数标成 `in` / `out` / `inout`；
6. 生成 true branch 文本：

   ```slang
   // core 中的声明（declareBranch）
   import type_header;
   extern void branch_0(in float4 var_1, out float4 var_2);

   // branch_0_true 模块（trueBranch）
   import type_header;
   export void branch_0(in float4 var_1, out float4 var_2)
   {
       ...
   }
   ```

7. 没有 `$ELSE` 时，生成一个空的 false branch；
8. 在 core 代码中把原来的 `if` 替换成 `branch_0(...)` 调用。

有 `$ELSE` 时，`ElseStatement` 会把 true/false 两边的变量引用合并，生成完整的
false branch 函数。

当当前 stage 存在剪枝分支时：

- `getShaderOutput()` 不再把 input/output 结构体放进 core，而是追加到
  `Parser::getTypeHeader()`；
- `endPipelineParse()` 把 `getGlobalOutput()` 的结果拼到 `typeHeader` 前面；
- 分支模块和 core 都 `import type_header;`，因此都能看到 UBO、push constant、
  input/output 结构体。

`BranchOutput` 的结构：

```cpp
struct BranchOutput
{
    std::function<bool()> conditionDetector;
    std::string trueBranch;
    std::string falseBranch;
    std::string declareBranch;
};
```

`declareBranch` 存放 core 里的 `extern` 函数声明；`trueBranch`/`falseBranch` 是
可独立加载的 Slang 模块源码。

#### 3.5 编译器侧

管线入口把 `ParseOutput::branches` 和 `typeHeader` 放进
`CompilerOption`：

```cpp
compilerOption.branches = outputs[0].branches;
compilerOption.typeHeader = outputs[0].typeHeader;
```

`ShaderCodeCompiler::compile()`：

1. 如果有 `option.branches`，先加载 `type_header` 模块；
2. 按 branch index 加载 `branch_<i>_true` / `branch_<i>_false`；
3. 把每个 `branch.conditionDetector` 存到 `conditions[i]`；
4. 加载 core 并找到入口点 `main`；
5. 调用 `combineLocked()` 做首次组合。

`combineLocked()` 的组合顺序：

```text
core + entryPoint + type_header + dependency modules + 每个分支选中的 true/false module
```

然后 `createCompositeComponentType()` + `link()`，按 target 输出代码和反射。
`conditionInfo` 是 `std::vector<bool>`，第 `i` 位为 true 时选 `branch_i_true`，
否则选 `branch_i_false`。

缓存键包含 language、是否反射、是否 bindless，以及每个分支的选择位：

```text
<Language>[_Reflection][_Bindless]
    [_Branch_0_True|_Branch_0_False]
    ...
    _<source_location>
```

`getCurrentConditionInfo()` 会调用每个 `conditionDetector` 得到当前组合；
`getShaderCode(language, bindless, conditionInfo)`：

- `conditionInfo` 为空且有分支时，使用当前探测结果；
- 命中缓存直接返回；
- 未命中时在独占锁下调用 `combineLocked()`，只做 compose/link，不重新创建
  session、不重新从源码/IR 加载模块。

#### 3.6 运行时切换

光栅管线的 `RasterizerPipelineBase` 在 `clear_records()` 和
`record_indirect()` 开头调用 `sync_shader_conditions()`：

1. 从当前 pipeline 读取 shader 条件；
2. 与上次的 `vert_condition_info_` / `frag_condition_info_` 比较；
3. 变化时对 VS/FS 调用 `getShaderCode(..., conditionInfo)`，更新
   `autoBindEntries`，并用 `getCombinedKey()` 在 `pipeline_pool_` 中查找或创建
   新的 Vulkan 管线；
4. 条件不变时只做一次轻量比较，不重新链接。

计算管线对应 `ComputePipelineBase::sync_shader_conditions()`。`getCombinedKey()`
把条件位编码成字符串，保证同一组条件复用同一个 Vulkan pipeline。

#### 3.7 限制与排查

- **分支数量**：N 个 `$IF` 最多有 `2^N` 种条件组合；首次编译只加载模块，实际
  组合按需 link。bindless 与非 bindless 各有一套模块，内存和编译时间都会翻倍。
- **条件生命周期**：`conditionDetector` 是 `std::function<bool()>`，通常由
  `$IF` 的 `[&]` 捕获宿主变量。宿主变量必须活得比 `ShaderCodeCompiler` 和
  使用该条件的 pipeline 更久。
- **条件数量必须匹配**：`combineLocked()` 要求 `conditionInfo.size()` 等于
  `branches.size()`，否则抛异常。
- **顺序必须稳定**：branch index 按 AST 解析顺序分配；bindless 重新解析时顺序
  必须一致，否则会链接到错误分支。
- **条件向量是单份的**：`ShaderCodeCompiler::conditions` 会被最后一次 `compile()`
  重写；bindless 与非 bindless 变体应使用相同的条件语义，否则查询时可能按另一
  条变体的探测结果取代码。
- **两条路径都要写输出**：没有 `$ELSE` 时 false branch 是空函数；如果输出只在
  true branch 赋值，false 路径会留下未定义值。使用 `$IF` 时应显式初始化。
- **设备端条件不要用 `$IF(bool)` 代替**：逐像素/逐线程变化的布尔值应该用
  `$IF(VariateProxy<bool>)`，否则会把运行时数据当成宿主分支条件。
- **分支内部定义的局部变量会被过滤**：它们不会成为分支函数的外部参数；如果
  分支外部需要某个值，必须在 `$IF` 之前定义并通过外部变量传递。

### 4.Slang Compiler 编译流程逻辑

#### 4.1 输入与选项

`ShaderCodeCompiler` 的输入是：

- 生成好的 Slang 源码（一个 stage 一份）；
- `ShaderStage`：vertex / fragment / compute；
- `CompilerOption`；
- `std::source_location`：用于缓存键和 hardcode 名字。

`CompilerOption` 的主要字段：

```cpp
struct CompilerOption
{
    bool compileGLSL = false;
    bool compileHLSL = true;      // Apple 平台默认 false
    bool compileDXIL = false;
    bool compileDXBC = false;
    bool compileSpirV = true;
    bool enableBindless = true;
    bool enableMatrixColumnMajor = false;
    std::vector<SlangModule*> slangModules;
    std::vector<Ast::BranchOutput> branches;
    std::string typeHeader;
};
```

`horizon.h` 的 EDSL 管线会把 `enableMatrixColumnMajor` 设为 `true`，因为
`ktm::mat` 按列主序使用；如果自定义 `CompilerOption` 创建 EDSL 管线，必须保持
一致；静态 GLSL/HLSL 生成的 shader 继续使用 Slang 默认的行主序。

#### 4.2 `compile()` 的目标选择

`ShaderCodeCompiler::compile()` 先根据选项生成 target 列表：

| 选项 | Slang target | profile |
| --- | --- | --- |
| `compileGLSL` | `SLANG_GLSL` | `spirv_1_6` |
| `compileDXIL` | `SLANG_DXIL` | `sm_6_6` |
| `compileHLSL` | `SLANG_HLSL` | `sm_6_6` |
| `compileSpirV` | `SLANG_SPIRV` | `spirv_1_6` |
| `compileDXBC` | `SLANG_DXBC` | `sm_5_0`，bindless 下跳过 |

同时设置：

```cpp
slang::CompilerOptionName::EmitSpirvDirectly = 1
```

然后根据 bindless 标志创建/复用 `session` 或 `bindlessSession`。每个 session 的
`defaultMatrixLayoutMode` 由 `enableMatrixColumnMajor` 决定。

`HORIZON_SLANG_DUMP=<dir>` 环境变量会把每个 stage 的生成源码写到：

```text
<dir>/<seq>_stage<stage>[_bindless].slang
```

例如在 PowerShell 中：

```powershell
$env:HORIZON_SLANG_DUMP = "$PWD/build/slang-dump"
```

这是排查“编译通过但渲染不对”的第一手材料。

#### 4.3 模块加载与组合

`compile()` 的加载顺序：

1. 依赖模块：`option.slangModules` 里的每个 `SlangModule` 用
   `loadModuleFromIRBlob()` 加载，存入 `dependencyModules` 或
   `bindlessDependencyModules`；
2. 有分支时：
   - `type_header`：`loadModuleFromSourceString(session, "type_header", option.typeHeader)`；
   - `branch_<i>_true` / `branch_<i>_false`：分别加载 true/false 源码；
3. core：`loadModuleFromSourceString(session, "core_source", shaderCode)`；
4. 入口点：`findEntryPoint(core, "main")`。

在 Debug/RelWithDebInfo 下，`storeModule` 还会把模块序列化后写入
`src/Helicon/Compiler/HardcodeShaders/`；Release 下这一步被
`CABBAGE_ENGINE_DEBUG` 条件编译掉。

#### 4.4 compose、link 与代码产出

`combineLocked()` 的核心步骤：

```text
createCompositeComponentType(core, entryPoint, typeHeader?, deps..., branch modules...)
        ↓
link()
        ↓
for each target:
    getEntryPointCode(0, targetIndex)
    getLayout(targetIndex) -> ShaderResources
    binary 或 string 写入 SlangCompileResult
```

`ShaderLanguageConverter::fillCompileResult()`：

- SPIR-V / DXBC / DXIL 作为二进制 `std::vector<uint32_t>`；
- GLSL / HLSL 作为字符串；
- 每个 target 都生成一份 `ShaderResources` 反射（如果 `isNeedReflection`）；
- SPIR-V 会额外执行 `restoreComputeLocalSize()`：GLSL -> 序列化 IR -> SPIR-V
  可能把 compute 的 `LocalSize` 写成 `(1,1,1)`，而反射仍报告原始 workgroup
  size；这里从 SPIR-V 的 `OpExecutionMode LocalSize` 修正回反射值，避免
  `dispatch_extent()` 只覆盖部分像素。

`getFinalCode()` 对入口点程序调用 `getEntryPointCode(0, targetIndex, ...)`；
对库模式调用 `getTargetCode(...)`。

#### 4.5 反射

`ShaderLanguageConverter::slangReflection(programLayout)` 收集：

- 顶层参数：UBO、parameter block、全局纹理/采样器/buffer；
- entry point 的输入参数和返回值；
- `ShaderCursor::collectBindings()` 递归展开结构体、数组、嵌套资源；
- `set` / `binding` / `byteOffset` / `typeSize` / `elementCount` / `typeName`；
- `pushConstantSize/Name`、`uniformBufferSize/Name`；
- `entryPointInfoPool`，compute 还包含 `numthreads`。

关键规则：

- `ParameterBlock` 的 set 来自 `SubElementRegisterSpace`；
- 其余顶层资源的 set 来自 `getBindingSpace(DescriptorTableSlot)`；
- bindless 下 UBO 固定 `set=3`；三张 handle 表分别是 `set=0/1/2`；
- 真 push constant 走 `PushConstantBuffer`，不占 descriptor set；
- 反射得到的成员名是点分全名（如 `global_ubo.viewMatrix`）；SPIR-V 路径会
  `stripReflectionMemberPrefixes()` 裁成短名，供 CPU 侧按变量名查找。

#### 4.6 结果缓存与 hardcode

`getShaderCode()` 的读取顺序：

1. 如果启用了 hardcode（`HELICON_HAS_HARDCODE_SHADERS`），先按 stage + item
   从 `HardcodeShaders` 静态表读取；缺失或过期时捕获异常并回退；
2. 查 `compiledOutputs_`；
3. 没有条件且缓存缺失时抛 `runtime_error`；
4. 有条件时在独占锁下 `combineLocked()`，再重新查缓存。

hardcode 的启用条件：

- `CABBAGE_ENGINE_DEBUG` 只负责“写”；
- “读”需要 `HELICON_ENABLE_HARDCODE_SHADERS` 宏，并且
  `<Compiler/HardcodeShaders/HardcodeShaders.h>` 存在；
- `Debug` / `RelWithDebInfo` 会生成 `HardcodeShaders<Stage>.cpp` 和
  `HardcodeShaders.h/.cpp` 到源码树下的 `src/Helicon/Compiler/HardcodeShaders/`；
- `Release` 打开 `HORIZON_ENABLE_HARDCODE_SHADERS=ON` 后使用这些表。

`ShaderHardcodeManager::clearOldHardcode()` 在进程内第一次写入时清理旧的
`HardcodeShaders*` 文件；生成文件属于构建产物，不要手工编辑。运行时分支切换
只更新 `compiledOutputs_`，不会往 hardcode 源码追加内容。切换 shader 或重新生成
后如果行为异常，优先怀疑 hardcode 过期，重新用 Debug/RelWithDebInfo 构建一次再
验证。

#### 4.7 线程与异常

- `EmbeddedShader::threadMutex` 是全局 `std::shared_mutex`：`getShaderCode()`
  命中缓存时用共享锁，未命中时用独占锁做 link 和写缓存。
- `Parser`、`SlangGenerator` 的状态是 `thread_local`，所以 AST 构造和代码生成
  必须发生在同一线程；跨线程并行创建不同 pipeline 时 AST 状态不会互相覆盖，
  但共享的编译结果缓存和 Debug 下的 hardcode 写入仍应串行化，不要把同一个
  `ShaderCodeCompiler` 的首次编译和查询并发执行。
- Slang 诊断由 `reportSlangDiagnostics()` 输出到 stderr；
  `ShaderLanguageConverter` 的诊断输出到 stdout。
- 源码加载、compose、link、取代码失败通常抛 `std::runtime_error`；IR 模块加载
  失败目前只打印诊断并返回空指针，最终会在 compose/link 阶段暴露。调试时不要
  吞掉这些异常，否则只会看到后续的空 shader。

### 5.迁移步骤：扩展 Ocarina AST

这里只扩展 AST 层。Helicon 的 C++ EDSL 前端（`VariateProxy`、`ParseHelper`、
`TypeAlias`、`BuiltinVariate`、`CustomLibrary`、`$IF/$ELSE/$WHILE` 宏、
`RasterizedPipelineObject`、`ComputePipelineObject`）不迁移；它们只是
Helicon AST 的构造者和使用示例。扩展内容是：节点语义、类型/变量/使用标记、
分支剪枝所需的 AST 元数据和分析、以及 AST 到 Slang 的生成契约。

核心建议：

- **只做增量扩展**：不替换 Ocarina 的现有节点、枚举和编译路径；用旁表、子类或
  新 API 接入新能力。
- **先 AST、后消费端**：先把 branch 元数据和外部变量分析做对，再扩展
  `AstToSlangSource`/`SlangShaderCompiler`；每一步都保留旧路径。
- **差异优先于照搬**：Ocarina 的类型、Usage、Function 上下文、kernel ABI 与
  Helicon 不同，按 Ocarina 语义映射，不复制 Helicon 类/字符串生成器。
- **资源/stage 不追求 1:1**：compute/RT 目标下不要搬 VS/FS、UBO、Sampler、
  Cube；需要时另开设计。
- **旧行为逐字不变**：普通 `IfStmt`、现有 hash/源码/SPIR-V/cache key 和
  `src/ast` 的既有测试必须保持通过。

#### 5.1 扩展边界

| 能力 | 扩展方式 | 说明 |
| --- | --- | --- |
| Helicon `Node`/`Value`/`Statement`/`Type` 类 | 不复制类层次，映射为 Ocarina 节点 | 新语义落到 `Expression`/`Statement`/`Type`，不替换现有节点 |
| 类型系统 | 映射，不改现有 `Type` 语义 | Helicon 的类型名字符串改为 Ocarina `Type`/`TypeDesc`/`LayoutResolver` |
| 访问权限 | 映射到 Ocarina `Usage` | 不改变现有 usage 的标记时机和语义 |
| 表达式/语句/作用域 | 映射到现有 Ocarina 节点 | 优先复用，不新增平行节点树 |
| Branch pruning | 新增可选能力 | 通过扩展点接入；普通 `IfStmt` 行为不变 |
| `type_header` / module import | 新增可选能力 | 只服务分支/模块路径；不影响现有单模块编译 |
| AST 消费端（codegen/compiler） | 新增分支/新路径 | 现有 `AstToCppSource`/`SlangShaderCompiler` 路径保持原样 |
| Helicon C++ EDSL 前端 | 不迁移 | 包括代理、宏、`ParseHelper`、`$IF` 前端展开 |
| `ShaderCodeModule`/反射/hardcode | 不迁移，除非外部 API 需要 | 保持 Ocarina `Type`/`VkShaderParameterLayout` 单一来源 |
| VS/FS 光栅语义 | 视目标另开设计 | Ocarina AST 目前是 compute/raytracing；不要塞进现有 `Function` |

**兼容性原则：扩展而不是改造**

1. **默认路径逐字不变**：没有 branch 元数据的 `IfStmt` 仍走原来的
   `AstToCppSource`/`AstToSlangSource` 分支；现有 AST 的 hash、函数名、源码和
   SPIR-V 不应变化。
2. **不改现有类布局**：`ocarina-ast` 是 `SHARED` 库
   （`WINDOWS_EXPORT_ALL_SYMBOLS`）。给 `IfStmt`/`Function`/`Variable` 直接加
   成员会改变类布局和 ABI。优先用旁表（key 为 `const IfStmt*`）、子类或新的
   可选扩展对象，而不是改旧类字段。
3. **新能力 opt-in**：普通 `if` 不创建 branch 对象、不增加分析开销；只有显式
   标记的分支才进入 branch pass/codegen/compiler。
4. **不改现有枚举语义**：不要复用 `Type::Tag`/`Variable::Tag`/`Statement::Tag`
   的已有值来表示新语义；需要新类型/Tag 时按追加方式设计，并检查所有 switch、
   hash、序列化和 ABI。
5. **不改变现有编译器缓存行为**：现有 shader 的 source/cache key、磁盘缓存
   路径和 `slang_source_cache_version` 不应因新增分支能力而变化；新分支路径
   使用独立 key/版本。
6. **测试优先**：先补“旧行为不变”的回归测试，再加新功能；`src/ast` 原有
   `FunctionCorrector`、codegen parity、compute/RT 测试必须保持通过。
7. **文件级隔离**：优先新增 `branch_registry.*`、`branch_analysis.*`、
   `branch_emitter.*` 等文件；对 `expression.h`/`statement.h`/`function.h` 只做
   追加式 hook，避免大范围改现有头文件。

“只迁移 AST”不等于只改 `src/ast`。分支剪枝的 AST 元数据必须被分析 pass、
`AstToSlangSource` 和 `SlangShaderCompiler` 消费；否则只是无效字段。但这些
消费端都应通过**新增分支**接入，而不是改写原有路径。

本次不要求修改 `src/dsl` 的 `$if`/`IfStmtBuilder` 前端。扩展点先由 AST 单测
和后续调用方接入；等 AST/代码生成稳定后，再由前端决定如何暴露给用户。

#### 5.2 两个 AST 的模型差异

| 维度 | Helicon AST | Ocarina AST | 扩展注意 |
| --- | --- | --- | --- |
| 所有权 | 节点 `shared_ptr`，`Ast::Parser` 按 stage 保存 `EmbeddedShaderStructure` | `Function` 用 `unique_ptr` 拥有全部表达式/语句；节点有 `Function* context` | 不要跨 Function 复制/共享节点，`check_context` 会失败 |
| 类型 | `NameType`/`VecType`/`MatType` 等，本质是字符串 | `const Type*`，`Type::of`/`Type::resolve`，有 size/alignment/members | 用 `Type` 替换字符串；注意 `real`/`half` 精度解析 |
| 变量 | `Variate` 名字 + `AccessPermissions` | `Variable` uid/Tag/Usage，`RefExpr` 引用 | 变量身份用 uid，不要新造字符串变量 |
| 表达式 | `BinaryOperator`、`UnaryOperator`、`CallFunc`（字符串函数名） | `BinaryExpr`、`UnaryExpr`、`CallExpr`（Function/CallOp/template） | `CallFunc` 改为 `CallExpr`；内建函数走 `CallOp` |
| 语句 | 每个 stage 的全局/输入/输出/局部语句向量 | `ScopeStmt` 树，`IfStmt`/`LoopStmt`/`ForStmt`/`AssignStmt` 等 | 作用域和局部变量由 `ScopeStmt` 管理 |
| 使用标记 | 构造时按节点写 `AccessPermissions` | `Usage` 在表达式构造/`FunctionCorrector` 中标记 | 不要假设分析时机相同；通常要在 `correct()` 后读 usage |
| 入口/stage | `ShaderStage` + VS/FS/CS 的输入输出结构 | `Function::Tag::KERNEL/CALLABLE` + raytracing；当前只生成 compute | 先确定 Ocarina 是否需要光栅；否则不要扩展 VS/FS 语义 |
| 资源 | UBO/push constant/parameter block/纹理/数组/采样器 | `Variable::Tag`/`Type`：buffer、bytebuffer、texture2d/3d、bindless、accel | 不要 1:1 搬资源容器；先对齐 Ocarina 的 kernel ABI |
| 模块 | `slangModuleSource` + `import` | `Function::headers_` 只生成 `#include`；单函数单模块 | 需要新增 module dependency；不要用 `#include` 冒充 `import` |
| 分支 | `IfStatement` 带 `conditionDetector`，生成 branch 模块 | `IfStmt` 只有 condition + true/false scope | 需要新增元数据、分析和 codegen/compiler 支持 |
| 代码生成 | 每个节点 `parse()` 直接拼字符串 | visitor 层级 + 类型/layout/usage 语义 | 扩展现有 visitor，不要移植 Helicon 字符串生成器 |
| 编译/缓存 | 多 target、多 module、条件组合缓存 | 一个 Function -> 一个源码 -> 一个 SPIR-V；缓存按源码 hash | 分支模块需要新的编译/缓存键 |

#### 5.3 节点映射原则

| Helicon | Ocarina 目标 | 注意 |
| --- | --- | --- |
| `BasicType`/`VecType`/`MatType` | `Type::of<T>()` / `Type::resolve()` | 用 Ocarina 的 `real`/`half` 精度策略，不要保留 Helicon 类型名 |
| `AggregateType`/`DefineAggregateType` | Ocarina `Type` + `OC_STRUCT`/`TypeDesc`；`Function::add_used_structure` | 成员顺序、offset、alignment 以 Ocarina 反射为准 |
| `Variate`/`LocalVariate` | `Variable` + `RefExpr`，`Function::local()` | 局部变量要加入当前 `ScopeStmt` 的 `local_vars()` |
| `DefineLocalVariate` | `Function::local()` + `RefExpr` | Ocarina 没有独立的“声明语句”节点 |
| `Assign` | `Function::assign()` / `AssignStmt` | 左值 WRITE 由 `FunctionCorrector` 标记 |
| `IfStatement` | `IfStmt` | 普通 `if` 直接用；剪枝需要额外 BranchInfo |
| `WhileStatement` | `LoopStmt` + 条件 break，或新增条件字段 | Ocarina 没有单独的 `WhileStmt`；不要把 Helicon 节点直接塞进 Loop |
| `CallFunc` | `CallExpr`（`CallOp` 或 `Function` 引用） | 内建函数走 `CallOp`，自定义函数走 `Function` |
| `FunctionDeclaration` | `Function` + `CallExpr`/`used_custom_func_` | 不要生成 `extern` 字符串；让 codegen 生成 |
| `UniversalTexture`/`UniversalArray` | `Variable::Tag::TEXTURE2D/BUFFER/BINDLESS_ARRAY`（或扩展 Tag） | 先确认 cube/array/sampler 是否需要新增 Type/Tag |
| `UniformVariate` | Ocarina kernel 参数/捕获资源 | Ocarina 没有 UBO 节点；不要复制 Helicon 的 UBO 语义 |
| `DefineInputVariate`/`DefineOutputVariate` | `Function::arguments()`/`ReturnStmt`，或新增 stage/IO 元数据 | 当前 Ocarina 是 compute/RT；VS/FS 需要单独设计 |
| `AccessPermissions` | `Usage::READ/WRITE/READ_WRITE` | `READ_WRITE` 不是简单位或；要区分 lhs 写入和 rhs 读取 |
| `ParseOutput`/`BranchOutput` | `Function` + branch module plan | Ocarina 没有 per-stage `ParseOutput`；不要复制该结构 |

Ocarina AST 层的构造方式（示意）：

```cpp
auto kernel = Function::define_kernel([&] {
    auto *f = Function::current();
    auto *value = f->local(Type::of<float>());
    auto *one = f->literal(Type::of<float>(), 1.0f);
    f->assign(value, one);

    auto *branch = f->if_(f->binary(Type::of<bool>(), BinaryOp::GREATER, value, one));
    f->with(branch->true_branch(), [&] {
        // 在 true scope 内构造语句
    });
    f->with(branch->false_branch(), [&] {
        // 在 false scope 内构造语句
    });
});
```

#### 5.4 Branch Pruning 的 AST 设计

这是扩展的重点。Ocarina 的 `IfStmt` 目前只有 `condition`、`true_branch`、
`false_branch`，没有“宿主条件”概念，也没有 branch 模块。

**5.4.1 元数据**

优先不改 `IfStmt` 的类布局，而是通过下面任一方式关联一份 branch 信息：

- 旁表：`map<const IfStmt*, BranchInfo>`，由独立的 `BranchRegistry`（按
  `Function*`/`IfStmt*` 关联）持有；不要直接加到 `Function`/`IfStmt` 成员上，
  否则同样改变 `ocarina-ast` 的类布局；
- 子类：`class PrunedIfStmt : public IfStmt`，只在新构造路径中使用，codegen 通过
  `dynamic_cast` 识别；新增 `Function::if_pruned()` 工厂，现有 `Function::if_()`
  的签名和返回类型保持不变；
- 如果确实要加字段，必须接受 `ocarina-ast` 的 ABI/布局变化，并同步所有构造、
  hash、拷贝/移动和导出符号的使用方。

无论哪种方式，branch 信息至少包含：

```cpp
struct BranchInfo
{
    uint32_t index = 0;                 // 分支序号，按 AST 遍历顺序稳定分配
    uint64_t condition_key = 0;         // 宿主条件/缓存键，不参与设备端条件
    std::function<bool()> detector;     // 可选；建议放编译器/pipeline 侧
    bool pruned = true;                 // 普通 if 保持 false
    // 分析结果：外部变量、参数方向、branch 函数名等
};
```

建议把 `detector` 放在编译器/pipeline 侧的 branch 表中，AST 只关联 `index` 和
`condition_key`。如果把 `std::function` 直接放进 AST：

- 不要把它纳入 `IfStmt::compute_hash()` / `Function::compute_hash()`，否则同一
  AST 的条件回调变化会导致函数名/缓存键变化；
- 要保证回调生命周期不短于使用它的 pipeline；
- 不要依赖它的执行次数（Helicon 的 `$IF` 会多次求值条件）。

如果使用 `PrunedIfStmt` 子类，可以只在该子类里重写 `compute_hash()` 加入
`pruned`/`index`，保持普通 `IfStmt` 的 hash 完全不变。

**5.4.2 外部变量分析**

分支函数需要知道哪些变量要从 `main` 传进来。建议在 `Function::correct()` 之后
增加一个 branch 分析 pass：

1. 遍历 `true_branch`/`false_branch` 的 `ScopeStmt`；
2. 用 `RefExpr` 找到变量根；`MemberExpr`/`SubscriptExpr` 递归到 `range`/`parent`；
3. 排除在分支 `ScopeStmt::local_vars()` 中声明的局部变量（它们属于分支函数）；
4. 读取 `Variable::usage()`：`READ` -> `in`，`WRITE` -> `out`，
   `READ_WRITE` -> `inout`；
5. 用 uid/声明顺序排序，不要用指针地址排序（Helicon 用 `std::set<const Variate*>`
   的顺序只在本进程内稳定，不适合作为 ABI 顺序）；
6. 对 `CallExpr` 的 `REFERENCE` 参数、输出参数和 `AssignStmt` 左值额外标记写；
7. `CapturedResource`/kernel arguments 不要整体作为分支函数参数；资源应通过
   共享 preamble/模块可见，或只传 Ocarina 允许的资源句柄；
8. 拒绝或特殊处理分支内的 `ReturnStmt`/`BreakStmt`/`ContinueStmt`/`SwitchStmt`：
   独立 branch 函数无法 `return` 到 kernel，也不能 `break` 外层循环。

**5.4.3 分支标识与缓存**

- 普通 `IfStmt` 的 hash 不变；只有 `PrunedIfStmt`/branch 元数据参与时，才在
  子类或旁表中加入稳定 `index`/`pruned`，不要包含 `detector` 或条件值；
- `Function::compute_hash()` 保持稳定的函数身份；条件组合由编译器缓存键单独
  表示；
- branch 函数名用稳定序号生成，例如 `branch_<index>`，不要用 AST 指针；
- 同一个 AST 的不同条件值应复用 core 源码，只切换 branch module。

**5.4.4 与 FunctionCorrector 的交互**

Ocarina 的 `FunctionCorrector` 会改写捕获表达式、拆分参数结构并标记 usage。
branch 分析必须在它之后运行，否则：

- `AssignStmt` 左值可能还没有 `Usage::WRITE`；
- callable 的捕获还没展开成分支可用的参数；
- `param_struct` 拆分后变量可能已变化。

不要在 branch 分析后再跑一次 `FunctionCorrector`，否则同一表达式可能被重复
捕获/改写。如果 `IfStmt` 位于 callable 内，必须从 kernel 入口完成调用链的
correct 后再分析，否则 callable 的捕获和参数结构可能尚未展开。

#### 5.5 分支模块、type header 与 codegen

Helicon 的分支实现由三部分组成：core 里的 `extern` 声明和调用、独立的
true/false branch 模块、以及共享的 `type_header` 模块。Ocarina 目前是
“一个 Function -> 一个完整源码 -> 一个 `main`”，没有模块切分：

- `AstToSlangSource::_emit_function()` 生成 `[shader("compute")]` 和 `main`；
- `SlangShaderCompiler::compile()` 只找 `main` entry point；
- `Function::for_each_structure()`/`for_each_header()` 只服务于单个函数源码。

扩展时建议：

1. **共享 preamble**：把结构体、资源声明（`OCPushConstants`、`oc_push`、
   bindless 表等）和分支共用的类型放到一个共享 module/头中，core 和 branch
   模块只 `import`/包含一次，避免同一 struct 或 push constant 重复定义。
2. **core 模块**：保留 `main`，把剪枝 `IfStmt` 替换成 branch 函数调用，并生成
   `extern`/import 声明。
3. **branch 模块**：只生成 `export` 的分支函数，不生成 `[shader]`/`main`；
   函数体直接来自 `IfStmt::true_branch()`/`false_branch()` 的 `ScopeStmt`。
4. **命名与链接**：用稳定序号生成 `branch_<index>` 和唯一的 module 名，并确保
   Slang 不做名字修饰；Ocarina 当前编译器没有设置 `NoMangle`，需要新增选项或
   保证 `extern` 名字匹配。
5. **类型集合**：core 需要所有用到的类型，branch 模块只需要自己用到的类型；
   但共享类型必须在 preamble 中只定义一次。`Function::for_each_structure()`
   可以给出当前 Function 的 used 集合，扩展时要区分“preamble 的 union”和
   “branch 模块的子集”。
6. **代码生成隔离**：`SourceEmitter` 的 scratch、缩进、`FUNCTION_GUARD`、
   `generated_func_`/`generated_struct_` 都是生成期状态。branch 模块生成要使用
   独立的 scratch/生成集合，不能把 branch 函数误标记成普通 `Function`。
7. **C++ 路径**：`AstToCppSource` 是 `AstToSlangSource` 的基类；普通 `IfStmt`
   必须继续生成普通 `if`，只有带剪枝元数据的 `IfStmt` 才走模块切分。

不建议把 Helicon 的 `BranchOutput`/`type_header` 字符串直接搬进 Ocarina：
Ocarina 的类型定义来自 `Type`/`LayoutResolver`，资源声明来自 kernel ABI；
字符串拼接会和 `Type` 的 size/alignment、`VkShaderParameterLayout` 脱节。

#### 5.6 编译器、缓存与运行时

Ocarina 的 `SlangShaderCompiler::compile()` 目前是：

```text
Function -> emit_slang_source -> loadModuleFromSourceString
         -> findEntryPoint("main") -> compose(module, entry_point)
         -> link -> getEntryPointCode -> SPIR-V
```

缓存键是 `function.func_name() + source_hash + workgroup`；其中 `source_hash` 已包含
`slang_source_cache_version` 和 builtin 源码。它没有 branch 条件位，也没有多模块
compose。扩展分支剪枝有两种落地方式：

| 方案 | 做法 | 代价 |
| --- | --- | --- |
| 多模块 compose（接近 Helicon） | 首次编译加载 core/type_header/branch 模块，运行时按条件 compose/link | 需要改编译器 API、缓存和 pipeline 对象 |
| 按条件重编译（简单） | 每个条件组合重新生成 core 源码并编译 | 语义正确，但没有剪枝收益，适合先打通 |

如果选择多模块：

- 缓存键必须包含 branch 条件位、branch 顺序和 branch 模块源码 hash；只复用
  `function.func_name()` 会错误命中其他条件组合；
- `slang_source_cache_version` 要递增，磁盘 cache 也要按条件分目录/分键；
- branch 模块应以 library 方式加载（无 `main`），core 只保留一个 entry point；
- 运行时切换只做 compose/link 和 pipeline 重建，不重新加载/解析模块（这是
  Helicon 的行为，也是 `$IF` 相对普通 CPU `if` 的收益来源）。

Ocarina 的 pipeline layout 由 `VkShaderParameterLayout` 从 `Function` 的
arguments/captured resources 计算。分支变体必须保持同一 ABI：

- 两个分支不能改变 `Function` 的 arguments/captured resources 集合；分支专用
  资源会进入统一的 `VkShaderParameterLayout`，切换条件时不能改变该布局；
- 如果确实需要按分支改变布局，Ocarina 的 pipeline layout 不能只按当前分支计算，
  要么取并集/固定超集，要么在条件切换时重建 layout 和 pipeline；
- 不要在 branch 模块里重新声明一份不同的 `OCPushConstants`，否则 core 与
  branch 的 ABI 不一致。

条件回调的生命周期也不同于 Helicon：Ocarina 的 `Function` 可能比 pipeline
更早/更晚销毁。不要让 AST 持有一个指向临时宿主变量的 `std::function`；把
detector 放到 pipeline/compiler 侧，AST 只保留稳定 key 更安全。

#### 5.7 stage 与资源语义：哪些不要搬

Ocarina AST 目前是 compute/raytracing AST：

- `Function::Tag` 只有 `KERNEL`/`CALLABLE`，`set_raytracing()` 只区分 ray kernel；
- `AstToSlangSource` 固定生成 `[shader("compute")]`/`main`；
- 没有 VS/FS 的 `SV_POSITION`、`LOCATION`、`SV_TARGET`、MRT、`discard` 语义。

因此：

- 如果目标只是 Ocarina compute/RT：**不要迁移** Helicon 的 `ShaderStage`、
  `EmbeddedShaderStructure` 的 input/output 分区、`RasterizedPipelineObject`、
  `position()`、`SV_TARGET`、`discard`。这些在 Ocarina AST 里没有对应语义，
  强行塞进 `Function` 会让 codegen 和 ABI 都变复杂。
- 如果目标必须包含光栅 stage：需要单独设计 `EntryPoint`/stage/IO 元数据，
  并扩展 Ocarina 的光栅 codegen 和 pipeline；不要用 `Function::Tag` 或
  `Variable::Tag` 的临时值来“模拟”VS/FS。

资源语义也不要 1:1 搬迁：

| Helicon | Ocarina 现状 | 建议 |
| --- | --- | --- |
| `UniformVariate`（UBO 成员） | 没有 UBO 节点；kernel 参数/捕获资源走 push constant/BDA | 映射到 Ocarina 参数/结构体，不要新增 UBO 节点 |
| `pushConstant` | `OCPushConstants`/kernel 参数 | 用 Ocarina 现有参数 ABI |
| `UniversalTexture`（2D） | `Variable::Tag::TEXTURE2D` + `TEX2D_*` CallOp | 可映射 |
| `TextureCube`/`Texture2DArray` | 没有对应 Type/Tag/CallOp | 需要扩展 `Type::Tag`、`Variable::Tag`、`LayoutResolver`、codegen 和 ABI；不要只加 AST 节点 |
| `Sampler` | 没有 sampler 节点，2D 采样走 combined texture/描述符 | 不要移植 `SamplerType`；使用 Ocarina 的采样 `CallOp` |
| `UniversalArray` | `BUFFER`/`BINDLESS_ARRAY` | 映射到 Ocarina 资源，不要保留 Helicon `UniversalArray` 节点 |
| `ParameterBlock` | 没有对应容器，资源经 kernel 参数/bindless | 不要新增 ParameterBlock 节点 |
| `ShaderResources` 反射 | `VkShaderParameterLayout` + AST usage | 不要引入第二套反射；保持 Ocarina ABI 单一来源 |

尤其是 `TextureCube`、`Sampler`、UBO/ParameterBlock：它们不是“在 AST 里加一个
节点”就能工作的，必须同时改 `Type`/`Variable`、`TypeDesc`/`LayoutResolver`、
`AstToSlangSource`、`VkShaderParameterLayout` 和后端描述符绑定。若不在扩展目标
内，应在文档和代码中明确排除，避免留下半成品节点。

#### 5.8 推荐扩展顺序

1. **冻结 Ocarina AST 不变量**：先补 `IfStmt`/`ScopeStmt`/`Variable` 的测试，
   明确 hash、context、usage 和 corrector 的现有行为；保存现有 C++/Slang 源码
   作为 golden。
2. **只加 AST 扩展点**：用旁表或 `PrunedIfStmt` 子类增加 branch index/condition
   key/pruned 标记，不改 `IfStmt`/`Function` 的布局；普通 `if` 的 codegen 不变；
   用 AST 单元测试直接构造，不依赖前端。
3. **加分支分析 pass**：在 `Function::correct()` 之后收集外部变量、方向和
   局部变量过滤；用 uid/声明顺序稳定排序；补 `FunctionCorrector` 交互测试。
4. **加 Slang 分支生成**：共享 preamble + core `extern` + branch `export`；
   保持 `AstToCppSource` 的普通 `if` 不变；用 golden source 对比。
5. **加编译器多模块/条件缓存**：为分支新增独立 API/key/缓存，不改现有
   `compile()` 的 cache key 和磁盘缓存；补 cache 命中和条件切换测试。
6. **按目标决定资源/stage**：compute-only 就明确不迁移 VS/FS、UBO、Sampler、
   Cube；需要时另开设计，不要夹在 branch pruning 扩展里。
7. **清理**：删除 Helicon 专用概念在 Ocarina 侧的残留命名，更新 `src/ast` 和
   backend 文档。

#### 5.9 验证

先确认 Ocarina AST 自身可构建，再验证新增的 AST 行为。示意命令：

```powershell
python build_tools/dev.py build ocarina-ast --configuration Debug --target-family ocarina
python build_tools/dev.py build test-core-function-corrector --configuration Debug --target-family ocarina-tests
```

如果扩展分支到 codegen/compiler，额外运行 Slang 相关 test-* 和运行时 parity 测试。

建议验证顺序：

1. **独立性**：`src/ast` 不包含 Helicon 头/命名空间；AST 构建不依赖 EDSL 前端。
2. **AST 不变量**：普通 `IfStmt` 的 hash、context、`ScopeStmt::local_vars`、
   `FunctionCorrector` 行为和扩展前一致。
3. **分支元数据**：`IfStmt` 的 branch index 稳定；`std::function`/条件值不进入
   `Function::compute_hash()`；同一个 AST 的不同条件值只改变 condition key。
4. **分支分析**：外部变量、局部变量过滤、`in/out/inout` 方向、uid 排序、
   `CallExpr` 的 REFERENCE 参数都有单元测试。
5. **代码生成**：普通 `if` 的 C++/Slang 源码逐字不变；带剪枝的 core 产生
   `extern`/调用，branch 模块产生 `export` 函数；golden source 对比通过。
6. **链接**：core + type header + branch 模块可以 compose/link；无重复 struct、
   无 repeated push constant、`extern` 名称匹配（必要时 `NoMangle`）。
7. **缓存**：条件 A 和条件 B 生成不同 SPIR-V；缓存命中不会把 A 的结果给 B；
   source cache version 已按需递增。
8. **运行结果**：同一个 kernel/条件在 branch pruning 和普通 `if` 下输出一致；
   条件切换后 dispatch 结果一致。
9. **ABI**：`VkShaderParameterLayout` 的 push constant 大小/offset 在两个分支中
   一致；若不一致，已显式重建 layout/pipeline。
10. **回归**：`FunctionCorrector`、参数结构拆分、捕获资源、`switch`/`loop`/
    `return` 相关既有测试全部通过。

建议新增的验收项：

- [ ] `src/ast` 可独立构建，`rg "Helicon|EmbeddedShader" src/ast` 为空。
- [ ] 普通 `IfStmt`/`Function` 的 `sizeof`/公共布局不变；未使用扩展时零额外对象。
- [ ] 普通 `IfStmt` 的源码和扩展前逐字一致，函数 hash/名称不变。
- [ ] 既有 `SlangShaderCompiler::compile()` 的 cache key、磁盘缓存和
      `slang_source_cache_version` 行为不变。
- [ ] branch 分析与 `FunctionCorrector` 的先后顺序有测试覆盖。
- [ ] `ReturnStmt`/`BreakStmt`/`ContinueStmt` 出现在剪枝分支时被拒绝或明确降级。
- [ ] branch 模块不含 `main`/`[shader]`，core 只有一个 entry point。
- [ ] condition key 包含 branch 顺序和值，缓存测试覆盖 A->B->A。
- [ ] Ocarina 现有 compute/RT 测试通过。

#### 5.10 常见坑

| 坑 | 后果 | 对策 |
| --- | --- | --- |
| 直接给 `IfStmt`/`Function` 加成员 | `ocarina-ast` 类布局/ABI 变化，旧调用方和缓存失配 | 用旁表或 `PrunedIfStmt` 子类；不改旧类布局 |
| 普通 `IfStmt` 也走 branch codegen | 现有源码/SPIR-V/hash 全变，旧测试和缓存失效 | 默认路径保持原样，只有 opt-in 分支才走新路径 |
| 改现有 `compile()` 的 cache key/版本 | 旧磁盘缓存全部 miss，行为不可预期 | 新分支使用独立 key/版本，现有 key 保持不变 |
| 给 `Type::Tag`/`Variable::Tag` 复用旧值 | 现有 switch、hash、序列化和 ABI 被破坏 | 追加新值并检查所有 switch；必要时版本化 |
| 把 `std::function` detector 放进 `IfStmt` 并参与 hash | 函数名/缓存键随回调变化，或回调生命周期越界 | condition key 与 AST hash 分离；detector 放 pipeline/compiler 侧 |
| 把 `AccessPermissions` 直接当 `Usage` | Ocarina 的 WRITE 在 `FunctionCorrector` 里才标记，分析时机不对 | 在 `Function::correct()` 之后做 branch 分析 |
| 复制 Helicon 的 `Variate`/`NameType` 字符串 | 与 Ocarina `Type`/layout 脱节，ABI 错误 | 全部换成 `Type`/`Variable`/`RefExpr` |
| 用 `std::set<const Variate*>` 的顺序做参数顺序 | 指针顺序不稳定，跨构建 ABI 不一致 | 用 uid/声明顺序排序 |
| 把 branch `ScopeStmt` 内定义的局部变量当外部参数 | 分支函数拿到未定义变量或重复参数 | 先收集 `ScopeStmt::local_vars()` 并排除 |
| 忽略分支里的 `ReturnStmt`/`BreakStmt`/`ContinueStmt` | 独立 branch 函数无法 return 到 kernel 或 break 外层循环 | 限制剪枝分支只含局部语句；否则拒绝并报错 |
| 忽略 `FunctionCorrector` 的捕获/参数结构拆分 | 分支参数和实际表达式不一致 | 先 correct，再做 branch 分析；不要重复 correct |
| branch 模块又声明一份 `OCPushConstants`/`oc_push`/struct | Slang link 冲突或 ABI 不一致 | 共享 preamble/type header 只定义一次 |
| 没有 `NoMangle`/`export` 名字不匹配 | link 找不到 branch 函数 | 加编译器选项或统一 `extern`/`export` 名称 |
| 缓存键漏掉 branch 条件位/顺序 | 条件 B 复用了条件 A 的 SPIR-V | 缓存键包含 condition bits、branch 顺序、模块 hash |
| 两个分支的资源布局不同 | Ocarina pipeline layout 只按一遍 AST 计算，绑定错位 | 约束同一 ABI，或切换时重建 layout/pipeline |
| 用 `#include` 代替 Slang `import` | 模块隔离/预编译模块复用失效，重复定义 | 新增 module dependency；`headers_` 只用于文本 include |
| 改动 `AstToCppSource` 的普通 `if` 路径 | C++ 代码生成回归 | 只有带 branch 元数据的 `IfStmt` 走新路径，并加 golden 测试 |
| 把 VS/FS 语义塞进 `Function::Tag` | Ocarina compute/RT codegen 和 ABI 混乱 | 光栅 stage 单独设计；compute-only 明确排除 |
| 搬入 Helicon `ShaderCodeModule`/反射 | 与 Ocarina `Type`/`VkShaderParameterLayout` 形成两套真相 | 保留 Ocarina ABI；不要引入 Helicon 反射 |
