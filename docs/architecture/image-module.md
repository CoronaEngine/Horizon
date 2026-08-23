# Horizon Image 模块拆分设计

> - 状态：已实施
> - 日期：2026-08-22
> - 目标位置：`src/image`
> - CMake target：`horizon-image`
> - 命名空间：`horizon::image`

## 1. 背景

当前 CPU 图像容器位于 `src/dsl/image`，但其实现不构造 AST，也不使用 `Var`、`Expr`、`Function` 等 DSL 类型。它负责 CPU 像素存储、颜色转换以及图像文件加载和保存，真实职责不属于 DSL。

这些文件最初来自旧 `modules/ocarina/core/image`，迁入 Horizon 后曾位于 `src/core/image`。为解除 Core 对 Math 的反向依赖，它们被临时上移到 `src/dsl/image`。这种移动解决了局部依赖问题，但没有给图像功能建立真实的模块所有者。

## 2. 目标与非目标

目标：

- 从 `horizon-dsl` 中完全移除 CPU 图像容器与图像编解码。
- 新增独立的 `horizon-image` 静态库。
- 允许 Image 单向依赖 Math，同时保持 `math -> core` 的基础依赖方向。
- 完整保留现有 PNG/JPG/BMP/TGA/HDR/EXR 加载和保存能力。
- 不再引用最终会删除的旧 `modules/ocarina/ext`。

非目标：

- 不设计或实现 RHI/GPU texture 上传接口。
- 不改变 RHI 物理 Image 或 Vulkan Image 的设计。
- 不重构现有 Math 类型或颜色算法。
- 不改变现有 Image 格式 API、像素转换行为或文件格式分派规则。

## 3. 目标依赖方向

箭头表示左侧模块可以依赖右侧模块：

```text
horizon-image -> horizon-math -> horizon-core
horizon-image -> horizon-core
```

禁止出现：

```text
horizon-core -> horizon-image
horizon-math -> horizon-image
horizon-dsl -> horizon-image   // 基础 DSL 不应因图像 I/O 增加依赖
```

未来的 GPU 上传便利接口应位于 `horizon-runtime`，由该层同时依赖 `horizon-image` 和 `horizon-rhi`。

## 4. 模块职责

### 4.1 Core 保留的内容

[`src/core/image/image_format.h`](../../src/core/image/image_format.h) 保持在 Core，继续提供不依赖 Math 的轻量格式协议：

- `core::PixelStorage`
- 像素格式的通道数、标量类型和字节尺寸等纯值信息

这些类型可由类型系统、Image、未来 RHI 和 Runtime 层共同使用。Core 不拥有 CPU 图像缓冲、文件路径或编解码实现。

### 4.2 Image 模块拥有的内容

新 `horizon::image` 模块拥有：

- `ColorSpace`
- `EToneMap`
- `ImageWrap`
- `ImageBase`
- `ImageView`
- `Image`
- CPU 像素缓冲与平均颜色
- 8-bit/32-bit 格式转换
- sRGB/Linear 转换调用
- 文件加载和保存

上述类型保持现有命名，只将所有者命名空间从 `horizon::core` 改为 `horizon::image`。本次不顺带进行无关 API 重命名。

### 4.3 Image 模块不拥有的内容

- Vulkan `VkImage`、image view、sampler 和 allocation
- GPU texture、descriptor 和 image barrier
- DSL texture expression、sample、load 和 store
- AST image type、resource binding 和 reflection

CPU `image::Image`、运行时 `runtime::Texture`、底层 `rhi::Image` 和 shader 侧 `dsl::TextureRef` 是不同身份，不能合并为一个通用 Image 类型。

## 5. 目录与公开接口

目标目录：

```text
src/image/
├─ CMakeLists.txt
├─ image_base.h
├─ image.h
└─ image.cpp
```

保持现有 `ImageBase`、`ImageView` 和 `Image` 的数据接口及格式处理行为，统一迁入 `horizon::image` 命名空间。实现继续使用 `math::uint2`、`math::float3`、`math::float4` 等类型，因此 `horizon-image` 公开依赖 `horizon-math`。

`Image::load()` 继续根据扩展名选择普通 8-bit 图像、HDR 浮点图像或 EXR 图像。`load_hdr`、`load_exr`、`save_hdr` 和 `save_exr` 全部保留。

