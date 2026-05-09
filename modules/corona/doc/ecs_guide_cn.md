# Corona Framework ECS 指南

本文档描述 Corona Framework 中的实体组件系统（ECS）模块。

> **状态**：ECS 目前正在开发中（Phase 1 已完成）。实体管理、查询系统和 World 集成计划在后续阶段实现。

## 概述

ECS 模块提供基于 Archetype 的实体存储系统，针对缓存友好的数据访问和高性能迭代进行了优化。主要特性：

- **Archetype 存储**：具有相同组件组合的实体存储在一起
- **SoA 布局**：数组结构（Structure-of-Arrays）内存布局，优化缓存利用率
- **类型安全**：C++20 concepts 确保编译期类型检查
- **零开销抽象**：直接指针运算实现 O(1) 组件访问

## 核心概念

| 术语 | 描述 |
|------|------|
| **Component（组件）** | 满足 `Component` 概念的纯数据结构 |
| **Archetype（原型）** | 存储具有相同组件类型的实体 |
| **Chunk（内存块）** | Archetype 内部的固定大小内存块（16KB） |
| **Signature（签名）** | 唯一标识 Archetype 的组件类型集合 |

## 头文件

```
include/corona/kernel/ecs/
├── ecs_types.h           // 基础类型（EntityId, ArchetypeId, EntityLocation）
├── component.h           // Component 概念、ComponentTypeInfo、ComponentRegistry
├── archetype_signature.h // ArchetypeSignature 用于组件集合标识
├── archetype_layout.h    // 内存布局计算
├── chunk.h               // Chunk 内存管理
└── archetype.h           // Archetype 主类
```

## 1. 定义组件

组件是满足 `Component` 概念的普通数据结构：

```cpp
#include "corona/kernel/ecs/component.h"

using namespace Corona::Kernel::ECS;

// 位置组件
struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// 速度组件  
struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
};

// 生命值组件
struct Health {
    int current = 100;
    int max = 100;
};

// 所有组件必须满足 Component 概念
static_assert(Component<Position>);
static_assert(Component<Velocity>);
static_assert(Component<Health>);
```

### 组件要求

`Component` 概念要求类型必须：
- 可默认构造（`std::is_default_constructible_v<T>`）
- 可移动构造（`std::is_move_constructible_v<T>`）
- 可析构（`std::is_destructible_v<T>`）

非平凡类型（如 `std::string`）也受支持，但会有略微更高的开销。

### 注册组件

在使用组件创建 Archetype 之前，需要在 `ComponentRegistry` 中注册：

```cpp
CORONA_REGISTER_COMPONENT(Position);
CORONA_REGISTER_COMPONENT(Velocity);
CORONA_REGISTER_COMPONENT(Health);
```

## 2. 创建 Archetype

### 使用 ArchetypeSignature

```cpp
#include "corona/kernel/ecs/archetype.h"

using namespace Corona::Kernel::ECS;

// 从组件类型创建签名
auto signature = ArchetypeSignature::create<Position, Velocity, Health>();

// 使用唯一 ID 创建 Archetype
Archetype archetype(1, signature);

// 检查组件成员
if (archetype.has_component<Position>()) {
    // ...
}
```

### 动态构建签名

```cpp
ArchetypeSignature signature;
signature.add<Position>();
signature.add<Velocity>();

// 移除组件类型
signature.remove<Velocity>();

// 检查包含关系
if (signature.contains<Position>()) {
    // ...
}
```

## 3. 实体操作

### 分配实体

```cpp
// 分配新的实体槽位
EntityLocation loc = archetype.allocate_entity();

// loc.chunk_index    - 实体所在的 Chunk 索引
// loc.index_in_chunk - 在该 Chunk 内的索引
```

### 设置组件值

```cpp
// 设置组件数据
archetype.set_component<Position>(loc, Position{10.0f, 20.0f, 0.0f});
archetype.set_component<Velocity>(loc, Velocity{1.0f, 0.0f, 0.0f});
archetype.set_component<Health>(loc, Health{80, 100});
```

### 读取组件值

```cpp
// 获取组件指针（如果类型不在 archetype 中则返回 nullptr）
Position* pos = archetype.get_component<Position>(loc);
if (pos) {
    pos->x += 1.0f;
}

// 常量访问
const Health* health = archetype.get_component<Health>(loc);
```

### 释放实体

系统使用 **swap-and-pop** 策略实现 O(1) 删除，同时保持数据紧凑：

```cpp
// deallocate 返回被移动实体的原始位置（如果有的话）
auto moved_from = archetype.deallocate_entity(loc);

if (moved_from.has_value()) {
    // 有实体从 moved_from 位置移动到了 loc 位置
    // 需要相应更新你的实体位置映射
}
```

