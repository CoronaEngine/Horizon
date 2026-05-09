# Entity 管理设计文档（Phase 2）

## 1. 概述

本文档描述 Corona Framework ECS 模块中 **Entity 管理** 的设计方案。Entity 管理是连接用户层和底层 Archetype 存储的桥梁，负责实体的创建、销毁、组件动态增删以及位置追踪。

### 1.1 设计目标

- **稳定的实体标识**：EntityId 在实体生命周期内保持不变
- **高效的位置查找**：O(1) 从 EntityId 到存储位置的映射
- **支持组件动态变更**：添加/移除组件时自动迁移实体到正确的 Archetype
- **ID 复用**：已销毁实体的 ID 可被安全回收
- **版本控制**：防止悬空引用访问已销毁实体

### 1.2 核心概念

| 术语 | 说明 |
|------|------|
| **EntityId** | 实体的唯一标识符，包含索引和版本号 |
| **EntityRecord** | 实体的元数据，记录所在 Archetype 和位置 |
| **EntityManager** | 管理实体生命周期和 ID 分配 |
| **Generation** | 版本号，用于检测悬空引用 |

---

## 2. EntityId 设计

### 2.1 ID 结构

采用 **索引 + 版本号** 的复合结构，使用 64 位整数编码：

```
EntityId (64 bits):
┌─────────────────────────────────┬─────────────────────────────────┐
│         Index (32 bits)         │       Generation (32 bits)      │
└─────────────────────────────────┴─────────────────────────────────┘
```

- **Index**：实体在 EntityRecord 数组中的索引
- **Generation**：版本号，每次 ID 被回收并重新分配时递增

### 2.2 类型定义

```cpp
namespace Corona::Kernel::ECS {

/// 实体 ID（索引 + 版本号）
class EntityId {
public:
    using IndexType = std::uint32_t;
    using GenerationType = std::uint32_t;
    
    /// 无效实体 ID
    static constexpr EntityId invalid() { 
        return EntityId(kInvalidIndex, 0); 
    }
    
    /// 默认构造（无效 ID）
    constexpr EntityId() : id_(kInvalidRawId) {}
    
    /// 从原始值构造
    constexpr explicit EntityId(std::uint64_t raw) : id_(raw) {}
    
    /// 从索引和版本号构造
    constexpr EntityId(IndexType index, GenerationType generation)
        : id_(pack(index, generation)) {}
    
    /// 获取索引部分
    [[nodiscard]] constexpr IndexType index() const {
        return static_cast<IndexType>(id_ & kIndexMask);
    }
    
    /// 获取版本号部分
    [[nodiscard]] constexpr GenerationType generation() const {
        return static_cast<GenerationType>(id_ >> 32);
    }
    
    /// 获取原始值
    [[nodiscard]] constexpr std::uint64_t raw() const { return id_; }
    
    /// 检查是否有效
    [[nodiscard]] constexpr bool is_valid() const {
        return index() != kInvalidIndex;
    }
    
    /// 比较运算符
    [[nodiscard]] constexpr bool operator==(const EntityId& other) const = default;
    [[nodiscard]] constexpr auto operator<=>(const EntityId& other) const = default;

private:
    static constexpr IndexType kInvalidIndex = std::numeric_limits<IndexType>::max();
    static constexpr std::uint64_t kInvalidRawId = 
        pack(kInvalidIndex, 0);
    static constexpr std::uint64_t kIndexMask = 0xFFFFFFFF;
    
    static constexpr std::uint64_t pack(IndexType index, GenerationType gen) {
        return static_cast<std::uint64_t>(gen) << 32 | index;
    }
    
    std::uint64_t id_;
};

} // namespace Corona::Kernel::ECS
```

### 2.3 版本号的作用

```cpp
// 场景：悬空引用检测
EntityId enemy = world.create_entity();  // index=5, gen=1
world.destroy_entity(enemy);              // ID 回收
EntityId npc = world.create_entity();     // index=5, gen=2 (复用索引)

// 旧引用已失效
world.is_alive(enemy);  // false (gen=1 != 当前 gen=2)
world.is_alive(npc);    // true
```

