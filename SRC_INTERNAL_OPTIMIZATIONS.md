# Src内部优化方案（无需修改example）

## 目标
在保持API不变（特别是**不使用背面剔除**）的前提下，通过优化src内部实现提升所有example的性能。

---

## 已完成的优化（历史记录）

### ✅ P0: Bindless slot回收
- **问题**: descriptor slot单调泄漏
- **修复**: free_list回收机制
- **状态**: 已落地 (commit 14e6902)

### ✅ Descriptor绑定缓存
- **问题**: 每draw/dispatch重复bind descriptor sets和push UBO
- **修复**: encode()内按layout缓存绑定点
- **效果**: ibl 26 draws bind从26→1，push从26→1
- **状态**: 已落地

### ✅ Per-draw desc()拷贝热点
- **问题**: 每draw深拷贝SPIR-V+反射表
- **修复**: 先比条件再拷贝
- **效果**: drawstress commit 265ms→31.5ms
- **状态**: 已落地

---

## 待实施的src内部优化

### P1: 移除bindless_descriptor_sets()的锁竞争
**当前问题**:
```cpp
// 每次调用都过全局锁
const auto& sets = resource_manager().bindless_descriptor_sets();
```

**优化方案**:
- bindless三个set是进程单例，句柄恒定
- 在VulkanCommandEncoder初始化时缓存
- 整个submission期间复用，无需每次lock

**预期效果**:
- 消除热路径上的锁竞争
- 对高draw count场景（assao 363 draws）改善明显

**实施位置**: `src/hardware_wrapper_vulkan/hardware/execution.cpp`

---

### P2: prepare_draw/prepare_dispatch 每draw重算
**当前问题**:
- `prepare_draw()`/`prepare_dispatch()` 每个draw/dispatch都重新计算并加锁
- 批次内这些信息通常是恒定的

**优化方案**:
- 批次级缓存prepare结果
- 只在pipeline变更时重新prepare

**预期效果**:
- 减少CPU开销
- 对draw-heavy场景有帮助

**实施位置**: `src/hardware_wrapper_vulkan/hardware/execution.cpp`

---

### P3: sampled-image死循环修复
**当前问题**:
- `execution.cpp` 1363/1417行存在sampled-image处理死循环
- 可能导致某些配置下hang

**优化方案**:
- 修复循环条件
- 添加防护检查

**预期效果**:
- 修复潜在的hang问题
- 提升稳定性

**实施位置**: `src/hardware_wrapper_vulkan/hardware/execution.cpp:1363, 1417`

---

### P4: Push descriptor UBO截断
**当前问题**:
- 每个set限制16个UBO
- 超过部分被静默截断

**优化方案**:
- 提升限制或动态分配
- 添加warning/assert

**预期效果**:
- 修复正确性问题
- 避免难以排查的bug

**实施位置**: Push descriptor相关代码

---

### P5: MRT Pipeline优化（部分在example）
**当前问题**:
- assao有3个几何pass各121 draws = 363 draws total
- 每个三角形处理3次

**优化方案A（已完成代码，待测试）**:
- 使用MRT合并为1个pass
- 单fragment shader输出3个RT
- 预期: 363→121 draws, 28.45ms→~9.5ms

**优化方案B（src内部实现）**:
- 在Vulkan层自动检测多pass写相同几何
- 自动合并为MRT（需要复杂的IR分析）
- **难度高，暂不建议**

---

### P6: Batch优化
**当前问题**:
- 每个draw call都有overhead
- 即使geometry/shader相同

**优化方案**:
- Multi-draw indirect
- 在src层自动batch相同pipeline的draws
- 需要：
  - 收集draw参数到buffer
  - 使用vkCmdDrawIndexedIndirect
  - 处理per-draw push constants

**预期效果**:
- assao: 121 draws → 1 indirect call
- drawstress: 64k draws → 1 indirect call
- **巨大改善**

**实施位置**: 
- `src/hardware_wrapper_vulkan/hardware/execution.cpp`
- `src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.cpp`

**复杂度**: 高（需要IR层支持）

---

### P7: Pipeline缓存优化
**当前问题**:
- Pipeline创建开销大
- 可能在首次draw时stall

**优化方案**:
- 启用VkPipelineCache并序列化
- 预热常用pipeline变体

**预期效果**:
- 首帧流畅度提升
- 减少stutter

**实施位置**: Pipeline创建代码

---

## 优先级排序（按投入产出比）

### 立即执行（低成本，高收益）
1. **P1: bindless_descriptor_sets()缓存** (1小时)
   - 代码改动小
   - 热路径优化
   - 所有example受益

2. **P3: 死循环修复** (30分钟)
   - 修复正确性问题
   - 避免潜在hang

3. **P4: UBO截断修复** (1小时)
   - 修复正确性问题
   - 添加诊断

### 中期执行（中等成本，高收益）
4. **P2: prepare_*批次缓存** (2-3小时)
   - 代码改动中等
   - draw-heavy场景受益

5. **P7: Pipeline缓存** (2-4小时)
   - 标准Vulkan特性
   - 改善首帧体验

### 长期执行（高成本，极高收益）
6. **P6: Multi-draw indirect自动batch** (1-2周)
   - 复杂，需要IR层支持
   - 但收益巨大（assao/drawstress）
   - 可能是达到144fps的关键

---

## 建议实施顺序

### 第一轮：快速胜利（今天，3小时）
1. P1: bindless缓存
2. P3: 死循环修复
3. P4: UBO截断修复

**预期**: 修复正确性问题 + 5-10%性能提升

### 第二轮：稳定提升（本周，5-7小时）
4. P2: prepare缓存
5. P7: Pipeline缓存

**预期**: 再+5-10%性能提升，首帧流畅

### 第三轮：架构优化（下周，1-2周）
6. P6: Multi-draw indirect

**预期**: 
- assao可能达标 (28.45ms → <7ms)
- drawstress可能达标 (118ms → <7ms)
- **关键优化**

---

## 当前建议

**开始P1-P4快速修复**，这些都是src内部改动，无需修改example，且：
- 修复已知问题（P3/P4）
- 消除热路径overhead（P1/P2）
- 所有example受益
- 代码改动小，风险低

要开始吗？我可以先实施P1（bindless缓存）。
