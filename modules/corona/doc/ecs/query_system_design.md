# Query 系统设计文档（Phase 3）

## 1. 概述

本文档描述 Corona Framework ECS 模块中 **Query（查询）系统** 的设计方案。Query 是 ECS 中访问实体和组件的核心接口，允许用户按组件组合筛选实体并高效遍历。

### 1.1 设计目标

- **类型安全**：编译期检查查询的组件类型
- **高性能迭代**：直接访问 Archetype 存储，无中间拷贝
- **灵活过滤**：支持 With/Without/Optional 等过滤条件
- **缓存友好**：复用查询结果，避免重复匹配
- **简洁 API**：提供符合直觉的查询语法

### 1.2 核心概念

| 术语 | 说明 |
|------|------|
| **Query** | 查询对象，定义要访问的组件集合 |
| **Filter** | 过滤条件，进一步筛选匹配的 Archetype |
| **QueryResult** | 查询结果，包含匹配的 Archetype 列表 |
| **View** | 组件视图，提供对查询结果的迭代访问 |

---

## 2. Query 描述符

### 2.1 组件访问类型

```cpp
namespace Corona::Kernel::ECS {

/// 组件访问修饰符
template <Component T>
struct Read {
    using ComponentType = T;
    static constexpr bool is_read_only = true;
};

template <Component T>
struct Write {
    using ComponentType = T;
    static constexpr bool is_read_only = false;
};

template <Component T>
struct Optional {
    using ComponentType = T;
    static constexpr bool is_optional = true;
};

/// 过滤条件
template <Component T>
struct With {};  // 必须包含该组件（但不访问数据）

template <Component T>
struct Without {};  // 必须不包含该组件

} // namespace Corona::Kernel::ECS
```

### 2.2 Query 类型定义

```cpp
namespace Corona::Kernel::ECS {

/// Query 描述符（编译期）
template <typename... Components>
struct QueryDesc {
    /// 提取所有读取组件
    using ReadComponents = /* 过滤 Read<T> */;
    
    /// 提取所有写入组件
    using WriteComponents = /* 过滤 Write<T> */;
    
    /// 提取所有可选组件
    using OptionalComponents = /* 过滤 Optional<T> */;
    
    /// 提取 With 过滤条件
    using WithFilters = /* 过滤 With<T> */;
    
    /// 提取 Without 过滤条件
    using WithoutFilters = /* 过滤 Without<T> */;
    
    /// 获取必需组件签名（用于 Archetype 匹配）
    static ArchetypeSignature required_signature();
    
    /// 获取排除组件签名
    static ArchetypeSignature excluded_signature();
};

} // namespace Corona::Kernel::ECS
```

### 2.3 使用语法

```cpp
// 查询所有具有 Position 和 Velocity 的实体（读写 Position，只读 Velocity）
using MovementQuery = QueryDesc<Write<Position>, Read<Velocity>>;

// 带过滤条件的查询
using EnemyQuery = QueryDesc<
    Write<Position>,
    Read<Health>,
    With<EnemyTag>,      // 必须有 EnemyTag 组件
    Without<DeadTag>     // 不能有 DeadTag 组件
>;

// 可选组件
using RenderQuery = QueryDesc<
    Read<Position>,
    Read<Sprite>,
    Optional<Rotation>   // Rotation 可选
>;
```

---

## 3. QueryCache 设计

### 3.1 职责

- 缓存查询匹配的 Archetype 列表
- 当 Archetype 创建/销毁时自动更新缓存
- 避免重复的签名匹配计算

### 3.2 接口设计

