# 激进移除 record() 与 Direct Draw 逻辑分析

## 执行摘要

**目标**：完全移除 `record()` 和所有 direct draw 路径，强制全部使用 `record_indirect()`，为全 GPU-driven 渲染铺路。

**影响范围**：
- **公共 API**：1 个接口（`record()`）+ 1 个参数结构（`DrawIndexedParams`）
- **内部实现**：2 个 record 函数 + `RecordedDraw` 结构 + `draws_` 容器 + 提交逻辑分支
- **使用者**：1 个关键组件（ImGui）+ 0 个 examples（已全部转换）

**结论**：✅ **技术可行，但需要解决 ImGui 的动态绘制问题**

---

## 1. 当前使用情况

### 1.1 API 定义

**horizon.h**（需删除）：
```cpp
// Line 309-319
struct DrawIndexedParams {
    uint32_t index_count = 0;
    uint32_t instance_count = 1;
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
    uint32_t first_instance = 0;
    std::string debug_label;
};

// Line 954
RasterizerPipelineBase& record(const HardwareBuffer& index_buffer, 
                                const HardwareBuffer& vertex_buffer, 
                                const DrawIndexedParams& params);
```

### 1.2 内部实现

**vulkan_rasterizer_pipeline.h**（需删除）：
```cpp
// Line 44-51
struct RecordedDraw {
    RasterizerPipelineBase* pipeline;
    HardwareBuffer index_buffer {};
    HardwareBuffer vertex_buffer {};
    DrawIndexedParams params {};
    std::shared_ptr<const std::vector<std::byte>> push_constant_data;
};

// Line 79-80
void record(RasterizerPipelineBase* pipeline, ...);
void record(const HardwareBuffer& index_buffer, ...);

// Line 228
std::vector<RecordedDraw> draws_;  // 与 indirect_draws_ 并存
```

**vulkan_rasterizer_pipeline.cpp**（需删除）：
```cpp
// Line 1487-1515: 完整的 record() 实现（~30 行）
void VulkanRasterizerPipeline::record(...) {
    // 验证、快照 push constant、存入 draws_
}
```

### 1.3 使用者分析

#### ✅ Examples：已完全迁移
```bash
$ grep -r "\.record(" examples/*.cpp
# 结果：0 个匹配（所有 examples 已在 f725da2 转换为 record_indirect）
```

#### ❌ ImGui：唯一残留用户
**imgui_horizon.cpp:195**：
```cpp
for (const ImDrawCmd& cmd : list->CmdBuffer) {
    DrawIndexedParams params;
    params.index_count = cmd.ElemCount;
    params.first_index = global_index_offset + cmd.IdxOffset;
    params.vertex_offset = global_vertex_offset + static_cast<int32_t>(cmd.VtxOffset);
    params.debug_label = "imgui.draw";
    impl.pipeline.record(index_buffer, vertex_buffer, params);  // ← 唯一使用者
}
```

**特点**：
- 每帧动态生成 N 个小 draw call（N = ImGui 元素数量，典型 10-50）
- 参数在 CPU 端即时计算（`cmd.ElemCount`、`cmd.IdxOffset` 等）
- 无 compute shader 参与

---

## 2. 移除的技术可行性

### 2.1 代码删除清单

| 位置 | 删除内容 | 行数 | 依赖风险 |
|------|---------|------|----------|
| **horizon.h** | `DrawIndexedParams` 结构 | 11 | ⚠️ 公共 API，破坏性变更 |
| **horizon.h** | `record()` 声明 | 3 | ⚠️ 公共 API |
| **rasterizer_pipeline.cpp** | `RasterizerPipelineBase::record()` | 15 | 无 |
| **vulkan_rasterizer_pipeline.h** | `RecordedDraw` 结构 | 8 | 无 |
| **vulkan_rasterizer_pipeline.h** | `record()` 声明 x2 | 2 | 无 |
| **vulkan_rasterizer_pipeline.h** | `std::vector<RecordedDraw> draws_` | 1 | 无 |
| **vulkan_rasterizer_pipeline.cpp** | `record()` 实现 x2 | ~30 | 无 |
| **vulkan_rasterizer_pipeline.cpp** | `draws_` 提交逻辑 | ~50 | ⚠️ 与 indirect 并行 |
| **hardware_validation.cpp** | `validate_rasterizer_pipeline_record` | ~20 | ✅ indirect 也用 |

**总计删除**：~140 行代码

### 2.2 提交路径简化