## 6. 格式支持与第三方依赖

STB 提供：

- PNG
- JPEG/JPG
- BMP
- TGA
- HDR

TinyEXR 提供：

- EXR

依赖管理规则：

- `stb/cci.20240531` 从 examples 专用依赖提升为 Horizon 基础依赖，因为 `horizon-image` 在基础设施目标族中构建。
- 新增 Conan 依赖 `tinyexr/1.0.7`，替代旧 `modules/ocarina/ext/tinyexr`。
- 新增 `cmake/horizon_image_dependencies.cmake`，通过 `find_package(stb CONFIG REQUIRED)` 和 `find_package(tinyexr CONFIG REQUIRED)` 建立 `horizon::stb`、`horizon::tinyexr` alias。
- `horizon-image` 私有链接 `horizon::stb` 和 `horizon::tinyexr`；第三方头文件不出现在 Image 公共头文件中。
- examples 继续复用同一个 `horizon::stb` target，不重复创建 alias。
- 不使用 `FetchContent`，不引用 `modules/ocarina/ext`。

## 7. CMake 接入

`src/CMakeLists.txt` 按以下顺序接入：

```text
core
math
image
ast
dsl
```

`src/image/CMakeLists.txt` 创建：

```text
horizon-image
  PUBLIC  horizon-core horizon-math
  PRIVATE horizon::stb horizon::tinyexr
```

`horizon-dsl` 删除对 `image/image.cpp` 的排除规则。文件迁出后，DSL target 不再 glob 到任何 Image 文件，也不新增对 `horizon-image` 的依赖。

`horizon-tests` 增加对 `horizon-image` 的构建依赖，并接入 Image 测试目录。

## 8. 错误处理

- 无法打开、解析或写入文件时抛出包含路径和格式信息的异常。
- 不支持的扩展名必须显式报错，不静默选择其他编码格式。
- 像素格式、数据大小和分辨率不匹配时，在分配或编码前拒绝操作。
- 所有可能失败的加载与保存接口不得标记为 `noexcept`。

## 9. 测试与验证

新增 `tests/image`，至少覆盖：

1. 从内存创建 2x2 RGBA Image，并验证分辨率、格式、字节大小和 view。
2. PNG 无损保存与加载往返，验证尺寸和像素数据。
3. BMP/TGA 保存与加载 smoke test。
4. JPEG 保存与加载，验证尺寸和通道，不要求逐字节相等。
5. HDR 浮点保存与加载，验证尺寸、格式和有限数值。
6. EXR 浮点保存与加载，验证尺寸、通道、格式和有限数值。
7. 不支持扩展名产生明确错误。
8. `horizon-dsl` 公共头聚合检查不需要链接 `horizon-image`、STB 或 TinyEXR。
9. Core/Math 依赖边界检查继续通过。

验证命令应覆盖：

- 重新 Configure，因为新增 target、源目录和 Conan 依赖。
- `horizon-image` 与相关测试 target 构建。
- Image 测试执行。
- 现有 Core、Math、AST、DSL 基础设施 target 构建与测试。
- `git diff --check`。

## 10. 迁移结果判定

完成迁移后必须满足：

1. `src/dsl/image` 不再存在。
2. `horizon-dsl` 不链接 `horizon-image`。
3. `horizon-image` 可以独立于 DSL、AST、RHI 和旧 Ocarina 构建。
4. `horizon-core` 继续不依赖 `horizon-math`。
5. 图像实现不包含 `modules/ocarina` 或 Vulkan 头文件；TinyEXR 只通过 Conan target 使用。
6. 所有 Image 类型位于 `horizon::image`，格式协议位于 `horizon::core`。
7. STB 实现宏只在 `src/image/image.cpp` 中定义一次。

## 11. 实施记录

截至 2026-08-22，迁移已落地：`horizon-image` 在 Core 与 Math 之后接入基础目标族，所有 host Image 类型位于 `horizon::image`，STB 与 TinyEXR 均来自 Conan。`src/dsl/image` 已移除，`horizon-dsl` 不链接 `horizon-image`。

迁移测试还确认并修复了旧 `Image::from_data` 的分配/释放协议不匹配问题：Image 持有的数组现在统一由 `new_array` 分配并由数组 deleter 释放。未知扩展名不再静默按 JPEG 编码，而是显式抛出包含扩展名和路径的异常。