```cpp
namespace Corona::Kernel::ECS {

/// 查询缓存条目
struct QueryCacheEntry {
    ArchetypeSignature required;              ///< 必需组件
    ArchetypeSignature excluded;              ///< 排除组件
    std::vector<Archetype*> matched_archetypes; ///< 匹配的 Archetype
    bool dirty = true;                        ///< 是否需要刷新
};

/// 查询缓存管理器
class QueryCache {
public:
    /// 查询 ID 类型
    using QueryId = std::size_t;
    
    /**
     * @brief 注册查询
     * @param required 必需组件签名
     * @param excluded 排除组件签名
     * @return 查询 ID
     */
    [[nodiscard]] QueryId register_query(
        const ArchetypeSignature& required,
        const ArchetypeSignature& excluded = {}
    );
    
    /**
     * @brief 获取匹配的 Archetype 列表
     * @param query_id 查询 ID
     * @return Archetype 指针列表
     */
    [[nodiscard]] std::span<Archetype* const> get_matched_archetypes(QueryId query_id);
    
    /**
     * @brief 通知新 Archetype 创建
     * @param archetype 新创建的 Archetype
     */
    void on_archetype_created(Archetype* archetype);
    
    /**
     * @brief 通知 Archetype 销毁
     * @param archetype 将要销毁的 Archetype
     */
    void on_archetype_destroyed(Archetype* archetype);
    
    /**
     * @brief 刷新所有脏缓存
     * @param archetypes 所有 Archetype 列表
     */
    void refresh(std::span<Archetype* const> archetypes);

private:
    std::vector<QueryCacheEntry> entries_;
    std::unordered_map<std::size_t, QueryId> signature_to_id_;  ///< 签名哈希 → ID
};

} // namespace Corona::Kernel::ECS
```

### 3.3 匹配算法

```cpp
bool matches_query(const Archetype& archetype, const QueryCacheEntry& entry) {
    const auto& sig = archetype.signature();
    
    // 必须包含所有必需组件
    if (!sig.contains_all(entry.required)) {
        return false;
    }
    
    // 不能包含任何排除组件
    if (sig.contains_any(entry.excluded)) {
        return false;
    }
    
    return true;
}
```

---

## 4. Query 执行器

### 4.1 Query 类

```cpp
namespace Corona::Kernel::ECS {

/// Query 执行器
template <typename... Components>
class Query {
public:
    using Descriptor = QueryDesc<Components...>;
    
    /// 构造函数
    explicit Query(QueryCache& cache, QueryCache::QueryId id)
        : cache_(&cache), query_id_(id) {}
    
    /// 遍历所有匹配的实体
    template <typename Func>
    void for_each(Func&& func);
    
    /// 遍历所有匹配的 Chunk
    template <typename Func>
    void for_each_chunk(Func&& func);
    
    /// 获取匹配的实体数量
    [[nodiscard]] std::size_t count() const;
    
    /// 检查是否为空
    [[nodiscard]] bool empty() const;

private:
    QueryCache* cache_;
    QueryCache::QueryId query_id_;
};

} // namespace Corona::Kernel::ECS
```

### 4.2 for_each 实现

```cpp
template <typename... Components>
template <typename Func>
void Query<Components...>::for_each(Func&& func) {
    auto archetypes = cache_->get_matched_archetypes(query_id_);
    
    for (Archetype* archetype : archetypes) {
        for (auto& chunk : archetype->chunks()) {
            if (chunk.is_empty()) continue;
            
            // 获取各组件数组
            auto [comp_ptrs...] = get_component_arrays<Components...>(chunk);
            
            // 遍历 Chunk 内所有实体
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                // 调用用户函数，传入组件引用
                func(get_component_ref<Components>(comp_ptrs, i)...);
            }
        }
    }
}
```

### 4.3 for_each_chunk 实现

```cpp
template <typename... Components>
template <typename Func>
void Query<Components...>::for_each_chunk(Func&& func) {
    auto archetypes = cache_->get_matched_archetypes(query_id_);
    
    for (Archetype* archetype : archetypes) {
        for (auto& chunk : archetype->chunks()) {
            if (chunk.is_empty()) continue;
            
            // 传入 Chunk 和组件 span
            func(chunk, chunk.get_components<typename Components::ComponentType>()...);
        }
    }
}
```

---

## 5. View（视图）

### 5.1 EntityView

提供对单个实体组件的访问：