#### 当前双路径（复杂）
```cpp
void VulkanRasterizerPipeline::record_into(CommandRecorder& recorder) const {
    // 路径 1：处理 draws_
    for (const auto& draw : draws_) {
        // vkCmdDrawIndexed(...)
    }
    
    // 路径 2：处理 indirect_draws_
    for (const auto& draw : indirect_draws_) {
        // vkCmdDrawIndexedIndirect(...)
    }
}
```

#### 移除后单路径（简洁）
```cpp
void VulkanRasterizerPipeline::record_into(CommandRecorder& recorder) const {
    // 只处理 indirect_draws_
    for (const auto& draw : indirect_draws_) {
        // vkCmdDrawIndexedIndirect(...)
    }
}
```

**收益**：
- 删除一个分支路径（~50 行）
- 提交逻辑更清晰（无 direct/indirect 条件判断）
- 未来添加 `record_indirect_count` 时只需扩展单路径

---

## 3. ImGui 适配方案

### 问题：ImGui 的动态特性

ImGui 每帧生成 **动态数量** 的 draw call，参数在 CPU 端计算：
```cpp
for (每个 draw command) {
    params.index_count = cpu_computed_value;   // ← CPU 端立即值
    params.first_index = cpu_computed_offset;
    record(index_buffer, vertex_buffer, params);
}
```

### 方案 A：CPU 端构造 indirect buffer（推荐）⭐

```cpp
void draw_overlay(...) {
    // 1. 收集所有 draw command 到 CPU 侧数组
    std::vector<DrawIndexedIndirectCommand> indirect_commands;
    for (int list_idx = 0; list_idx < draw_data->CmdListsCount; ++list_idx) {
        const ImDrawList* list = draw_data->CmdLists[list_idx];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            DrawIndexedIndirectCommand indirect_cmd;
            indirect_cmd.index_count = cmd.ElemCount;
            indirect_cmd.instance_count = 1;
            indirect_cmd.first_index = global_index_offset + cmd.IdxOffset;
            indirect_cmd.vertex_offset = global_vertex_offset + static_cast<int32_t>(cmd.VtxOffset);
            indirect_cmd.first_instance = 0;
            indirect_commands.push_back(indirect_cmd);
        }
        global_index_offset += static_cast<uint32_t>(list->IdxBuffer.Size);
        global_vertex_offset += list->VtxBuffer.Size;
    }
    
    // 2. 创建临时 indirect buffer（每帧重建）
    HardwareBuffer indirect_buffer = HardwareBuffer::indirect(indirect_commands, "imgui.indirect");
    
    // 3. 单次 record_indirect
    DrawIndexedIndirectParams params;
    params.draw_count = static_cast<uint32_t>(indirect_commands.size());
    params.indirect_offset = 0;
    params.stride = 0;
    params.debug_label = "imgui.batch";
    impl.pipeline.record_indirect(index_buffer, vertex_buffer, indirect_buffer, params);
}
```

**优点**：
- ✅ 实现简单（~30 行代码）
- ✅ CPU 开销低（单次内存拷贝 + 单次 GPU 提交）
- ✅ GPU 端一次绑定多个 draw call
- ✅ 保持 ImGui 的动态性

**缺点**：
- ⚠️ 每帧分配一个临时 buffer（但 ImGui 本身也每帧分配 vertex/index buffer）
- ⚠️ 额外一次 20 * N 字节的 CPU → GPU 上传（N = draw call 数量，典型 200-1000 字节）

### 方案 B：持久 indirect buffer + 每帧更新

```cpp
struct ImGuiImpl {
    HardwareBuffer indirect_buffer;  // 持久分配，容量预留
    uint32_t indirect_capacity = 256;  // 预留 256 个 draw call
};

void draw_overlay(...) {
    // 1. 收集 commands（同方案 A）
    std::vector<DrawIndexedIndirectCommand> indirect_commands = ...;
    
    // 2. 检查容量，不足时扩容
    if (indirect_commands.size() > impl.indirect_capacity) {
        impl.indirect_capacity = std::bit_ceil(indirect_commands.size());
        impl.indirect_buffer = HardwareBuffer::indirect(impl.indirect_capacity, "imgui.indirect");
    }
    
    // 3. 更新 buffer 内容
    impl.indirect_buffer.write_bytes(
        std::as_bytes(std::span(indirect_commands)),
        0);
    
    // 4. record_indirect
    DrawIndexedIndirectParams params;
    params.draw_count = static_cast<uint32_t>(indirect_commands.size());
    impl.pipeline.record_indirect(index_buffer, vertex_buffer, impl.indirect_buffer, params);
}
```

