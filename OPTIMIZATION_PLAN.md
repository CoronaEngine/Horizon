# 深度分析：达到稳定144 FPS (p99 < 6.944ms)

## 当前状态（p99测量）

| Example | p50 | p95 | p99 | 目标 | 状态 | 瓶颈分析 |
|---------|-----|-----|-----|------|------|---------|
| ibl | 5.19ms | 5.97ms | **6.24ms** | 6.944ms | ✓ 唯一达标 | 已用背面剔除 |
| edsl_ibl | 7.23ms | 8.95ms | **9.34ms** | 6.944ms | ✗ 超34% | ? |
| assao | 32.16ms | 39.26ms | **42.29ms** | 6.944ms | ✗ 超6倍 | **CPU: 363次record()/帧** |
| sponza | 8.22ms | 9.10ms | **9.28ms** | 6.944ms | ✗ 超33% | ? |
| edsl_sponza | 8.35ms | 9.23ms | **9.46ms** | 6.944ms | ✗ 超36% | ? |
| rsm | 8.45ms | 9.54ms | **10.54ms** | 6.944ms | ✗ 超52% | ? |
| edsl_rsm | 8.21ms | 9.21ms | **9.51ms** | 6.944ms | ✗ 超37% | ? |
| drawstress | 96.18ms | 106.81ms | **118.75ms** | 6.944ms | ✗ 超17倍 | **CPU: 64,000次record()/帧** |

## 问题严重性分级

### P0 - 完全不可接受
- **assao**: p99=42ms，需要减少到**1/6**
- **drawstress**: p99=119ms，需要减少到**1/17**

### P1 - 需要显著优化
- **rsm**: p99=10.54ms，需要减少34%
- **edsl_ibl**: p99=9.34ms，需要减少26%
- **edsl_sponza**: p99=9.46ms，需要减少27%
- **edsl_rsm**: p99=9.51ms，需要减少27%
- **sponza**: p99=9.28ms，需要减少25%

## 深度分析

### assao - CPU瓶颈

**问题**：
- 3个rasterizer pass，每个都调用record_scene()
- record_scene()对121个物体(1地面+120模型)各调用一次record()
- 总计：3 × 121 = **363次record()/帧**
- 每次record()都重新计算model矩阵（glm::translate + glm::scale）
- 每次都设置push constant（pipeline.vpc.model, pipeline.vpc.color）

**优化方向**：
1. **预计算model矩阵** - 场景静态，矩阵可以只算一次存起来
2. **批量record** - 把3个pass的record()调用合并/复用
3. **减少pass数** - 能否MRT一次画出color+normal+depth？
4. **GPU driven** - 如果真的需要这么多draw，考虑indirect + SSBO

**方案A：MRT合并pass（最优）**
- 改成一个shader同时输出color/normal/depth到3个RT
- record_scene只调用一次，减少到**121次record()**
- 三角形只装配一次，减少顶点处理
- **预期提升**：3倍，p99 从42ms → ~14ms（还不够，需要继续）

**方案B：预计算+批量**
- 初始化时算好全部121个model矩阵，存vector
- record_scene内部改成按index取矩阵，避免每帧glm运算
- 可能还需要优化record()本身的开销
- **预期提升**：1.5-2倍，p99 从42ms → ~21-28ms（不够）

**方案C：Multi-draw indirect**
- 一次vkCmdDrawIndexedIndirect画全部
- 需要打包draw params到GPU buffer
- **预期提升**：5-10倍，p99可能到5-8ms

**结论**：assao必须用方案A(MRT)或C(indirect)，B不够。

### drawstress - 极端CPU瓶颈

**问题**：
- 40³ = **64,000次独立record()**
- 每次都计算euler angle + 矩阵乘法
- 每次设置mvp push constant
- p99=119ms几乎全是CPU

**优化方向**：
1. **GPU instancing** - 一次drawIndexed(64000 instances)，transform在shader里索引SSBO
2. **Multi-draw indirect** - vkCmdDrawIndexedIndirect一次提交全部
3. **减少draw count** - 降低dim（但这改变benchmark意义）

**方案A：GPU instancing（推荐）**
- 预计算64000个mvp矩阵到SSBO
- vertex shader: mvp = mvp_array[gl_InstanceIndex]
- 一次drawIndexedInstanced(cube, 64000)
- **画面完全一致，但benchmark性质改变**（不再测draw call overhead）
- **预期**：p99从119ms → <1ms

**方案B：Multi-draw indirect**
- 仍是64000个draw，但API调用变一次
- 需要打包IndirectDrawCommand数组
- **预期**：p99从119ms → ~10-20ms（API开销降低，但仍有64k个draw）

**结论**：drawstress的benchmark意义是"测CPU能否handle大量draw"，如果用instancing就失去意义。但要稳定144FPS，**必须用instancing或indirect**。需要和用户确认是否接受。

### sponza / edsl_sponza - p99=9.3ms

**已知**：
- 背面剔除不安全（破坏9%像素）
- 背面剔除也没有性能提升
- p50=8.2ms已接近目标，但p99=9.3ms超33%

**可能原因**：
- Sponza场景复杂，draw call多
- 可能有compute pass（AO? shadow?）
- p99尖峰可能是间歇性的descriptor分配或barrier

**优化方向**：
1. 检查有多少draw call，是否能合批
2. 检查shadow pass能否用更低分辨率
3. 检查是否有不必要的barrier
4. 用GPU timestamp定位具体哪个pass慢

### rsm / edsl_rsm - p99=10.5ms / 9.5ms

**已知**：
- 有两个pass：RSM light space + 主场景
- 背面剔除不安全（破坏21%像素）
- 但back culling能3倍加速（说明严重overdraw）

**可能原因**：
- RSM pass分辨率高？
- 主场景有大量单面几何导致overdraw
- 两个pass都在画复杂场景

**优化方向**：
1. 降低RSM分辨率（间接光照，低分辨率影响小）
2. 分析哪个pass更慢（RSM pack vs scene）
3. 检查能否用depth prepass减少overdraw（不改变输出）

## 通用优化策略

### CPU侧
1. **预计算静态数据** - 矩阵、颜色等
2. **减少record()调用** - 批量、MRT、间接
3. **优化record()内部** - 如果有热点

### GPU侧
1. **降低不重要pass的分辨率** - RSM、shadow
2. **MRT合并pass** - assao的3个pass
3. **Depth prepass** - 减少overdraw（仅当有大量alpha test或复杂fragment shader）

### 同步/分配
1. **检查每帧是否有malloc/descriptor分配**
2. **检查不必要的barrier** - 可能导致bubble
3. **预分配资源** - descriptor、staging buffer

## 行动计划

### 第一批（最大收益）
1. **assao MRT合并** - 预期p99从42ms → ~14ms
2. **drawstress instancing** - 预期p99从119ms → <1ms（需用户确认）

### 第二批（中等收益）
3. **rsm降低分辨率** - 预期p99从10.5ms → ~7-8ms
4. **assao预计算矩阵** - 在MRT基础上继续优化

### 第三批（fine-tuning）
5. **sponza/edsl_sponza profiling** - 找p99尖峰根因
6. **edsl_ibl profiling** - 为什么比ibl慢50%？

## 需要用户确认

1. **drawstress instancing** - 会改变benchmark的测量目标，是否接受？
2. **RSM分辨率** - 从多少降到多少？视觉质量trade-off
3. **assao MRT** - 需要改shader，work量较大