```cpp
namespace Corona::Kernel::ECS {

/// 实体视图（只读）
template <typename... Components>
class EntityView {
public:
    EntityView(EntityId id, std::tuple<const Components*...> components)
        : id_(id), components_(components) {}
    
    [[nodiscard]] EntityId id() const { return id_; }
    
    template <Component T>
    [[nodiscard]] const T& get() const {
        return *std::get<const T*>(components_);
    }

private:
    EntityId id_;
    std::tuple<const Components*...> components_;
};

/// 实体视图（可变）
template <typename... Components>
class MutableEntityView {
public:
    // 类似实现，但返回非 const 引用
};

} // namespace Corona::Kernel::ECS
```

### 5.2 ChunkView

```cpp
namespace Corona::Kernel::ECS {

/// Chunk 视图
template <typename... Components>
class ChunkView {
public:
    ChunkView(Chunk& chunk) : chunk_(&chunk) {}
    
    [[nodiscard]] std::size_t size() const { return chunk_->size(); }
    
    template <Component T>
    [[nodiscard]] std::span<T> get() {
        return chunk_->get_components<T>();
    }
    
    template <Component T>
    [[nodiscard]] std::span<const T> get() const {
        return chunk_->get_components<T>();
    }

private:
    Chunk* chunk_;
};

} // namespace Corona::Kernel::ECS
```

---

## 6. API 使用示例

### 6.1 基本查询

```cpp
// 定义查询
auto query = world.query<Write<Position>, Read<Velocity>>();

// 遍历实体
query.for_each([](Position& pos, const Velocity& vel) {
    pos.x += vel.vx * delta_time;
    pos.y += vel.vy * delta_time;
});
```

### 6.2 带过滤条件

```cpp
// 查询所有存活的敌人
auto enemy_query = world.query<
    Write<Position>,
    Read<Health>,
    With<EnemyTag>,
    Without<DeadTag>
>();

enemy_query.for_each([](Position& pos, const Health& health) {
    // 处理敌人逻辑
});
```

### 6.3 Chunk 级遍历（SIMD 优化）

```cpp
auto physics_query = world.query<Write<Position>, Read<Velocity>>();

physics_query.for_each_chunk([](Chunk& chunk, 
                                std::span<Position> positions,
                                std::span<const Velocity> velocities) {
    // 可以使用 SIMD 优化
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        positions[i].x += velocities[i].vx * dt;
        positions[i].y += velocities[i].vy * dt;
    }
});
```

### 6.4 可选组件

```cpp
auto render_query = world.query<
    Read<Position>,
    Read<Sprite>,
    Optional<Rotation>
>();

render_query.for_each([](const Position& pos, 
                         const Sprite& sprite,
                         const Rotation* rotation) {  // 可能为 nullptr
    float angle = rotation ? rotation->angle : 0.0f;
    renderer.draw(sprite, pos, angle);
});
```

### 6.5 获取 EntityId

```cpp
auto query = world.query<Read<Position>>();

query.for_each_with_entity([](EntityId entity, const Position& pos) {
    std::cout << "Entity " << entity.index() << " at (" 
              << pos.x << ", " << pos.y << ")\n";
});
```

---

## 7. Archetype Graph（可选优化）

### 7.1 概念

Archetype Graph 记录 Archetype 之间的关系，加速组件添加/移除时的目标 Archetype 查找。

```
                    ┌─────────────────┐
                    │ (Position)      │
                    └────────┬────────┘
                             │ +Velocity
                             ▼
                    ┌─────────────────┐
                    │ (Position,      │
     +Health ◄──────│  Velocity)      │──────► -Velocity
                    └────────┬────────┘
                             │ +Health
                             ▼
                    ┌─────────────────┐
                    │ (Position,      │
                    │  Velocity,      │
                    │  Health)        │
                    └─────────────────┘
```

### 7.2 边缘缓存

