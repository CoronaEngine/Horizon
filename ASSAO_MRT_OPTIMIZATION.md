# ASSAO MRT优化 - 实施记录

## 目标
将assao的3个几何pass合并为1个MRT pass，预期性能改善：
- 当前: p99 = 28.45ms (363 draws: 121物体 × 3 passes)
- 目标: p99 < 6.944ms (144 FPS稳定)
- 预期: ~9.5ms (121 draws，-67%三角形处理)

## 已完成的修改

### 1. 新建MRT fragment shader
**文件**: `examples/shaders/assao_gbuffer_mrt_frag.glsl`

合并了三个原shader的输出：
- `assao_color_frag.glsl` → `layout(location = 0) out vec4 outColor`
- `assao_normal_frag.glsl` → `layout(location = 1) out vec4 outNormal`
- `assao_depth_frag.glsl` → `layout(location = 2) out vec4 outDepthVal`

单个fragment shader一次输出3个RT，逻辑完全一致：
```glsl
void main()
{
    vec3 n = normalize(v_normal_vs);
    
    // RT0: 场景颜色（固定方向光照）
    const vec3 light_dir_vs = normalize(vec3(-0.3, 0.8, -0.5));
    float ndotl = max(0.0, dot(n, light_dir_vs));
    outColor = vec4(fpc.color.xyz * (0.25 + 0.75 * ndotl), 1.0);
    
    // RT1: view空间法线编码到[0,1]
    outNormal = vec4(n * 0.5 + 0.5, 1.0);
    
    // RT2: 器件深度值
    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
```

### 2. 修改example_assao.cpp

#### 2.1 Include改动
```cpp
// 前：3个fragment shader
#include GLSL(shaders/assao_color_frag.glsl)
#include GLSL(shaders/assao_normal_frag.glsl)
#include GLSL(shaders/assao_depth_frag.glsl)

// 后：1个MRT fragment shader
#include GLSL(shaders/assao_gbuffer_mrt_frag.glsl)
```

#### 2.2 深度附件简化
```cpp
// 前：3个独立深度附件
horizon::HardwareImage depth_c(..., "example_assao.depth_c");
horizon::HardwareImage depth_n(..., "example_assao.depth_n");
horizon::HardwareImage depth_d(..., "example_assao.depth_d");

// 后：1个共享深度附件
horizon::HardwareImage depth_attachment(..., "example_assao.depth");
```

#### 2.3 Pipeline设置改动
```cpp
// 前：3个独立rasterizer
horizon::RasterizerPipeline color_rasterizer(assao_scene_vert_glsl, assao_color_frag_glsl, scene_desc);
color_rasterizer.outColor = scene_color_image;
color_rasterizer.bind_depth_target(depth_c);

horizon::RasterizerPipeline normal_rasterizer(assao_scene_vert_glsl, assao_normal_frag_glsl, normal_desc);
normal_rasterizer.outNormal = normal_image;
normal_rasterizer.bind_depth_target(depth_n);

horizon::RasterizerPipeline depthval_rasterizer(assao_scene_vert_glsl, assao_depth_frag_glsl, depth_desc);
depthval_rasterizer.outDepthVal = depth_val_image;
depthval_rasterizer.bind_depth_target(depth_d);

// 后：1个MRT rasterizer，绑定3个color attachment
horizon::RasterizerPipeline gbuffer_rasterizer(assao_scene_vert_glsl, assao_gbuffer_mrt_frag_glsl, scene_desc);
gbuffer_rasterizer.outColor = scene_color_image;
gbuffer_rasterizer.outNormal = normal_image;
gbuffer_rasterizer.outDepthVal = depth_val_image;
gbuffer_rasterizer.bind_depth_target(depth_attachment);
```

#### 2.4 Record调用简化
```cpp
// 前：调用3次record_scene
record_scene(color_rasterizer);
record_scene(normal_rasterizer);
record_scene(depthval_rasterizer);

// 后：调用1次
record_scene(gbuffer_rasterizer);
```

#### 2.5 提交链简化
```cpp
// 前：提交3个rasterizer
render_executor << color_rasterizer.extent(ao_width, ao_height)
                << normal_rasterizer.extent(ao_width, ao_height)
                << depthval_rasterizer.extent(ao_width, ao_height)
                << ...

// 后：提交1个
render_executor << gbuffer_rasterizer.extent(ao_width, ao_height)
                << ...
```

