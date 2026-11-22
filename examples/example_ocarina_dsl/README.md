# Ocarina DSL 示例：从 C++ EDSL 到 Vulkan

这个示例用一条完整的链路渲染一个简单的光线步进（raymarching）画面：

```text
C++ EDSL (Kernel lambda)
        │  构建 AST
        ▼
Ocarina AST ──► AstToSlangSource ──► Slang 源码
                                        │
                                        │ device.compile()
                                        ▼
                                   Slang → SPIR-V
                                        │
                                        ▼
                              Vulkan Compute pipeline
                                        │ dispatch + storage texture
                                        ▼
                                   Texture2D 回读
                                        │
                                        ▼
                                      PNG
```

Ocarina 的 Vulkan 后端目前只有 compute，没有图形管线。这里的“渲染”指
compute shader 把程序化生成的画面写入 `Texture2D`，再回读到 CPU 保存成图片，
不涉及窗口、交换链或光栅化。

从 AST 构建到 Slang 生成的内部细节，见
[`docs/ocarina-dsl-example-flow.md`](../../docs/ocarina-dsl-example-flow.md)。

## 构建

示例已经并入根目录的 `examples/`，通过 `HorizonExamples` 的 `ocarina_dsl`
模式运行。构建时需要同时启用 examples 和 Ocarina：

```powershell
python build_tools/dev.py build HorizonExamples --configuration RelWithDebInfo --with-ocarina
```

`--with-ocarina` 会让 Conan 额外安装 Ocarina 依赖，并由
`build_tools/cmake/horizon_ocarina.cmake` 把
`examples/example_ocarina_dsl/example_ocarina_dsl.cpp` 和 Ocarina 库加入
`HorizonExamples`；不开启 Ocarina 的普通 examples 构建不会包含该模式。

如果该 examples 构建目录之前是在未启用 Ocarina 时配置的，CMake 缓存不会自动
把 `HORIZON_BUILD_OCARINA` 从 `OFF` 翻转。请显式覆盖一次，或使用干净的构建目录：

```powershell
cmake --preset examples-relwithdebinfo -DHORIZON_BUILD_OCARINA=ON
```

已经配置过该组合后，也可以直接构建：

```powershell
cmake --build --preset examples-relwithdebinfo --target HorizonExamples
```

## 运行

Ocarina 开启时，`HorizonExamples` 输出到 Ocarina 的 `bin/<Configuration>/`，
以便和 `ocarina-backend-vulkan-compute`、`slang/` 放在一起：

```powershell
.\build\conan\examples\relwithdebinfo\bin\RelWithDebInfo\HorizonExamples.exe ocarina_dsl
```

默认输出目录是当前工作目录下的 `ocarina_dsl_example_output/`：

```text
ocarina_dsl_example_output/
├── ocarina-dsl-raymarch.slang   # 生成的 Slang 源码
└── ocarina-dsl-raymarch.png     # Vulkan compute 渲染结果
```

模式名之后可以追加可选参数：

```text
--output <dir>       指定输出目录
--runtime-dir <dir>  指定 Ocarina 后端和 slang/ 头文件所在目录
--size <WxH>         输出分辨率，默认 960x540
--print-slang        在终端打印完整 Slang 源码
```

例如：

```powershell
.\build\conan\examples\relwithdebinfo\bin\RelWithDebInfo\HorizonExamples.exe `
    ocarina_dsl --size 1280x720 --output .\ocarina_dsl_example_output