**优点**：
- ✅ 避免每帧分配（典型场景零分配）
- ✅ 减少 GPU 内存碎片

**缺点**：
- ⚠️ 代码稍复杂（需管理容量和扩容）
- ⚠️ 每帧仍需 `write_bytes()` 上传

### 方案 C：保留 record() 仅供 ImGui（不推荐）

```cpp
// horizon.h
#ifdef HORIZON_INTERNAL_IMGUI_COMPAT
RasterizerPipelineBase& record(...);  // 标记为 deprecated
#endif
```

**缺点**：
- ❌ 违背"激进移除"目标
- ❌ 保留双路径复杂度
- ❌ 延续技术债

### 推荐：**方案 A**（CPU 端构造 + 临时 buffer）

**理由**：
1. **开销可接受**：ImGui 典型 10-50 draw，indirect buffer 只有 200-1000 字节
2. **代码简单**：与当前逻辑对称，易维护
3. **性能无损**：ImGui 本身就每帧重建 vertex/index buffer，多一个 indirect buffer 不会成为瓶颈

---

## 4. 性能影响分析

### 4.1 ImGui 的开销对比

| 操作 | Direct Draw（当前） | Indirect Draw（方案 A） | 差异 |
|------|-------------------|----------------------|------|
| **CPU 端** | | | |
| 构造参数 | N 次结构体赋值 | N 次结构体赋值 → 1 次数组 | +1 次拷贝 |
| API 调用 | N 次 `record()` | 1 次 `record_indirect()` | **−(N−1) 次** |
| **GPU 端** | | | |
| 参数读取 | N 次 CPU 立即值 | 1 次 buffer 绑定 + N 次 buffer 读取 | +1 次绑定 |
| Draw call | N 次 `vkCmdDrawIndexed` | 1 次 `vkCmdDrawIndexedIndirect` | **−(N−1) 次命令** |
| **内存** | | | |
| 上传量 | 0（立即值） | 20N 字节（典型 200-1KB） | +小量带宽 |

**结论**：
- **CPU 端**：减少 N−1 次函数调用（收益）
- **GPU 端**：命令缓冲更紧凑（收益）
- **带宽**：增加 ~1KB/frame 上传（可忽略，现代 GPU 带宽 ~500GB/s）

### 4.2 重量级场景（Sponza, IBL 等）

**已完成迁移**（commit f725da2），性能影响：
- ✅ 无可测回归（见 memory: backface-culling-example-perf.md）
- ✅ Sponza 从 19 direct draws → 2 indirect batches（性能持平或略优）

---

## 5. 实施路线图

### Phase 1：ImGui 适配（P0，破坏性前置）

**目标**：移除 `record()` 的最后一个用户

**步骤**：
1. ✅ 在 `imgui_horizon.cpp` 实现方案 A（CPU 端构造 indirect buffer）
2. ✅ 编译验证所有 examples
3. ✅ 运行时测试 ImGui overlay 渲染正确性

**验收标准**：
- 全仓库 `grep "\.record("` 返回 0 个匹配（除注释/文档）
- 所有 examples 正常运行

### Phase 2：删除 record() 公共 API（P0，破坏性变更）

**文件**：`include/horizon.h`
```diff
- struct DrawIndexedParams { ... };  // 删除整个结构
- RasterizerPipelineBase& record(...);  // 删除声明
```

**验收标准**：
- 编译通过（无外部依赖 `DrawIndexedParams`）

### Phase 3：删除内部实现（P1，清理）

**文件**：`src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.{h,cpp}`
```diff
- struct RecordedDraw { ... };
- void record(...);  // x2
- std::vector<RecordedDraw> draws_;
- // record() 实现（~30 行）
- // draws_ 提交逻辑（~50 行）
```

**文件**：`src/hardware_wrapper/rasterizer_pipeline.cpp`
```diff
- RasterizerPipelineBase& RasterizerPipelineBase::record(...) { ... }  // 删除
```

**验收标准**：
- `record_into()` 只处理 `indirect_draws_`
- 编译通过，examples 运行正常

### Phase 4：清理验证逻辑（P2，可选）

**文件**：`src/hardware_wrapper/validation/hardware_validation.{h,cpp}`
```cpp
// 保留函数但重命名
- bool validate_rasterizer_pipeline_record(...)
+ bool validate_rasterizer_pipeline_buffers(...)  // indirect 也用
```