## 4. 批量迭代（推荐）

为获得最佳性能，直接遍历 Chunk：

```cpp
// 物理更新示例
float delta_time = 1.0f / 60.0f;

for (auto& chunk : archetype.chunks()) {
    auto positions = chunk.get_components<Position>();
    auto velocities = chunk.get_components<Velocity>();
    
    // 处理该 chunk 中的所有实体
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        positions[i].x += velocities[i].vx * delta_time;
        positions[i].y += velocities[i].vy * delta_time;
        positions[i].z += velocities[i].vz * delta_time;
    }
}
```

### Chunk 迭代的优势

1. **缓存友好**：组件数组在内存中连续存储
2. **SIMD 就绪**：便于编译器自动向量化
3. **最小开销**：直接指针访问，无虚函数调用

## 5. 内存布局

### SoA（数组结构）

每个 Chunk 使用 SoA 布局以优化缓存利用率：

```
Chunk（16KB）：
┌─────────────────────────────────────────────────────┐
│ [Position_0][Position_1]...[Position_N]             │
├─────────────────────────────────────────────────────┤
│ [Velocity_0][Velocity_1]...[Velocity_N]             │
├─────────────────────────────────────────────────────┤
│ [Health_0][Health_1]...[Health_N]                   │
└─────────────────────────────────────────────────────┘
```

### 布局信息

```cpp
const auto& layout = archetype.layout();

// 每个 chunk 的实体容量（根据组件大小计算）
std::size_t capacity = layout.entities_per_chunk;

// 查找特定组件的布局
const ComponentLayout* pos_layout = layout.find_component<Position>();
if (pos_layout) {
    std::size_t offset = pos_layout->array_offset;
    std::size_t size = pos_layout->size;
    std::size_t alignment = pos_layout->alignment;
}
```

## 6. 完整示例

```cpp
#include "corona/kernel/ecs/archetype.h"

using namespace Corona::Kernel::ECS;

struct Position { float x, y, z; };
struct Velocity { float vx, vy, vz; };

int main() {
    // 注册组件
    CORONA_REGISTER_COMPONENT(Position);
    CORONA_REGISTER_COMPONENT(Velocity);
    
    // 创建 archetype
    auto sig = ArchetypeSignature::create<Position, Velocity>();
    Archetype archetype(1, sig);
    
    // 创建 1000 个实体
    std::vector<EntityLocation> entities;
    for (int i = 0; i < 1000; ++i) {
        auto loc = archetype.allocate_entity();
        archetype.set_component<Position>(loc, {float(i), 0.0f, 0.0f});
        archetype.set_component<Velocity>(loc, {1.0f, 0.0f, 0.0f});
        entities.push_back(loc);
    }
    
    // 游戏循环
    float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 100; ++frame) {
        // 更新所有实体
        for (auto& chunk : archetype.chunks()) {
            auto positions = chunk.get_components<Position>();
            auto velocities = chunk.get_components<Velocity>();
            
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                positions[i].x += velocities[i].vx * dt;
                positions[i].y += velocities[i].vy * dt;
                positions[i].z += velocities[i].vz * dt;
            }
        }
    }
    
    return 0;
}
```

## 7. 性能注意事项

### 推荐做法

- ✅ 使用 chunk 迭代进行批量更新
- ✅ 在创建 Archetype 之前注册组件
- ✅ 优先使用 trivially copyable 的组件以获得最佳性能
- ✅ 尽可能批量创建/删除实体

### 避免做法

- ❌ 在紧密循环中通过 EntityLocation 访问组件（应使用 chunk 迭代）
- ❌ 跨帧边界存储组件指针（可能会失效）
- ❌ 在跟踪实体位置时忘记处理 swap-and-pop

## 8. 线程安全

**当前状态**：Archetype **非线程安全**。以下操作需要外部同步：

- 实体分配/释放
- 组件修改

**安全操作**（在 Archetype 结构不变的情况下）：
- 对不同 Chunk 的并发只读迭代
- 读取组件值

## 9. 开发路线图

| 阶段 | 状态 | 功能 |
|------|------|------|
| Phase 1 | ✅ 已完成 | Component、Signature、Layout、Chunk、Archetype |
| Phase 2 | 🔲 计划中 | EntityId 生成、Entity-Location 映射、迁移 |
| Phase 3 | 🔲 计划中 | 查询系统、Archetype 关系图 |
| Phase 4 | 🔲 计划中 | World 类、System 集成 |

## 参考资料

- 设计文档：[doc/design/archetype_design.md](design/archetype_design.md)
- 测试代码：`tests/kernel/archetype_test.cpp`
- 头文件：`include/corona/kernel/ecs/`