```

程序会自动从可执行文件所在目录查找 `ocarina-backend-vulkan-compute` 和
`slang/`；因此从仓库根目录用完整路径运行也可以。

## 代码结构

| 阶段 | 位置 | 说明 |
| --- | --- | --- |
| 1. DSL 构建 | [`example_ocarina_dsl.cpp:134`](example_ocarina_dsl.cpp#L134) `make_raymarch_kernel()` | 用普通 C++ lambda 写 kernel：局部变量、`$if`、`$for`、`$break`、向量/数学运算、`Texture2DVar` 写入 |
| 2. Slang 生成 | [`example_ocarina_dsl.cpp:302`](example_ocarina_dsl.cpp#L302) `AstToSlangSource::generate()` | 调用后端使用的同一个 AST 访问器，得到 kernel 的 Slang body；再补上后端 prelude 和 include 保存为 `.slang` |
| 3. Vulkan 编译与调度 | [`example_ocarina_dsl.cpp:315`](example_ocarina_dsl.cpp#L315) `device.compile()` / [`example_ocarina_dsl.cpp:324`](example_ocarina_dsl.cpp#L324) `shader(texture).dispatch(resolution)` | 后端再次从 AST 生成 Slang，交给 Slang 编译成 SPIR-V，创建 compute pipeline；`Stream` 提交 dispatch，shader 把颜色写进 storage texture |
| 4. 回读/保存 | [`example_ocarina_dsl.cpp:325`](example_ocarina_dsl.cpp#L325) / [`example_ocarina_dsl.cpp:332`](example_ocarina_dsl.cpp#L332) | `texture.download()` 把像素读回 host，`Image::save_image()` 保存 PNG |

### 1. DSL 构建

kernel 的参数类型 `Texture2DVar` 会被 DSL 转成 shader 参数；lambda 里的控制流
使用 Ocarina 的 `$if` / `$for` / `$break` 宏，普通 C++ 控制流在构建期只负责
生成 AST。例如球体和地板的求交：

```cpp
$for(step, 96u) {
    const Float3 position = ray_origin + ray_direction * t;
    const Float sphere_distance = length(position) - 1.f;
    const Float plane_distance = position.y + 1.f;
    const Float distance = min(sphere_distance, plane_distance);

    $if(distance < 0.001f) {
        // ...
        hit = true;
        $break;
    };
    t += distance;
};
```

### 2. Slang 生成

```cpp
const ShaderCompilationContext context{{}, ShaderExecutionMode::Compute, shader_name};
const auto emitted = AstToSlangSource::generate(
    *kernel.function(), context, /*obfuscation=*/false, /*supports_warp32=*/false);
```

`AstToSlangSource` 位于 `src/backends/vulkan_compute/`，是 Vulkan 后端的
codegen 实现。示例把它和 `example_ocarina_dsl.cpp` 一起编译进
`HorizonExamples`（与 `test-codegen-trace-requirements` 的做法一致），因此
不需要额外的公共 API 就能展示生成的 Slang。保存的 `.slang` 文件包含后端 prelude（`OC_WORKGROUP_*`、
`slang_device_resource.slang` / `slang_device_math.slang`）和生成的 body；
在本示例中它与后端实际编译的源码一致，仅额外加了两行说明注释。
`device.compile()` 内部会再次执行同一套生成逻辑，然后继续做 Slang → SPIR-V。
如需用 `slangc` 单独编译保存的 `.slang`，把运行目录下的 `slang/` 加入
include path 即可。

### 3. Vulkan 编译与渲染

```cpp
auto shader = device.compile(kernel, shader_name);
auto texture = device.create_texture2d(resolution, PixelStorage::FLOAT4, shader_name);
auto stream = device.create_stream();
stream << shader(texture).dispatch(resolution)
       << texture.download(pixels.data())
       << synchronize()
       << commit();
```

`Device::compile` 通过 RHI 动态加载
`ocarina-backend-vulkan-compute`，由后端创建 compute pipeline。
`dispatch(resolution)` 为每个像素启动一个线程；kernel 用
`dispatch_idx()` 取得像素坐标，把最终颜色写到纹理对应位置。

### 4. 保存结果

```cpp
Image::save_image(image_path, PixelStorage::FLOAT4, resolution, pixels.data());
```

纹理是 `FLOAT4`，`Image::save_image` 会转换成 8-bit RGBA 并写出 PNG。

## 预期结果

运行成功时终端会打印 kernel 参数数量、workgroup 大小、生成 Slang 的行数、
执行模式和输出文件路径；PNG 是一个带棋盘格地面、橙色球体和渐变天空的画面。
如果设备没有可用的 Vulkan compute，示例会在创建设备或编译 shader 时给出后端
错误。