---

## 3. EntityRecord 设计

### 3.1 记录结构

每个实体槽位对应一条记录，存储实体的位置信息：

```cpp
namespace Corona::Kernel::ECS {

/// 实体记录（存储实体位置信息）
struct EntityRecord {
    ArchetypeId archetype_id = kInvalidArchetypeId;  ///< 所属 Archetype
    EntityLocation location = kInvalidEntityLocation; ///< 在 Archetype 内的位置
    EntityId::GenerationType generation = 0;          ///< 当前版本号
    bool alive = false;                               ///< 是否存活
    
    /// 检查记录是否有效
    [[nodiscard]] bool is_valid() const {
        return alive && archetype_id != kInvalidArchetypeId;
    }
    
    /// 重置记录
    void reset() {
        archetype_id = kInvalidArchetypeId;
        location = kInvalidEntityLocation;
        alive = false;
        // generation 不重置，保持递增
    }
};

} // namespace Corona::Kernel::ECS
```

### 3.2 存储布局

```
EntityRecord Array:
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ Record0 │ Record1 │ Record2 │ Record3 │ Record4 │   ...   │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
     ↑                   ↑
     │                   │
  alive=true          alive=false (可回收)
```

---

## 4. EntityManager 设计

### 4.1 职责

- 分配和回收 EntityId
- 维护 EntityId → EntityRecord 的映射
- 管理空闲 ID 列表
- 提供实体存活检查

### 4.2 接口设计

```cpp
namespace Corona::Kernel::ECS {

/// 实体管理器
class EntityManager {
public:
    EntityManager() = default;
    ~EntityManager() = default;
    
    // 禁止拷贝
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;
    
    // 支持移动
    EntityManager(EntityManager&&) noexcept = default;
    EntityManager& operator=(EntityManager&&) noexcept = default;
    
    // ========================================
    // 实体生命周期
    // ========================================
    
    /**
     * @brief 分配新的 EntityId
     * 
     * 优先从空闲列表中复用 ID，否则创建新槽位。
     * 
     * @return 新分配的 EntityId
     */
    [[nodiscard]] EntityId allocate();
    
    /**
     * @brief 释放 EntityId
     * 
     * 将 ID 加入空闲列表，递增版本号。
     * 
     * @param id 要释放的 EntityId
     * @return 释放成功返回 true
     */
    bool deallocate(EntityId id);
    
    /**
     * @brief 检查实体是否存活
     * @param id EntityId
     * @return 存活返回 true
     */
    [[nodiscard]] bool is_alive(EntityId id) const;
    
    // ========================================
    // 记录访问
    // ========================================
    
    /**
     * @brief 获取实体记录
     * @param id EntityId
     * @return 记录指针，无效 ID 返回 nullptr
     */
    [[nodiscard]] EntityRecord* get_record(EntityId id);
    [[nodiscard]] const EntityRecord* get_record(EntityId id) const;
    
    /**
     * @brief 更新实体位置
     * @param id EntityId
     * @param archetype_id 新的 Archetype ID
     * @param location 新的位置
     */
    void update_location(EntityId id, ArchetypeId archetype_id, 
                        const EntityLocation& location);
    
    // ========================================
    // 统计信息
    // ========================================
    
    /// 获取存活实体数量
    [[nodiscard]] std::size_t alive_count() const { return alive_count_; }
    
    /// 获取总容量
    [[nodiscard]] std::size_t capacity() const { return records_.size(); }
    
    /// 获取空闲槽位数量
    [[nodiscard]] std::size_t free_count() const { return free_list_.size(); }

private:
    /// 扩展记录数组
    void grow(std::size_t min_capacity);
    
    std::vector<EntityRecord> records_;         ///< 实体记录数组
    std::vector<EntityId::IndexType> free_list_; ///< 空闲索引列表
    std::size_t alive_count_ = 0;               ///< 存活实体计数
};

} // namespace Corona::Kernel::ECS
```

### 4.3 分配算法

