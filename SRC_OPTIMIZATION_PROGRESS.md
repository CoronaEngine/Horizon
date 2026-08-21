# Src内部优化进度 (2026-08-18)

## 已完成的优化

### ✅ P1: Bindless descriptor sets 缓存
**状态**: 已实施（之前的commit）
**位置**: `src/hardware_wrapper_vulkan/hardware/execution.cpp:1485-1492`
**实现**:
```cpp
std::optional<std::array<VkDescriptorSet, ResourceManager::bindless_descriptor_set_count>> bindless_sets_cache;
auto bindless_sets = [&]() -> const std::array<VkDescriptorSet, ResourceManager::bindless_descriptor_set_count>& {
    if (!bindless_sets_cache.has_value())
        bindless_sets_cache = resource_manager().bindless_descriptor_sets();
    return *bindless_sets_cache;
};
```
**效果**: 消除每draw/dispatch的全局锁和数组拷贝

---

### ✅ P2: Prepare批次级缓存
**状态**: 已完成（commit d5c3ca3）
**位置**: `src/hardware_wrapper_vulkan/hardware/execution.cpp`

**Draw path** (已有):
- `DrawEncodeCache::prepared_valid` 缓存 `prepare_draw` 结果
- 只在 uniform_buffers 为空时复用

**Compute path** (新增):
- `DispatchEncodeCache` 结构
- 缓存 `prepare_dispatch` 结果
- 与 draw path 策略一致

**代码**:
```cpp
struct DispatchEncodeCache {
    const ComputePipelineBase* pipeline_key { nullptr };
    std::shared_ptr<VulkanComputePipeline> pipeline_impl;
    bool prepared_valid { false };
    VulkanComputePipeline::PreparedDispatch prepared {};
    // ...
} dispatch_cache;
```

**效果**: 
- 对 assao (4 compute dispatches) 有帮助
- 避免重复 prepare 调用开销

---

### ✅ P3: 死循环修复
**状态**: 已修复或记录有误
**结论**: 代码审查未发现 execution.cpp:1363/1417 有死循环问题

---

### ✅ P4: UBO截断修复
**状态**: 限制已提升至64
**位置**: 
- `vulkan_rasterizer_pipeline.cpp:1029`
- `vulkan_compute_pipeline.cpp:1178`
**实现**: `max_uniform_sets_per_layout = 64` 并有错误检查

---

## 待实施的优化

### P5: Multi-draw indirect 自动batch
**优先级**: 高（可能是达到144fps的关键）
**复杂度**: 高（1-2周）

**当前问题**:
- assao: 121 draws（即使geometry/shader相同）
- drawstress: 64,000 draws
- 每个draw call都有CPU overhead

**优化方案**:
- 收集相同pipeline的draw参数到buffer
- 使用 `vkCmdDrawIndexedIndirect`
- 处理 per-draw push constants（需要转为 vertex buffer或SSBO）

**预期效果**:
- assao: 121 draws → 1 indirect call (28.45ms → <7ms) ✅
- drawstress: 64k draws → 1 indirect call (118ms → <7ms) ✅

**实施难点**:
- 需要IR层支持
- Per-draw数据（model matrix等）需要从push constant迁移
- 可能需要修改shader接口

---

### P6: Pipeline缓存序列化
**优先级**: 中
**复杂度**: 低（2-4小时）

**优化方案**:
- 启用 `VkPipelineCache`
- 序列化到磁盘（~/.cache/horizon/pipeline.cache）
- 启动时加载

**预期效果**:
- 首帧流畅度提升
- 减少 pipeline 创建 stutter

---

## 性能影响预测

### 已完成优化（P1+P2）
**预期提升**: 5-10%
- 消除热路径上的锁竞争
- 减少重复调用开销
- 所有example受益

**特别受益的场景**:
- 高draw count: assao (363 → 121 draws后)
- 高dispatch count: assao compute (4 dispatches)
- Bindless路径（uniform_buffers为空）

### 如果实施P5（Multi-draw indirect）
**预期提升**: 巨大
- assao可能达标: 28.45ms → <7ms
- drawstress可能达标: 118ms → <7ms
- **关键优化**，可能让2个example达标

---

## 当前example状态预测

考虑移除背面剔除的影响（性能略有下降）和P1+P2优化（略有提升）：

| Example | 移除剔除前 | 移除剔除后预测 | P1+P2后预测 | 目标 | 状态 |
|---------|-----------|---------------|------------|------|------|
| ibl | 6.24ms | ~6.5ms | ~6.3ms | 6.944ms | ✅ 可能达标 |
| edsl_ibl | 6.79ms | ~7.0ms | ~6.8ms | 6.944ms | ⚠️ 临界 |
| rsm | 5.65ms | 5.65ms | 5.60ms | 6.944ms | ✅ 达标 |
| edsl_rsm | 5.21ms | 5.21ms | 5.15ms | 6.944ms | ✅ 达标 |
| sponza | 8.57ms | 8.57ms | 8.50ms | 6.944ms | ❌ 差1.6ms |
| edsl_sponza | 8.30ms | 8.30ms | 8.25ms | 6.944ms | ❌ 差1.3ms |
| assao | 28.45ms | ~29ms | ~28ms | 6.944ms | ❌ 需MRT+P5 |
| drawstress | 118.75ms | 118.75ms | 118ms | 6.944ms | ❌ 需P5 |

**预测**: P1+P2改善约2-5%，但移除背面剔除会抵消部分收益。

---

## 下一步建议

### 选项A: 测试当前优化效果
编译并测试P1+P2的实际效果：
```bash
cmake --build build --config Release
# 测试各example的p99
```

### 选项B: 继续src优化
1. **P6: Pipeline缓存** (2-4小时) - 改善首帧体验
2. **P5: Multi-draw indirect** (1-2周) - 关键优化，可能让assao/drawstress达标

### 选项C: 回顾example层优化
- ASSAO MRT已编码（待测试）
- Sponza profiling找瓶颈

---

## 建议

**立即**: 尝试编译测试，看P1+P2的实际效果

**短期**: 如果P1+P2效果明显，继续P6（pipeline缓存）

**长期**: 评估P5（multi-draw indirect）的投入产出比
- 如果目标是"尽可能多的example达标"，P5是必需的
- 如果接受"部分example达标"，当前优化可能已足够

要继续吗？