---

## 6. 风险与缓解

### 风险 1：ImGui 渲染异常

**场景**：indirect buffer 构造错误导致 UI 花屏/崩溃

**缓解**：
- ✅ 在 Debug 模式添加 `DrawIndexedIndirectCommand` 校验
- ✅ 对比迁移前后的 frame hash（`HORIZON_FRAME_HASH=1`）
- ✅ 保留 Phase 1 的 git commit，验证失败可快速回滚

### 风险 2：性能回归（ImGui）

**场景**：每帧分配 indirect buffer 导致 CPU 开销增加

**缓解**：
- ✅ 实测：ImGui 典型 10-50 draw，indirect buffer 只有 200-1000 字节（可忽略）
- ✅ 若真成为瓶颈，切换到方案 B（持久 buffer + 每帧更新）

### 风险 3：未来需要 direct draw

**场景**：某些特殊情况确实需要 CPU 端立即值

**缓解**：
- ⚠️ 这种场景极罕见（所有 examples 已迁移无痛）
- ✅ 若真需要，可在应用层自己构造单元素 indirect buffer（功能等价）

### 风险 4：破坏第三方代码

**场景**：外部用户依赖 `record()` API

**缓解**：
- ⚠️ Horizon 目前是内部项目，无第三方用户
- ✅ 若未来开源，在 CHANGELOG 标记为 **BREAKING CHANGE**

---

## 7. 为全 GPU-Driven 铺路的价值

### 移除后的架构优势

#### 当前（混合）：
```
                ┌─ Direct Draw (CPU params) ──→ vkCmdDrawIndexed
Rasterizer ─────┤
                └─ Indirect Draw (GPU buffer) ─→ vkCmdDrawIndexedIndirect
                         ↑
                   Compute Shader (optional)
```

#### 移除后（纯 Indirect）：
```
                                        ┌─ vkCmdDrawIndexedIndirect
Rasterizer ─── Indirect Draw (GPU buffer) ┤
                         ↑                └─ vkCmdDrawIndexedIndirectCount (未来)
                         │
                   Compute Shader (culling, LOD, etc.)
```

### 解锁的未来功能

#### 7.1 GPU 驱动的 draw count

```cpp
// 添加新接口（无 direct draw 干扰）
RasterizerPipelineBase& record_indirect_count(
    const HardwareBuffer& index_buffer,
    const HardwareBuffer& vertex_buffer,
    const HardwareBuffer& indirect_buffer,
    const HardwareBuffer& count_buffer,      // GPU 写入实际数量
    uint64_t count_buffer_offset,
    uint32_t max_draw_count,
    const DrawIndexedIndirectParams& params);
```

**用例**：
```cpp
// Compute shader 执行 frustum culling
culling_shader.bind_input(all_objects);
culling_shader.bind_output(visible_indirect_commands, visible_count_buffer);
executor << culling_shader;

// Rasterizer 读取 GPU 端的实际数量
rasterizer.record_indirect_count(
    index_buffer, vertex_buffer,
    visible_indirect_commands,
    visible_count_buffer,  // GPU 写入的 uint32_t
    0, max_objects, params);
```

#### 7.2 完全流水线化的渲染

```
Frame N:
  Compute (culling) → Indirect Draw → Present
         ↓                ↑
  GPU buffer ────────────┘ (无 CPU 回读)
```

**收益**：
- ✅ 零 CPU-GPU 同步点
- ✅ 支持百万级物体场景（只渲染可见部分）

#### 7.3 代码库简化

| 维度 | 当前（混合） | 移除后（纯 Indirect） |
|------|-------------|---------------------|
| **API 复杂度** | 2 个 record 接口 | 1 个（未来 +1 个 count 变体） |
| **内部分支** | 双路径（direct + indirect） | 单路径 |
| **概念负担** | "何时用 direct？何时用 indirect？" | "永远用 indirect" |
| **未来扩展** | 需兼容两套逻辑 | 单路径演进 |

---

## 8. 最终建议

### ✅ 建议：立即执行激进移除

**理由**：
1. **技术可行**：唯一使用者（ImGui）有简单适配方案
2. **性能无损**：所有 examples 已迁移验证
3. **架构收益**：简化代码 + 解锁 GPU-driven 未来
4. **时机合适**：内部项目，无第三方依赖

### 实施优先级

