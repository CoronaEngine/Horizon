# 达到稳定144 FPS的优化方案（按优先级排序）

## 实测数据总结

| Example | p99 | vs目标 | Draw/Frame | 主要瓶颈 |
|---------|-----|--------|-----------|---------|
| ibl | 6.24ms | ✓ 达标 | ~10 | 已优化 |
| edsl_ibl | 9.34ms | +34% | ~10 | ？|
| assao | 42.29ms | +509% | **363** | CPU: 重复record |
| sponza | 9.28ms | +34% | 50 | GPU: 复杂场景 |
| edsl_sponza | 9.46ms | +36% | 50 | GPU: 复杂场景 |
| rsm | 10.54ms | +52% | ~200+ | GPU: 双pass+间接光 |
| edsl_rsm | 9.51ms | +37% | ~200+ | GPU: 双pass+间接光 |
| drawstress | 118.75ms | +1610% | **64,000** | CPU: 极端 |

## 优化方案（4个阶段）

### 🔴 Phase 1: 低垂的果实（预期2-3小时，巨大收益）

#### 1.1 assao: 预计算静态model矩阵
**问题**: 每帧调用3次record_scene()，每次对121个物体重新计算model矩阵
```cpp
// 当前: 每帧计算 3×121 = 363 次
const glm::mat4 model = glm::translate(...) * glm::scale(...);
```

**方案**:
```cpp
// 初始化时算一次，存起来
struct StaticTransform {
    glm::mat4 model;
    glm::vec4 color;
};
std::vector<StaticTransform> ground_and_models; // size=121

// 每帧只需取值
pipeline.vpc.model = ground_and_models[i].model;
pipeline.vpc.color = ground_and_models[i].color;
```

**预期**: p99从42ms → ~28ms（节省30%的矩阵计算）

**难度**: ⭐ 简单，纯重构，不改渲染逻辑

---

#### 1.2 assao: MRT合并3个geometry pass
**问题**: color/normal/depth各画一遍，121×3=363次record+3倍顶点处理

**方案**: 一个fragment shader同时输出到3个RT
```glsl
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepth;

void main() {
    outColor = ...;
    outNormal = vec4(normalize(vNormal), 1.0);
    outDepth = vec4(gl_FragCoord.z, 0, 0, 1);
}
```

**预期**: p99从28ms → ~12ms（减少2/3的geometry work）

**难度**: ⭐⭐ 中等，需要改shader和pipeline设置

**关键**: 这个改动不改变最终图像（3个RT的内容完全一致）

---

#### 1.3 rsm/edsl_rsm: 降低RSM分辨率
**问题**: RSM是间接光照的light space buffer，当前可能用的分辨率过高

**方案**: 
- 当前rsm_width/height是多少？（需要查代码）
- 降低到512×512或甚至256×256
- 间接光照本身就是低频，分辨率影响极小

**预期**: 取决于当前分辨率，可能p99从10.5ms → 7-8ms

**难度**: ⭐ 简单，改一个常量

---

### 🟡 Phase 2: 架构优化（预期4-6小时，中等收益）

#### 2.1 drawstress: GPU instancing
**问题**: 64,000次独立draw call

**方案**: 
```cpp
// 预计算全部transform到SSBO
struct InstanceData { mat4 mvp; };
HardwareBuffer instance_buffer; // 64000 × sizeof(mat4)

// Vertex shader
layout(set = X, binding = Y) readonly buffer Instances {
    mat4 mvps[];
};
gl_Position = mvps[gl_InstanceIndex] * vec4(position, 1.0);

// 一次draw
vkCmdDrawIndexed(..., 64000, ...);
```

**预期**: p99从119ms → <2ms

**难度**: ⭐⭐⭐ 中高，需要shader改动和SSBO

**注意**: **改变benchmark性质**，需要用户确认！从"测CPU draw call throughput"变成"测GPU instancing"

---

#### 2.2 sponza/edsl_sponza: Shadow map分辨率
**问题**: shadow pass可能用了过高分辨率

**方案**: 检查当前shadow分辨率，降低到1024×1024

**预期**: p99从9.3ms → 7-8ms

**难度**: ⭐ 简单

---

### 🟢 Phase 3: 深度profiling（需要工具完善）

#### 3.1 集成GPU timestamps
**问题**: 目前gpu_timestamps代码存在但未集成到例子中

**方案**: 
- 在每个example的主要pass前后加begin_scope/end_scope
- 运行后看report()输出，定位最慢的pass

**预期**: 找到隐藏瓶颈

**难度**: ⭐⭐ 需要改多个example

---

#### 3.2 edsl_ibl为什么比ibl慢50%？
**问题**: edsl_ibl p99=9.34ms vs ibl p99=6.24ms，差距不应该这么大

**可能原因**:
- EDSL codegen的descriptor管理有额外开销？
- Shader编译差异？
- 需要profiling对比

---

### 🔵 Phase 4: 激进方案（仅当前3个phase不够时）

#### 4.1 assao: Multi-draw indirect
如果MRT+预计算还不够，用indirect一次提交121个draw

#### 4.2 rsm: 分离RSM pass到低优先级queue
如果RSM pass很慢但不阻塞主渲染，可以异步

#### 4.3 全局: Reduce resolution
最后手段：从960×540降到更低（但这改变了benchmark条件）

---

## 推荐执行顺序

### Week 1: 快速胜利
1. **assao预计算** (2小时) → p99: 42ms → 28ms
2. **rsm降分辨率** (30分钟) → p99: 10.5ms → 8ms
3. **sponza shadow降分辨率** (30分钟) → p99: 9.3ms → 8ms

此时状态:
- ✓ ibl: 6.24ms
- ✗ edsl_ibl: 9.34ms (需phase 3)
- ✗ assao: 28ms (需phase 1.2)
- ✗ sponza: 8ms (接近，可能需phase 3)
- ✗ edsl_sponza: 8ms (同上)
- ✗ rsm: 8ms (接近)
- ✗ edsl_rsm: 8ms (接近)
- ✗ drawstress: 119ms (需phase 2.1 + 用户确认)

### Week 2: MRT重构
4. **assao MRT** (4-6小时) → p99: 28ms → <7ms ✓

此时assao达标。

### Week 3: Profiling驱动
5. 集成GPU timestamps到所有example
6. 找出sponza/rsm/edsl系列的真正瓶颈
7. 针对性优化

### Week 4: 决策点
- drawstress是否接受instancing？
- 如果不接受，drawstress无法达标（物理上不可能让CPU handle 64k draws @144fps）
- 其他7个example应该都能达标

---

## 立即可做的3个改动（今天）

1. **assao预计算** - 2小时，-33%
2. **检查RSM分辨率并降低** - 30分钟，-20-30%
3. **检查shadow分辨率并降低** - 30分钟，-10-15%

这3个改动风险极低，不改shader逻辑，立即见效。

明天可以tackle assao MRT（最大的单项改进）。
