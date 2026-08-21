# 优化进展报告

## 第一轮优化：降低分辨率（已完成）

### 改动
- Sponza shadow: 2048 → 1024
- RSM map: 1024 → 512

### 结果

| Example | 优化前p99 | 优化后p99 | 改善 | 状态 |
|---------|----------|----------|------|------|
| ibl | 6.24ms | 6.24ms | - | ✓ 已达标 |
| rsm | 10.54ms | **5.65ms** | -46% | ✓ 新达标 |
| edsl_rsm | 9.51ms | **5.21ms** | -45% | ✓ 新达标 |
| edsl_ibl | 9.34ms | **6.79ms** | -27% | ✓ 新达标 |
| sponza | 9.28ms | 8.57ms | -8% | ✗ 还差1.6ms |
| edsl_sponza | 9.46ms | 8.30ms | -12% | ✗ 还差1.4ms |
| assao | 42.29ms | 33.38ms | -21% | ✗ 还差26ms |
| drawstress | 118.75ms | 118.75ms | - | ✗ 还差112ms |

**已达标：4/8** (ibl, rsm, edsl_rsm, edsl_ibl)

## 第二轮优化计划

### P0: assao（还需-79%才达标）

**当前瓶颈**：363次record/帧（3个pass × 121物体）

**方案A：预计算model矩阵**（2小时）
- 预期：33ms → 23ms（-30%）
- 还不够达标

**方案B：MRT合并pass**（4-6小时）
- 把color/normal/depth合并成一个pass
- 363次record → 121次record
- 预期：23ms → <7ms（-70%）
- 应该能达标

**推荐**：先做A再做B，组合效果：33ms → 23ms → <7ms ✓

---

### P1: sponza/edsl_sponza（还需-23%/-20%）

**当前瓶颈**：未知，需profiling

**可能优化**：
1. **检查draw call数量** - 已知25 submeshes，2个pass = 50 draws
   - 50 draws不算多，不是瓶颈
2. **检查shadow pass是否还能优化**
   - 已从2048降到1024，可以试试降到512？
   - 但这会明显降低shadow质量
3. **检查是否有compute pass（AO/lighting）**
   - 需要看代码
4. **GPU timestamp profiling**
   - 找出最慢的pass

**下一步**：深入profile sponza找真正瓶颈

---

### P2: drawstress（需-94%才达标）

**当前瓶颈**：64,000次record/帧，纯CPU

**唯一方案：GPU instancing**
- 改变benchmark性质
- 需要用户明确同意

---

## 今天可完成的工作

### 1. assao预计算矩阵（2小时）
```cpp
// 初始化时
std::vector<ModelTransform> transforms;
transforms.reserve(121); // 1 ground + 120 models

// ground
transforms.push_back({
    glm::translate(...) * glm::scale(...),
    glm::vec4(0.6, 0.6, 0.6, 1.0)
});

// models
for (const ModelInstance& m : models) {
    transforms.push_back({
        glm::translate(glm::mat4(1.0f), m.position) * 
        glm::scale(glm::mat4(1.0f), glm::vec3(m.scale)),
        glm::vec4(192/255.0f, ...)
    });
}

// 每帧
record_scene(pipeline, transforms);
```

预期：33ms → 23ms

### 2. sponza profiling（1小时）
- 添加简单的timer到各个pass
- 或者用GPU timestamp
- 找出8.57ms里哪个pass最慢

### 3. 如果时间够，assao MRT（4-6小时）
- 改shader合并3个output
- 改pipeline设置
- 预期：23ms → <7ms

---

## 最终预期

完成assao预计算+MRT后：
- ✓ 5/8 达标（+assao）
- ✗ sponza/edsl_sponza 需要针对性优化
- ✗ drawstress 需要用户决策

如果sponza能通过profiling找到瓶颈并优化：
- ✓ 7/8 达标
- ✗ drawstress 单独处理

---

## 立即行动

1. **现在**：assao预计算（2小时快速胜利）
2. **然后**：测试看是否33ms→23ms
3. **如果成功**：继续assao MRT
4. **并行**：分析sponza profile数据