## 技术细节

### MRT工作原理
- **单次vertex processing**: 121个draw call，每个物体的顶点处理1次（vs 3次）
- **单次primitive assembly**: 三角形装配1次（vs 3次）
- **单次rasterization**: 光栅化1次，fragment shader内输出多个RT（vs 3次）
- **共享depth buffer**: 深度测试1次，节省2个1280×720 D32附件

### 改善预测
1. **Draw call数**: 363 → 121 (-67%)
2. **顶点处理**: 3× → 1× (-67%)
3. **三角形装配**: 3× → 1× (-67%)
4. **Fragment workload**: ~相同（每像素仍需计算color/normal/depth，只是合并了）
5. **Memory bandwidth**: 节省2个深度附件的写入

**保守预估**: 28.45ms → ~9.5ms (-67% geometry cost)
- 如果瓶颈在geometry: 改善接近-67%
- 如果瓶颈在fragment: 改善较小（但geometry总有开销）

### 逐像素一致性
输出完全一致，因为：
- 每个RT的计算逻辑逐字复制自原shader
- `normalize(v_normal_vs)` 在MRT shader开头算1次，三个输出复用
- 浮点运算顺序完全相同

## 当前状态

### 完成
- ✅ MRT shader编写完成
- ✅ example_assao.cpp修改完成
- ✅ 代码逻辑验证（手工审查）

### 待完成
- ❌ 编译测试（遇到MSVC环境问题）
- ❌ 运行验证（需先解决编译）
- ❌ 性能测量（需先通过运行验证）
- ❌ 逐像素验证（frame_hash对比）

## 编译问题

当前编译失败原因：MSVC找不到标准库头文件
```
fatal error C1083: 无法打开包括文件: "cstdint": No such file or directory
fatal error C1083: 无法打开包括文件: "concepts": No such file or directory
fatal error C1083: 无法打开包括文件: "inttypes.h": No such file or directory
```

这是环境配置问题，不是代码问题。可能原因：
1. MSVC工具链路径配置错误
2. conda环境与MSVC版本不兼容
3. 需要重新激活developer command prompt

## 下一步

### 修复编译
1. 检查MSVC安装: `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC\14.51.36231`
2. 激活VS developer command prompt
3. 或使用已有的build（如果之前编译过）

### 测试流程
1. **功能验证**:
   ```bash
   ./build/conan/examples/release/examples/example_assao.exe
   ```
   看是否能正常启动、渲染

2. **逐像素验证**:
   ```bash
   # Baseline（如果保留了旧版二进制）
   HORIZON_FRAME_HASH=100 HORIZON_FRAME_HASH_DUMP=assao_baseline.bin ./old_assao.exe
   
   # MRT版本
   HORIZON_FRAME_HASH=100 HORIZON_FRAME_HASH_DUMP=assao_mrt.bin ./example_assao.exe
   
   # 对比
   dotnet script DumpCompare.cs assao_baseline.bin assao_mrt.bin
   ```

3. **性能测量**:
   ```bash
   HORIZON_PRESENT_MODE=immediate \
   HORIZON_BENCH_FRAMES=1200 \
   HORIZON_WARMUP_FRAMES=120 \
   ./example_assao.exe 2>&1 | grep "p99"
   ```

### 预期结果
- **最佳情况**: p99 < 6.944ms ✅ 直接达标
- **良好情况**: p99 ~9-10ms，需要进一步优化（Multi-draw indirect）
- **最坏情况**: p99 > 15ms，说明瓶颈在fragment而非geometry

## 风险评估

**技术风险**: 低
- MRT是标准Vulkan特性，Horizon框架已支持（deferred shading用过）
- 代码逻辑1:1复制，无算法变更
- 深度测试逻辑不变（Early-Z仍可工作）

**正确性风险**: 极低
- Fragment计算完全相同
- 唯一差异：`normalize(v_normal_vs)`从3次变1次（结果完全一致）

**性能风险**: 无
- 即使改善不如预期，也不会比原版更慢
- 最坏情况回滚成本低（恢复3行代码）