```cpp
namespace Corona::Kernel::ECS {

/// Archetype 边缘（组件变更映射）
struct ArchetypeEdge {
    ComponentTypeId component_id;  ///< 变更的组件类型
    Archetype* add_target;         ///< 添加该组件后的目标 Archetype
    Archetype* remove_target;      ///< 移除该组件后的目标 Archetype
};

/// Archetype 图节点扩展
class ArchetypeNode {
public:
    /// 获取添加组件后的目标 Archetype
    [[nodiscard]] Archetype* get_add_target(ComponentTypeId comp_id) const;
    
    /// 获取移除组件后的目标 Archetype
    [[nodiscard]] Archetype* get_remove_target(ComponentTypeId comp_id) const;
    
    /// 设置边缘
    void set_edge(ComponentTypeId comp_id, Archetype* add_target, Archetype* remove_target);

private:
    std::unordered_map<ComponentTypeId, ArchetypeEdge> edges_;
};

} // namespace Corona::Kernel::ECS
```

---

## 8. 文件组织

```
include/corona/kernel/ecs/
├── query_desc.h        // QueryDesc, Read/Write/Optional/With/Without
├── query.h             // Query 类
├── query_cache.h       // QueryCache
├── view.h              // EntityView, ChunkView
└── archetype_graph.h   // ArchetypeGraph (可选)

src/kernel/ecs/
├── query_cache.cpp
└── archetype_graph.cpp
```

---

## 9. 性能考量

### 9.1 时间复杂度

| 操作 | 复杂度 | 说明 |
|-----|--------|------|
| 注册查询 | O(组件数) | 计算签名 |
| 获取匹配 Archetype | O(1) | 缓存命中 |
| for_each | O(实体数) | 线性遍历 |
| for_each_chunk | O(Chunk 数) | 批量遍历 |
| 刷新缓存 | O(Archetype 数 × 查询数) | 延迟执行 |

### 9.2 优化策略

1. **延迟匹配**：Archetype 创建时标记缓存为脏，实际访问时刷新
2. **增量更新**：仅对新增 Archetype 执行匹配，而非全量刷新
3. **并行迭代**：for_each_chunk 支持并行执行

---

## 10. 线程安全

### 10.1 当前设计

- **Query 创建**：需要同步（修改 QueryCache）
- **Query 迭代**：只读查询可并行，写入查询需要同步
- **Archetype 变更通知**：需要同步

### 10.2 并行 for_each

```cpp
template <typename... Components>
template <typename Func>
void Query<Components...>::parallel_for_each(Func&& func) {
    auto archetypes = cache_->get_matched_archetypes(query_id_);
    
    // 使用 TBB 或标准并行算法
    std::for_each(std::execution::par, 
                  archetypes.begin(), archetypes.end(),
                  [&](Archetype* archetype) {
        for (auto& chunk : archetype->chunks()) {
            // 处理 chunk...
        }
    });
}
```

---

## 11. 测试计划

| 测试文件 | 测试内容 |
|---------|---------|
| `query_desc_test.cpp` | 类型萃取、签名生成 |
| `query_cache_test.cpp` | 注册、匹配、缓存更新 |
| `query_test.cpp` | for_each、for_each_chunk、计数 |
| `query_filter_test.cpp` | With/Without/Optional 过滤 |
| `query_perf_test.cpp` | 大规模遍历性能 |

---

## 12. 实现优先级

### Phase 3.1：基础 Query

- [ ] `QueryDesc` 类型萃取
- [ ] `QueryCache` 基本实现
- [ ] `Query::for_each`

### Phase 3.2：高级功能

- [ ] With/Without/Optional 过滤
- [ ] `for_each_chunk` 批量接口
- [ ] `for_each_with_entity` 带 EntityId

### Phase 3.3：优化

- [ ] Archetype Graph（可选）
- [ ] 并行 for_each
- [ ] 增量缓存更新

---

## 参考资料

- [Unity DOTS - EntityQuery](https://docs.unity3d.com/Packages/com.unity.entities@latest)
- [EnTT - View 和 Group](https://github.com/skypjack/entt)
- [Flecs - Query](https://github.com/SanderMertens/flecs)