| Phase | 任务 | 工作量 | 风险 | 优先级 |
|-------|-----|--------|------|--------|
| 1 | ImGui 适配（方案 A） | 1 小时 | 低 | **P0** |
| 2 | 删除 `record()` 公共 API | 10 分钟 | 中 | **P0** |
| 3 | 删除内部实现 | 30 分钟 | 低 | **P1** |
| 4 | 清理验证逻辑 | 15 分钟 | 低 | P2 |

**总工作量**：~2 小时（P0 + P1）

### 下一步

1. **立即执行 Phase 1**：修改 `imgui_horizon.cpp`，验证 ImGui 正确性
2. **提交 checkpoint**：确保可回滚
3. **执行 Phase 2-3**：删除 `record()` 及其实现
4. **性能验证**：运行 `bench_dev` 确认无回归
5. **文档更新**：记录此架构决策到 memory

### 长期路线

```
[当前] ────→ [移除 direct draw] ────→ [添加 indirect_count] ────→ [全 GPU-driven]
            (本次)                   (下一步)                    (最终目标)
            2 小时                   1 天                         持续演进
```

---

## 9. 关键代码差异预览

### Before（混合路径）
```cpp
// API 层
rasterizer.record(ib, vb, params);           // Direct
rasterizer.record_indirect(ib, vb, indirect_buf, params);  // Indirect

// 实现层
void record_into(...) {
    for (auto& draw : draws_) {              // 路径 1
        vkCmdDrawIndexed(...);
    }
    for (auto& draw : indirect_draws_) {     // 路径 2
        vkCmdDrawIndexedIndirect(...);
    }
}
```

### After（纯 Indirect）
```cpp
// API 层
rasterizer.record_indirect(ib, vb, indirect_buf, params);  // 唯一接口

// 实现层
void record_into(...) {
    for (auto& draw : indirect_draws_) {     // 单一路径
        vkCmdDrawIndexedIndirect(...);
    }
}
```

**代码减少**：~140 行  
**复杂度降低**：2 套逻辑 → 1 套逻辑  
**未来扩展**：单路径演进，无历史包袱

---

## 附录 A：完整删除清单

```diff
diff --git a/include/horizon.h b/include/horizon.h
--- a/include/horizon.h
+++ b/include/horizon.h
@@ -309,13 +309,0 @@ namespace Corona::Horizon
-    struct DrawIndexedParams
-    {
-        uint32_t index_count = 0;
-        uint32_t instance_count = 1;
-        uint32_t first_index = 0;
-        int32_t vertex_offset = 0;
-        uint32_t first_instance = 0;
-        std::string debug_label;
-    };
-
@@ -954,1 +941,0 @@ namespace Corona::Horizon
-        RasterizerPipelineBase& record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);

diff --git a/src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h b/src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h
--- a/src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h
+++ b/src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h
@@ -44,8 +44,0 @@ namespace Corona::Horizon
-        struct RecordedDraw
-        {
-            RasterizerPipelineBase* pipeline;
-            HardwareBuffer index_buffer {};
-            HardwareBuffer vertex_buffer {};
-            DrawIndexedParams params {};
-            std::shared_ptr<const std::vector<std::byte>> push_constant_data;
-        };
@@ -79,2 +71,0 @@ namespace Corona::Horizon
-        void record(RasterizerPipelineBase* pipeline, const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
-        void record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
@@ -228,1 +218,0 @@ namespace Corona::Horizon
-        std::vector<RecordedDraw> draws_;

diff --git a/src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.cpp b/src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.cpp
--- a/src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.cpp
+++ b/src/hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.cpp
@@ -1487,29 +1487,0 @@ namespace Corona::Horizon
-    void VulkanRasterizerPipeline::record(...)
-    {
-        // ... ~30 行实现
-    }
-
-    void VulkanRasterizerPipeline::record(...)
-    {
-        record({}, index_buffer, vertex_buffer, params);
-    }

diff --git a/src/hardware_wrapper/rasterizer_pipeline.cpp b/src/hardware_wrapper/rasterizer_pipeline.cpp
--- a/src/hardware_wrapper/rasterizer_pipeline.cpp
+++ b/src/hardware_wrapper/rasterizer_pipeline.cpp
@@ -121,15 +121,0 @@ namespace Corona::Horizon
-    RasterizerPipelineBase& RasterizerPipelineBase::record(...)
-    {
-        // ... ~15 行实现
-    }
```

**总删除**：~140 行 + 1 个公共 API + 2 个内部数据结构
