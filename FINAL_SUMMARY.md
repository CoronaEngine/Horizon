# 深度优化分析 - 最终结论

## 执行摘要

经过系统性的测量和优化，**8个example中4个已达到稳定144 FPS (p99 < 6.944ms)**。

剩余4个需要架构级改动才能达标，trade-off需要与你确认。

---

## 最终状态

| Example | 初始p99 | 优化后p99 | 改善 | 状态 | 备注 |
|---------|---------|-----------|------|------|------|
| **ibl** | 6.24ms | **6.24ms** | - | ✅ 达标 | 背面剔除 |
| **rsm** | 10.54ms | **5.65ms** | -46% | ✅ 达标 | RSM 512 |
| **edsl_rsm** | 9.51ms | **5.21ms** | -45% | ✅ 达标 | RSM 512 |
| **edsl_ibl** | 9.34ms | **6.79ms** | -27% | ✅ 达标 | 全局优化 |
| sponza | 9.28ms | 8.57ms | -8% | ❌ 差1.6ms | 瓶颈未知 |
| edsl_sponza | 9.46ms | 8.30ms | -12% | ❌ 差1.4ms | 瓶颈未知 |
| assao | 42.29ms | 28.45ms | -33% | ❌ 差21.5ms | 需MRT |
| drawstress | 118.75ms | 118.75ms | 0% | ❌ 差111.8ms | 需instancing |

**已达标: 4/8 (50%)**

---

## 已完成的优化

### 1. 背面剔除 (ibl, edsl_ibl, assao)
- 逐像素验证：ibl/edsl_ibl仅2像素差异
- ibl达标，edsl_ibl因其他优化达标

### 2. 降低分辨率
- **RSM**: 1024→512 (-75%像素，间接光照低频)
  - rsm: 10.54ms → 5.65ms ✅
  - edsl_rsm: 9.51ms → 5.21ms ✅
- **Shadow**: 2048→1024 (-75%像素)
  - sponza: 9.28ms → 8.57ms (改善但未达标)
  - 继续降到512反而变慢，说明瓶颈不在shadow

### 3. assao预计算transform
- 避免每帧363次glm::translate/scale调用
- 42.29ms → 33.38ms → 28.45ms
- 改善-33%，但距离目标还有4倍差距

---

## 剩余4个example的深度分析

### assao - 需要MRT重构

**当前问题**:
- p99=28.45ms，目标6.944ms，**还需-76%**
- 即使预计算后，仍需要3个geometry pass各画121个物体
- 总计363次GPU draw，三角形处理3遍

**唯一可行方案: MRT (Multiple Render Targets)**

```glsl
// 当前: 3个shader各输出1个RT
// color_frag: outColor
// normal_frag: outNormal  
// depth_frag: outDepth

// MRT方案: 1个shader输出3个RT
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepth;

void main() {
    outColor = ...;
    outNormal = vec4(normalize(vNormal), 1.0);
    outDepth = vec4(linearDepth, 0, 0, 1);
}
```

**效果预测**:
- 363 draws → 121 draws (-67%)
- 三角形处理3遍 → 1遍 (-67%)
- 预期: 28.45ms → ~9.5ms

**如果还不够**: Multi-draw indirect
- 121 draws → 1 indirect call
- 预期: 9.5ms → <3ms ✅

**实施时间**: 6-8小时 (shader改动 + pipeline设置)

**风险**: 低，MRT是标准技术，输出内容完全一致

---

### sponza / edsl_sponza - 瓶颈未定位

**当前问题**:
- p99=8.57/8.30ms，目标6.944ms
- 还需-19%/-16%
- Shadow降分辨率无效（说明瓶颈不在shadow pass）

**可能瓶颈**:
1. **Deferred lighting pass** - 25个submeshes可能生成大量light volumes
2. **G-buffer输出** - 可能有多个RT
3. **透明/alpha test物体** - 帷幕等可能有overdraw
4. **Compute pass** - SSAO? screen-space reflections?

**需要做的**:
1. **GPU timestamp profiling** - 找出8.57ms里各pass的耗时
2. **针对性优化**最慢的pass

**可能方案** (取决于profiling结果):
- Light culling更激进
- 降低G-buffer精度 (RGBA16→RGBA8)
- 减少deferred lights数量
- 优化compute pass

**实施时间**: 
- Profiling: 2小时
- 优化: 4-8小时 (取决于瓶颈)

