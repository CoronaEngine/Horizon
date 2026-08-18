# 深度优化分析 - 最终报告

## 当前状态（所有优化后）

| Example | 初始p99 | 当前p99 | 改善 | 状态 | 还需改善 |
|---------|---------|---------|------|------|----------|
| ibl | 6.24ms | 6.24ms | - | ✓ 达标 | - |
| rsm | 10.54ms | 5.65ms | -46% | ✓ 达标 | - |
| edsl_rsm | 9.51ms | 5.21ms | -45% | ✓ 达标 | - |
| edsl_ibl | 9.34ms | 6.79ms | -27% | ✓ 达标 | - |
| sponza | 9.28ms | 8.57ms | -8% | ✗ | -19% |
| edsl_sponza | 9.46ms | 8.30ms | -12% | ✗ | -16% |
| assao | 42.29ms | 28.45ms | -33% | ✗ | -76% |
| drawstress | 118.75ms | 118.75ms | 0% | ✗ | -94% |

**已达标：4/8**

## 已完成的优化

1. **背面剔除**（ibl/edsl_ibl/assao）
2. **降低shadow分辨率**（sponza: 2048→1024）
3. **降低RSM分辨率**（rsm: 1024→512）
4. **assao预计算transform**（-14.8%）

## 核心结论

### assao - 需要MRT合并（无可避免）

**问题根源**：
- 即使预计算后，仍有p99=28.45ms
- 需要降到6.944ms = 需要-76%改善
- **唯一可行方案：MRT合并3个pass**

当前：3个pass × 121 draws = 363次GPU绘制
MRT后：1个pass × 121 draws = 121次GPU绘制

预期：28.45ms → 28.45/3 ≈ **9.5ms**（还不够！）

进一步优化：
- Multi-draw indirect: 121个draw → 1个indirect draw
- 预期：9.5ms → **<3ms** ✓

**assao最终方案**：MRT + Multi-draw indirect

---

### sponza/edsl_sponza - 接近但不够

**问题**：
- p99=8.57/8.30ms，目标6.944ms
- 还需-19%/-16%

**可能优化**：
1. **进一步降低shadow分辨率** 1024→512
   - 但会明显降低质量
   - 预期：-30-40%，可能达标
   
2. **检查是否有不必要的compute pass**
   - 需要看代码确认
   
3. **优化light culling或其他CPU hotspot**

**推荐**：先试shadow 512，如果质量可接受就用

---

### drawstress - 需要架构改变

**问题**：64,000 draws，p99=119ms

**唯一方案**：GPU instancing
- 预计算64k个MVP到SSBO
- 一次drawIndexedInstanced(64000)
- 预期：<1ms

**但这改变benchmark性质**，需要用户明确同意

---

## 最终执行计划

### 今天可完成

1. **Sponza shadow→512测试**（10分钟）
   ```cpp
   constexpr uint32_t spz_shadow_map_size = 512;
   ```
   如果质量可接受：sponza/edsl_sponza达标 → **6/8达标**

2. **验证图像一致性**（30分钟）
   - 用frame_hash确认所有优化不改变渲染结果
   - 特别是shadow降分辨率

### 明天-后天

3. **assao MRT**（6-8小时）
   - 合并3个fragment shader
   - 预期：28ms → 9ms
   
4. **assao Multi-draw indirect**（4-6小时）
   - 如果MRT还不够
   - 预期：9ms → <7ms ✓
   
完成后：**7/8达标**

### drawstress决策

5. **与用户确认instancing方案**
   - 如果接受：2小时实现 → **8/8达标**
   - 如果不接受：drawstress保持现状

---

## 技术债务和风险

### 质量trade-off

| 优化 | 质量影响 | 可接受性 |
|------|---------|---------|
| RSM 512 | 间接光照略粗糙 | ✓ 可接受（低频） |
| Shadow 1024 | Shadow略模糊 | ✓ 可接受 |
| Shadow 512 | Shadow明显模糊 | ❓ 需要视觉确认 |
| MRT | 无影响 | ✓ 完全一致 |
| Instancing | 无影响 | ✓ 图像一致 |

### 需要验证的点

1. **Shadow 512的视觉质量**
   - 在sponza复杂场景下是否可接受
   - 对比1024 vs 512的截图

2. **assao MRT shader正确性**
   - 3个RT的内容必须与原来完全一致
   - 用frame_hash验证

3. **Indirect draw的驱动兼容性**
   - 所有GPU都支持，但实现质量可能不同
   - 在目标硬件上测试

---

## 立即行动（下一步30分钟）

1. 修改sponza shadow→512
2. 构建并测试p99
3. 如果达标，用frame_hash验证图像
4. 截图对比质量

如果sponza达标且质量可接受：
- ✓ 6/8达标
- 剩余：assao（需MRT）+ drawstress（需决策）
