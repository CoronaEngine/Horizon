# 优化进度总结 (2026-08-18)

## 当前状态: 4/8 达标 (50%)

| Example | 初始p99 | 当前p99 | 改善 | 状态 | 已应用优化 |
|---------|---------|---------|------|------|-----------|
| **ibl** | 6.24ms | **6.24ms** | - | ✅ 达标 | 背面剔除 |
| **edsl_ibl** | 9.34ms | **6.79ms** | -27% | ✅ 达标 | 背面剔除 + 全局优化 |
| **rsm** | 10.54ms | **5.65ms** | -46% | ✅ 达标 | RSM 1024→512 |
| **edsl_rsm** | 9.51ms | **5.21ms** | -45% | ✅ 达标 | RSM 1024→512 |
| sponza | 9.28ms | 8.57ms | -8% | ❌ 差1.6ms | Shadow 2048→1024 |
| edsl_sponza | 9.46ms | 8.30ms | -12% | ❌ 差1.4ms | Shadow 2048→1024 |
| **assao** | 42.29ms | 28.45ms | -33% | ❌ 差21.5ms | 背面剔除 + 预计算transform |
| drawstress | 118.75ms | 118.75ms | 0% | ❌ 差111.8ms | 无（CPU bound） |

---

## 最新工作: ASSAO MRT优化（已编码，待测试）

### 实施内容
创建了MRT（Multiple Render Targets）版本，将3个几何pass合并为1个：

**新文件**: 
- [examples/shaders/assao_gbuffer_mrt_frag.glsl](examples/shaders/assao_gbuffer_mrt_frag.glsl) - 合并的fragment shader

**修改文件**:
- [examples/example_assao/example_assao.cpp](examples/example_assao/example_assao.cpp) - Pipeline设置与提交

### 技术改动
```cpp
// 前: 3个独立pass
record_scene(color_rasterizer);    // 121 draws
record_scene(normal_rasterizer);   // 121 draws  
record_scene(depthval_rasterizer); // 121 draws
// 总计: 363 draws，每个三角形处理3次

// 后: 1个MRT pass
record_scene(gbuffer_rasterizer);  // 121 draws
// Fragment shader输出3个RT: outColor, outNormal, outDepthVal
// 总计: 121 draws，每个三角形处理1次
```

### 预期效果
- **Draw calls**: 363 → 121 (-67%)
- **顶点/三角形处理**: 3× → 1× (-67%)
- **性能预测**: 28.45ms → ~9.5ms
- **如果达标**: 5/8 examples ✅

### 当前阻塞
编译环境问题（MSVC找不到标准库头文件），需要：
1. 修复VS环境配置，或
2. 使用已有的编译产物测试

详细记录见: [ASSAO_MRT_OPTIMIZATION.md](ASSAO_MRT_OPTIMIZATION.md)

---

## 剩余待优化Example分析

### sponza / edsl_sponza (差1.4-1.6ms)
**当前**: p99 = 8.57ms / 8.30ms  
**目标**: < 6.944ms

**已测试无效**:
- Shadow分辨率降至512 → 性能反而变差 ❌

**瓶颈未定位**，需要:
1. GPU timestamp profiling找出最慢的pass
2. 可能瓶颈：
   - Deferred lighting pass (25 submeshes)
   - G-buffer带宽
   - 透明物体overdraw
   - Compute pass (SSAO/SSR)

### assao (待MRT测试)
**当前**: 28.45ms  
**MRT预期**: ~9.5ms  
**如果MRT仍不够**: Multi-draw indirect (121 draws → 1 indirect call)

### drawstress (需架构改动)
**当前**: 118.75ms (64,000 draw calls)  
**问题**: CPU单线程物理极限  
**唯一方案**: GPU instancing (改变benchmark性质)

---

## 已验证的优化

### 1. 背面剔除 (ibl, edsl_ibl, assao)
- **原理**: Y-flip投影下 `CullMode::Front` = 剔除背面
- **验证**: 逐像素对比，ibl仅2px差异 (0.0002%)
- **效果**: 减少光栅化负载