```cpp
EntityId EntityManager::allocate() {
    EntityId::IndexType index;
    
    if (!free_list_.empty()) {
        // 复用空闲槽位
        index = free_list_.back();
        free_list_.pop_back();
    } else {
        // 创建新槽位
        index = static_cast<EntityId::IndexType>(records_.size());
        records_.emplace_back();
    }
    
    auto& record = records_[index];
    record.alive = true;
    // generation 保持不变（已在上次释放时递增）
    
    ++alive_count_;
    return EntityId(index, record.generation);
}
```

### 4.4 释放算法

```cpp
bool EntityManager::deallocate(EntityId id) {
    if (!is_alive(id)) {
        return false;
    }
    
    auto& record = records_[id.index()];
    record.reset();
    record.generation++;  // 递增版本号，使旧引用失效
    
    free_list_.push_back(id.index());
    --alive_count_;
    return true;
}
```

---

## 5. 实体迁移（Archetype 变更）

### 5.1 迁移场景

当实体的组件集合发生变化时，需要迁移到新的 Archetype：

```cpp
// 添加组件：从 Archetype(Position) 迁移到 Archetype(Position, Velocity)
world.add_component<Velocity>(entity, Velocity{1.0f, 0.0f, 0.0f});

// 移除组件：从 Archetype(Position, Velocity) 迁移到 Archetype(Position)
world.remove_component<Velocity>(entity);
```

### 5.2 迁移流程

```
1. 查找目标 Archetype（或创建新的）
2. 在目标 Archetype 分配新槽位
3. 拷贝共有组件数据
4. 初始化新增组件（添加时）
5. 从源 Archetype 释放旧槽位
6. 更新 EntityRecord
7. 处理源 Archetype 的 swap-and-pop 影响
```

### 5.3 迁移辅助函数

```cpp
namespace Corona::Kernel::ECS {

/// 实体迁移信息
struct MigrationInfo {
    EntityId entity;
    ArchetypeId source_archetype;
    EntityLocation source_location;
    ArchetypeId target_archetype;
    EntityLocation target_location;
};

/// 迁移回调（用于通知外部系统）
using MigrationCallback = std::function<void(const MigrationInfo&)>;

/// 执行实体迁移
/// @param entity_mgr 实体管理器
/// @param source 源 Archetype
/// @param target 目标 Archetype
/// @param entity_id 要迁移的实体
/// @param callback 迁移完成回调（可选）
/// @return 新位置
EntityLocation migrate_entity(
    EntityManager& entity_mgr,
    Archetype& source,
    Archetype& target,
    EntityId entity_id,
    const MigrationCallback& callback = nullptr
);

} // namespace Corona::Kernel::ECS
```

### 5.4 Swap-and-Pop 位置更新

当源 Archetype 释放实体时，可能触发 swap-and-pop，需要更新被移动实体的记录：

```cpp
// 释放实体后处理
auto moved_from = source.deallocate_entity(old_location);
if (moved_from.has_value()) {
    // 有实体被移动，需要找到它的 EntityId 并更新记录
    // 这需要反向查找：从位置找到 EntityId
}
```

### 5.5 反向映射（位置 → EntityId）

为支持 swap-and-pop 更新，每个 Chunk 需要存储实体 ID 数组：

```cpp
/// 扩展 Chunk，存储实体 ID
class Chunk {
    // ... 现有成员 ...
    
    /// 获取实体 ID 数组
    [[nodiscard]] std::span<EntityId> get_entity_ids();
    [[nodiscard]] std::span<const EntityId> get_entity_ids() const;
    
private:
    std::vector<EntityId> entity_ids_;  ///< 每个槽位对应的实体 ID
};
```

---

## 6. 文件组织

```
include/corona/kernel/ecs/
├── ecs_types.h           // EntityId 类定义
├── entity_record.h       // EntityRecord 结构
├── entity_manager.h      // EntityManager 类
└── migration.h           // 迁移相关函数

src/kernel/ecs/
├── entity_manager.cpp
└── migration.cpp
```

---

## 7. API 使用示例

### 7.1 基本操作