**风险**: 中等，可能需要降低某些特效质量

---

### drawstress - 物理上不可能

**问题**:
- 64,000次独立draw call，p99=119ms
- 目标6.944ms = **需要-94%**
- CPU提交开销，GPU几乎空转 (gate=0.003ms)

**根本矛盾**:
CPU单线程@4GHz，每次draw至少需要:
- 函数调用开销
- Push constant设置
- vkCmdDrawIndexed调用
- 驱动内部状态管理

即使每个draw只要1微秒 (极度优化):
- 64,000 × 0.001ms = 64ms (仍超9倍)

**唯一方案: GPU Instancing**

```cpp
// 当前: 64,000次CPU循环
for (int i = 0; i < 64000; ++i) {
    calc_mvp();  // CPU
    rasterizer.record(...);  // CPU
}

// Instancing方案: 预计算到GPU
// 初始化时
HardwareBuffer mvp_buffer; // 64000 × mat4
// ... 填充mvp_buffer

// Vertex shader
layout(set=X, binding=Y) readonly buffer MVPs { mat4 data[]; };
gl_Position = data[gl_InstanceIndex] * vec4(position, 1.0);

// 每帧: 1次draw
vkCmdDrawIndexed(cmd, indices, 64000, ...);
```

**效果**: 119ms → <2ms ✅

**Trade-off**: 
- ✅ 画面完全一致 (逐像素相同)
- ❌ **benchmark性质改变**:
  - 从"测CPU能否handle 64k draw calls"
  - 变成"测GPU instancing性能"
  - 失去了drawstress的原始意义

**实施时间**: 2-3小时

**决策点**: 你是否接受改变drawstress的benchmark性质？

---

## 推荐执行计划

### 选项A: 追求最多达标数量 (推荐)

1. **assao MRT** (6-8小时)
   - 预期达标 → **5/8**
   
2. **sponza profiling + 优化** (6-10小时)
   - 如果找到瓶颈，两个sponza都可达标 → **7/8**
   
3. **drawstress instancing** (2小时, 需你确认)
   - 如果接受benchmark性质改变 → **8/8** ✅

**总时间**: 14-20小时

**最终结果**: 7-8/8达标 (87.5%-100%)

---

### 选项B: 保守方案

1. **assao MRT** (6-8小时)
   - → **5/8**
   
2. **sponza分析但不改**
   - 记录瓶颈，但如果需要降质量则放弃
   
3. **drawstress保持现状**
   - 承认64k CPU draws无法@144fps

**总时间**: 6-8小时

**最终结果**: 5/8达标 (62.5%)

---

### 选项C: 快速胜利点 (当前状态)

**什么也不做，提交当前优化**

**总时间**: 0小时 (已完成)

**最终结果**: 4/8达标 (50%)

已完成的优化有价值:
- RSM分辨率降低 (合理trade-off)
- Shadow分辨率降低 (视觉影响小)
- assao预计算 (纯优化)
- 背面剔除 (逐像素验证)

---

## 我的建议

**执行选项A，但分阶段确认**:

### 今天/明天
1. **assao MRT** - 这是最大的单项改进
   - 低风险，标准技术
   - 如果达标 → 5/8

### 本周内
2. **sponza profiling** - 找真正瓶颈
   - 2小时profiling
   - 如果瓶颈是可优化的(如light culling) → 继续
   - 如果瓶颈是本质的(如三角形数量) → 放弃

### 决策点
3. **drawstress instancing** - 需要你明确同意
   - 如果你的目标是"所有example都144fps" → 做
   - 如果你的目标是"保持benchmark原意" → 不做

最坏情况: **5/8达标** (62.5%)
最好情况: **8/8达标** (100%)
最可能: **7/8达标** (87.5%)

---

## 你的决策

请明确以下几点:

1. **assao MRT** - 是否执行? (6-8小时工作量)
   - [ ] 是，追求达标
   - [ ] 否，保持当前状态

2. **sponza深度优化** - 如果需要降质量是否接受?
   - [ ] 是，质量可妥协
   - [ ] 否，质量优先

3. **drawstress instancing** - 是否接受改变benchmark性质?
   - [ ] 是，追求性能
   - [ ] 否，保持测试意义

4. **时间预算** - 还愿意投入多少时间?
   - [ ] 不限，追求100%
   - [ ] 10-20小时
   - [ ] 当前即可，提交现状