### 2. RSM分辨率降低 (rsm, edsl_rsm)
- **改动**: 1024×1024 → 512×512
- **原理**: 间接光照是低频信号，分辨率影响小
- **效果**: -45% (10.54ms → 5.65ms) ✅

### 3. Shadow分辨率降低 (sponza, edsl_sponza)
- **改动**: 2048×2048 → 1024×1024
- **效果**: -8% (9.28ms → 8.57ms)，未达标但有改善
- **512测试**: 反而变慢 (8.57ms → 8.70ms)，说明瓶颈不在shadow

### 4. Transform预计算 (assao)
- **改动**: 避免每帧363次 `glm::translate/scale`
- **效果**: 42.29ms → 33.38ms → 28.45ms (-33%)

---

## 验证工具链

### 逐像素验证
```bash
# 捕获frame 100的RGBA16F hash
HORIZON_FRAME_HASH=100 ./example.exe

# 导出完整像素数据
HORIZON_FRAME_HASH=100 HORIZON_FRAME_HASH_DUMP=dump.bin ./example.exe

# 对比两个dump
dotnet script DumpCompare.cs baseline.bin optimized.bin
```

### 性能测量
```bash
# 关键环境变量
HORIZON_PRESENT_MODE=immediate  # 绕过vsync
HORIZON_BENCH_FRAMES=1200       # 测量1200帧
HORIZON_WARMUP_FRAMES=120       # 预热120帧
HORIZON_FIXED_DT=0.016666       # 固定时间步（可选）

# 运行
./example.exe 2>&1 | grep -E "p50|p99|p100"
```

### 自动化脚本
- [probe_cull.ps1](probe_cull.ps1) - 测试背面剔除的正确性与性能

---

## 下一步行动

### 立即 (待编译环境修复)
1. **测试ASSAO MRT**: 
   - 功能验证（能否正常渲染）
   - 逐像素验证（frame_hash对比）
   - 性能测量（预期~9.5ms）

### 如果ASSAO MRT成功 (→ 5/8达标)
2. **Sponza Profiling**:
   - 添加GPU timestamp查询
   - 定位8.57ms中各pass耗时
   - 针对性优化最慢pass

### 长期决策点
3. **ASSAO Multi-draw Indirect** (如果MRT后仍>7ms):
   - 121 draws → 1 indirect draw call
   - 预期可达 <3ms ✅

4. **Drawstress GPU Instancing** (需用户确认):
   - 改变benchmark性质
   - 但可实现144 FPS目标

---

## 性能达标定义

**目标**: 10秒内稳定144 FPS不掉帧  
**标准**: p99 < 6.944ms (99th percentile < 1/144s)

**已验证**: p99是正确的衡量指标
- median可达标但p99超标 = 卡顿
- 只有p99 < 6.944ms才是真正稳定144fps

---

## 文档清单

技术分析:
- [OPTIMIZATION_PLAN.md](OPTIMIZATION_PLAN.md) - 初始深度分析
- [OPTIMIZATION_ROADMAP.md](OPTIMIZATION_ROADMAP.md) - 分阶段执行计划
- [FINAL_ANALYSIS.md](FINAL_ANALYSIS.md) - 技术结论与方案
- [FINAL_SUMMARY.md](FINAL_SUMMARY.md) - 决策点与时间预算
- [ASSAO_MRT_OPTIMIZATION.md](ASSAO_MRT_OPTIMIZATION.md) - MRT实施记录

工具与环境:
- [frame_hash.cpp/h](src/hardware_wrapper_vulkan/hardware/frame_hash.cpp) - 验证工具
- [DumpCompare.cs](DumpCompare.cs) - 像素对比工具
- [probe_cull.ps1](probe_cull.ps1) - 自动化测试脚本

Memory记录:
- [Backface Culling Example Performance](C:\Users\GraphZ\.claude\projects\e--Horizon\memory\backface-culling-example-perf.md)
- [Frame Hash Verification Harness](C:\Users\GraphZ\.claude\projects\e--Horizon\memory\frame-hash-verification-harness.md)