```cpp
EntityManager entity_mgr;

// 分配实体
EntityId e1 = entity_mgr.allocate();
EntityId e2 = entity_mgr.allocate();

// 检查存活
assert(entity_mgr.is_alive(e1));

// 更新位置
entity_mgr.update_location(e1, archetype_id, location);

// 获取记录
const EntityRecord* record = entity_mgr.get_record(e1);
if (record && record->is_valid()) {
    // 访问 Archetype 和位置
}

// 释放实体
entity_mgr.deallocate(e1);
assert(!entity_mgr.is_alive(e1));

// ID 复用（版本号不同）
EntityId e3 = entity_mgr.allocate();
assert(e3.index() == e1.index());      // 索引相同
assert(e3.generation() > e1.generation()); // 版本号更高
```

### 7.2 与 Archetype 集成

```cpp
// 创建实体并分配到 Archetype
EntityId entity = entity_mgr.allocate();
EntityLocation loc = archetype.allocate_entity();
entity_mgr.update_location(entity, archetype.id(), loc);

// 设置组件
archetype.set_component<Position>(loc, Position{0, 0, 0});

// 通过 EntityId 访问组件
const EntityRecord* record = entity_mgr.get_record(entity);
if (record && record->is_valid()) {
    Position* pos = archetype.get_component<Position>(record->location);
}
```

---

## 8. 性能考量

### 8.1 时间复杂度

| 操作 | 复杂度 | 说明 |
|-----|--------|------|
| allocate() | O(1) | 从空闲列表取或数组追加 |
| deallocate() | O(1) | 标记 + 加入空闲列表 |
| is_alive() | O(1) | 数组索引 + 版本号比较 |
| get_record() | O(1) | 数组索引 |
| 迁移 | O(组件数) | 拷贝共有组件 |

### 8.2 空间开销

- 每个实体槽位：~24 字节（EntityRecord）
- 每个 Chunk 槽位：+8 字节（EntityId 反向映射）

### 8.3 优化建议

1. **预分配容量**：已知实体数量时预分配 records_ 数组
2. **批量操作**：提供批量分配/释放接口减少开销
3. **延迟回收**：使用 deferred 列表批量处理释放

---

## 9. 线程安全

### 9.1 当前设计

EntityManager **非线程安全**，需要外部同步：
- allocate/deallocate：需要独占访问
- is_alive/get_record：可并发读取（无写入时）

### 9.2 未来扩展

```cpp
class EntityManager {
    // 读写锁保护
    mutable std::shared_mutex mutex_;
    
    // 原子操作优化
    std::atomic<std::size_t> alive_count_;
};
```

---

## 10. 测试计划

| 测试文件 | 测试内容 |
|---------|---------|
| `entity_id_test.cpp` | ID 编码/解码、版本号递增、比较运算 |
| `entity_manager_test.cpp` | 分配/释放、存活检查、ID 复用 |
| `migration_test.cpp` | 实体迁移、swap-and-pop 更新 |
| `entity_manager_mt_test.cpp` | 并发分配/释放压力测试 |

---

## 11. 实现优先级

### Phase 2.1：EntityId 和 EntityManager

- [ ] `EntityId` 类（索引 + 版本号）
- [ ] `EntityRecord` 结构
- [ ] `EntityManager` 基本功能

### Phase 2.2：Chunk 扩展

- [ ] Chunk 增加 EntityId 数组
- [ ] 反向映射查询

### Phase 2.3：实体迁移

- [ ] 迁移函数实现
- [ ] swap-and-pop 位置更新
- [ ] 迁移回调支持

---

## 12. 与 Phase 1 的集成点

| Phase 1 组件 | 修改内容 |
|-------------|---------|
| `ecs_types.h` | 更新 EntityId 为类类型 |
| `Chunk` | 增加 entity_ids_ 成员和访问方法 |
| `Archetype` | 分配/释放时接受 EntityId 参数 |

---

## 参考资料

- [Unity DOTS - Entity 管理](https://docs.unity3d.com/Packages/com.unity.entities@latest)
- [EnTT - 稀疏集合 Entity 存储](https://github.com/skypjack/entt)
- [Flecs - Entity ID 设计](https://github.com/SanderMertens/flecs)
