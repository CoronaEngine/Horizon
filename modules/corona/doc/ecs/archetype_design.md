# Archetype 数据结构设计文档

## 1. 概述

本文档描述 Corona Framework ECS（Entity Component System）模块中 **Archetype** 数据结构的设计方案。Archetype 是 ECS 架构中的核心数据结构，用于高效存储和管理具有相同组件组合的实体。

### 1.1 设计目标

- **高性能内存布局**：组件数据连续存储，支持缓存友好的遍历
- **类型安全**：利用 C++20 concepts 确保编译期类型检查
- **低开销查询**：O(1) 组件访问，O(组件数量) 的 Archetype 匹配
- **动态扩展**：支持运行时动态添加/移除实体
- **内存对齐**：组件数据按缓存行对齐，避免 false sharing

### 1.2 核心概念

| 术语 | 说明 |
|------|------|
| **Entity** | 实体，一个唯一 ID，不存储任何数据 |
| **Component** | 组件，纯数据结构（POD 或 trivially copyable） |
| **Archetype** | 原型，存储具有相同组件组合的所有实体 |
| **ComponentType** | 组件类型标识符（type_index + 元信息） |
| **Chunk** | 内存块，Archetype 内部的固定大小内存单元 |

---

## 2. 数据结构设计

### 2.1 组件类型标识

```cpp
namespace Corona::Kernel::ECS {

/// 组件类型 ID（基于 type_index 的哈希值）
using ComponentTypeId = std::size_t;

/// 组件类型约束
template <typename T>
concept Component = std::is_default_constructible_v<T> 
                 && std::is_move_constructible_v<T>
                 && std::is_destructible_v<T>;

/// 组件类型信息
struct ComponentTypeInfo {
    ComponentTypeId id;           ///< 类型唯一标识
    std::size_t size;             ///< sizeof(T)
    std::size_t alignment;        ///< alignof(T)
    std::string_view name;        ///< 类型名称（调试用）
    
    // 类型擦除的构造/析构/移动函数指针
    void (*construct)(void* dst);
    void (*destruct)(void* dst);
    void (*move_construct)(void* dst, void* src);
    void (*move_assign)(void* dst, void* src);
    void (*copy_construct)(void* dst, const void* src);
};

/// 获取组件类型信息的模板函数
template <Component T>
[[nodiscard]] const ComponentTypeInfo& get_component_type_info();

/// 获取组件类型 ID 的便捷函数
template <Component T>
[[nodiscard]] constexpr ComponentTypeId get_component_type_id() {
    return std::type_index(typeid(T)).hash_code();
}

} // namespace Corona::Kernel::ECS
```

### 2.2 Archetype 类型签名

Archetype 由一组排序后的 `ComponentTypeId` 唯一标识：

```cpp
namespace Corona::Kernel::ECS {

/// Archetype 类型签名（组件类型 ID 的有序集合）
class ArchetypeSignature {
public:
    ArchetypeSignature() = default;
    
    /// 从组件类型列表构造
    template <Component... Ts>
    static ArchetypeSignature create();
    
    /// 添加组件类型
    void add(ComponentTypeId type_id);
    
    /// 移除组件类型
    void remove(ComponentTypeId type_id);
    
    /// 检查是否包含某组件类型
    [[nodiscard]] bool contains(ComponentTypeId type_id) const;
    
    /// 检查是否包含所有指定的组件类型
    [[nodiscard]] bool contains_all(const ArchetypeSignature& other) const;
    
    /// 检查是否包含任一指定的组件类型
    [[nodiscard]] bool contains_any(const ArchetypeSignature& other) const;
    
    /// 获取组件数量
    [[nodiscard]] std::size_t size() const;
    
    /// 判断是否为空
    [[nodiscard]] bool empty() const;
    
    /// 获取哈希值（用于 Archetype 查找）
    [[nodiscard]] std::size_t hash() const;
    
    /// 比较运算符
    [[nodiscard]] bool operator==(const ArchetypeSignature& other) const;
    [[nodiscard]] auto operator<=>(const ArchetypeSignature& other) const;
    
    /// 迭代器支持
    [[nodiscard]] auto begin() const;
    [[nodiscard]] auto end() const;

private:
    std::vector<ComponentTypeId> type_ids_;  ///< 排序后的组件类型 ID
    std::size_t cached_hash_ = 0;            ///< 缓存的哈希值
};

} // namespace Corona::Kernel::ECS
```

### 2.3 Chunk（内存块）

Chunk 是 Archetype 内部的内存管理单元，固定大小（默认 16KB），存储多个实体的组件数据：

```cpp
namespace Corona::Kernel::ECS {

/// 默认 Chunk 大小（16KB，通常为 4 个内存页）
inline constexpr std::size_t kDefaultChunkSize = 16 * 1024;

/// Chunk 内存块
class Chunk {
public:
    /// 构造函数
    /// @param layout 组件布局信息
    /// @param capacity 实体容量
    explicit Chunk(const struct ArchetypeLayout& layout, std::size_t capacity);
    
    ~Chunk();
    
    // 禁止拷贝
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    
    // 支持移动
    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;
    
    /// 获取当前实体数量
    [[nodiscard]] std::size_t size() const { return count_; }
    
    /// 获取容量
    [[nodiscard]] std::size_t capacity() const { return capacity_; }
    
    /// 是否已满
    [[nodiscard]] bool is_full() const { return count_ >= capacity_; }
    
    /// 是否为空
    [[nodiscard]] bool is_empty() const { return count_ == 0; }
    
    /// 获取指定组件类型的数据数组起始指针
    /// @param type_id 组件类型 ID
    /// @return 组件数组指针，如果类型不存在返回 nullptr
    [[nodiscard]] void* get_component_array(ComponentTypeId type_id);
    [[nodiscard]] const void* get_component_array(ComponentTypeId type_id) const;
    
    /// 类型安全的组件数组访问
    template <Component T>
    [[nodiscard]] std::span<T> get_components();
    
    template <Component T>
    [[nodiscard]] std::span<const T> get_components() const;
    
    /// 在 Chunk 末尾分配一个新实体槽位
    /// @return 新实体在 Chunk 内的索引
    [[nodiscard]] std::size_t allocate();
    
    /// 释放指定索引的实体槽位（swap-and-pop 策略）
    /// @param index 要释放的实体索引
    /// @return 如果发生了交换，返回被移动的实体原索引；否则返回 nullopt
    std::optional<std::size_t> deallocate(std::size_t index);

private:
    std::byte* data_ = nullptr;              ///< 原始内存块
    std::size_t count_ = 0;                  ///< 当前实体数量
    std::size_t capacity_ = 0;               ///< 最大实体容量
    const ArchetypeLayout* layout_ = nullptr; ///< 组件布局（由 Archetype 持有）
};

} // namespace Corona::Kernel::ECS
```

### 2.4 Archetype 布局

描述 Archetype 内组件的内存布局：

```cpp
namespace Corona::Kernel::ECS {

/// 单个组件在 Chunk 内的布局信息
struct ComponentLayout {
    ComponentTypeId type_id;      ///< 组件类型 ID
    std::size_t offset;           ///< 在单个实体数据块内的偏移
    std::size_t size;             ///< 组件大小
    std::size_t alignment;        ///< 对齐要求
    const ComponentTypeInfo* type_info; ///< 类型信息指针
};

/// Archetype 内存布局
/// 
/// 采用 SoA（Structure of Arrays）布局：
/// [Entity0_CompA][Entity1_CompA]...[EntityN_CompA][Entity0_CompB][Entity1_CompB]...
struct ArchetypeLayout {
    std::vector<ComponentLayout> components;  ///< 各组件布局
    std::size_t entity_stride;                ///< 单个实体的总大小（对齐后）
    std::size_t entities_per_chunk;           ///< 每个 Chunk 可容纳的实体数
    
    /// 计算布局
    static ArchetypeLayout calculate(
        const ArchetypeSignature& signature,
        std::size_t chunk_size = kDefaultChunkSize
    );
    
    /// 查找组件布局
    [[nodiscard]] const ComponentLayout* find_component(ComponentTypeId type_id) const;
    
    /// 获取组件在 Chunk 内的数组偏移
    [[nodiscard]] std::size_t get_array_offset(
        ComponentTypeId type_id, 
        std::size_t entities_per_chunk
    ) const;
};

} // namespace Corona::Kernel::ECS
```

### 2.5 Archetype 主类

```cpp
namespace Corona::Kernel::ECS {

/// 实体在 Archetype 内的位置
struct EntityLocation {
    std::size_t chunk_index;   ///< Chunk 索引
    std::size_t index_in_chunk; ///< Chunk 内索引
};

/// Archetype ID 类型
using ArchetypeId = std::size_t;

/// Archetype - 存储具有相同组件组合的所有实体
class Archetype {
public:
    /// 构造函数
    /// @param id Archetype 唯一标识
    /// @param signature 组件类型签名
    explicit Archetype(ArchetypeId id, ArchetypeSignature signature);
    
    ~Archetype();
    
    // 禁止拷贝
    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;
    
    // 支持移动
    Archetype(Archetype&&) noexcept;
    Archetype& operator=(Archetype&&) noexcept;
    
    // ========================================
    // 基本信息
    // ========================================
    
    /// 获取 Archetype ID
    [[nodiscard]] ArchetypeId id() const { return id_; }
    
    /// 获取组件签名
    [[nodiscard]] const ArchetypeSignature& signature() const { return signature_; }
    
    /// 获取布局信息
    [[nodiscard]] const ArchetypeLayout& layout() const { return layout_; }
    
    /// 获取实体总数
    [[nodiscard]] std::size_t entity_count() const;
    
    /// 获取 Chunk 数量
    [[nodiscard]] std::size_t chunk_count() const { return chunks_.size(); }
    
    /// 检查是否包含指定组件类型
    [[nodiscard]] bool has_component(ComponentTypeId type_id) const;
    
    template <Component T>
    [[nodiscard]] bool has_component() const {
        return has_component(get_component_type_id<T>());
    }
    
    // ========================================
    // 实体管理
    // ========================================
    
    /// 分配一个新实体槽位
    /// @return 实体在 Archetype 内的位置
    [[nodiscard]] EntityLocation allocate_entity();
    
    /// 释放实体槽位
    /// @param location 实体位置
    /// @return 如果发生了 swap-and-pop，返回被移动实体的原位置
    std::optional<EntityLocation> deallocate_entity(const EntityLocation& location);
    
    // ========================================
    // 组件访问
    // ========================================
    
    /// 获取指定位置实体的组件指针
    /// @param location 实体位置
    /// @param type_id 组件类型 ID
    /// @return 组件指针，类型不存在返回 nullptr
    [[nodiscard]] void* get_component(const EntityLocation& location, ComponentTypeId type_id);
    [[nodiscard]] const void* get_component(const EntityLocation& location, ComponentTypeId type_id) const;
    
    /// 类型安全的组件访问
    template <Component T>
    [[nodiscard]] T* get_component(const EntityLocation& location) {
        return static_cast<T*>(get_component(location, get_component_type_id<T>()));
    }
    
    template <Component T>
    [[nodiscard]] const T* get_component(const EntityLocation& location) const {
        return static_cast<const T*>(get_component(location, get_component_type_id<T>()));
    }
    
    /// 设置组件值
    template <Component T>
    void set_component(const EntityLocation& location, T&& value) {
        T* ptr = get_component<T>(location);
        if (ptr) {
            *ptr = std::forward<T>(value);
        }
    }
    
    // ========================================
    // Chunk 访问（用于批量处理）
    // ========================================
    
    /// 获取指定索引的 Chunk
    [[nodiscard]] Chunk& get_chunk(std::size_t index);
    [[nodiscard]] const Chunk& get_chunk(std::size_t index) const;
    
    /// Chunk 迭代器
    [[nodiscard]] auto chunks_begin() { return chunks_.begin(); }
    [[nodiscard]] auto chunks_end() { return chunks_.end(); }
    [[nodiscard]] auto chunks_begin() const { return chunks_.cbegin(); }
    [[nodiscard]] auto chunks_end() const { return chunks_.cend(); }
    
    /// 范围遍历支持
    struct ChunkRange {
        std::vector<Chunk>::iterator begin_;
        std::vector<Chunk>::iterator end_;
        auto begin() { return begin_; }
        auto end() { return end_; }
    };
    
    struct ConstChunkRange {
        std::vector<Chunk>::const_iterator begin_;
        std::vector<Chunk>::const_iterator end_;
        auto begin() { return begin_; }
        auto end() { return end_; }
    };
    
    [[nodiscard]] ChunkRange chunks() { return {chunks_.begin(), chunks_.end()}; }
    [[nodiscard]] ConstChunkRange chunks() const { return {chunks_.cbegin(), chunks_.cend()}; }

private:
    /// 确保有可用的 Chunk 空间
    void ensure_capacity();
    
    /// 创建新的 Chunk
    Chunk& create_chunk();

private:
    ArchetypeId id_;                    ///< Archetype 唯一标识
    ArchetypeSignature signature_;      ///< 组件类型签名
    ArchetypeLayout layout_;            ///< 内存布局
    std::vector<Chunk> chunks_;         ///< Chunk 列表
};

} // namespace Corona::Kernel::ECS
```

---

## 3. 内存布局详解

### 3.1 SoA vs AoS

本设计采用 **SoA（Structure of Arrays）** 布局，而非 AoS（Array of Structures）：

**AoS 布局**（不采用）：
```
[Entity0: CompA, CompB, CompC][Entity1: CompA, CompB, CompC]...
```

**SoA 布局**（采用）：
```
Chunk 内部:
[CompA_0][CompA_1]...[CompA_N] | [CompB_0][CompB_1]...[CompB_N] | [CompC_0]...
```

### 3.2 SoA 布局优势

1. **缓存友好**：遍历单个组件类型时数据连续，Cache Line 利用率高
2. **SIMD 友好**：同类型数据连续存储，便于向量化操作
3. **灵活对齐**：每个组件数组可以独立对齐
4. **带宽优化**：System 只访问所需组件，不加载无关数据

### 3.3 Chunk 内存示意图

```
Chunk (16KB):
┌──────────────────────────────────────────────────────────────┐
│ Header (元数据，可选)                                          │
├──────────────────────────────────────────────────────────────┤
│ ComponentA Array [0..N-1]  (按 alignof(A) 对齐)               │
├──────────────────────────────────────────────────────────────┤
│ Padding (对齐填充)                                            │
├──────────────────────────────────────────────────────────────┤
│ ComponentB Array [0..N-1]  (按 alignof(B) 对齐)               │
├──────────────────────────────────────────────────────────────┤
│ ...                                                          │
└──────────────────────────────────────────────────────────────┘

N = entities_per_chunk (由 Chunk 大小和组件总大小决定)
```

---

## 4. API 设计

### 4.1 创建 Archetype

```cpp
// 方式 1：通过签名创建
auto signature = ArchetypeSignature::create<Position, Velocity, Health>();
auto archetype = std::make_unique<Archetype>(archetype_id, signature);

// 方式 2：动态构建签名
ArchetypeSignature signature;
signature.add(get_component_type_id<Position>());
signature.add(get_component_type_id<Velocity>());
```

### 4.2 实体操作

```cpp
// 分配实体
EntityLocation loc = archetype->allocate_entity();

// 设置组件
archetype->set_component<Position>(loc, Position{10.0f, 20.0f, 0.0f});
archetype->set_component<Velocity>(loc, Velocity{1.0f, 0.0f, 0.0f});

// 获取组件
Position* pos = archetype->get_component<Position>(loc);
if (pos) {
    pos->x += 1.0f;
}

// 释放实体
archetype->deallocate_entity(loc);
```

### 4.3 批量遍历

```cpp
// 遍历所有 Chunk
for (auto& chunk : archetype->chunks()) {
    auto positions = chunk.get_components<Position>();
    auto velocities = chunk.get_components<Velocity>();
    
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        positions[i].x += velocities[i].x * delta_time;
        positions[i].y += velocities[i].y * delta_time;
    }
}
```

---

## 5. 线程安全考量

详细设计见 [thread_safety_design.md](thread_safety_design.md)

### 5.1 当前设计

- **Archetype 实例**：非线程安全，需要外部同步
- **只读遍历**：多线程安全（Chunk 数据不变时）
- **写入操作**：需要独占访问或细粒度锁

### 5.2 未来扩展方向

1. **读写锁**：Archetype 级别的 shared_mutex
2. **Chunk 级锁**：更细粒度的并发控制
3. **无锁分配**：使用原子操作管理 Chunk 内索引
4. **延迟操作**：Command Buffer 模式批量处理结构变更

---

## 6. 文件组织

建议的文件结构：

```
include/corona/kernel/ecs/
├── component.h              // Component concept, ComponentTypeInfo
├── archetype_signature.h    // ArchetypeSignature
├── archetype_layout.h       // ArchetypeLayout, ComponentLayout
├── chunk.h                  // Chunk
├── archetype.h              // Archetype
└── ecs_types.h              // 公共类型定义 (EntityId, ArchetypeId, etc.)

src/kernel/ecs/
├── component.cpp
├── archetype_signature.cpp
├── archetype_layout.cpp
├── chunk.cpp
└── archetype.cpp
```

---

## 7. 依赖关系

```
ecs_types.h
    ↓
component.h  ←──────────────┐
    ↓                       │
archetype_signature.h       │
    ↓                       │
archetype_layout.h ─────────┤
    ↓                       │
chunk.h ────────────────────┘
    ↓
archetype.h
```

---

## 8. 测试计划

### 8.1 单元测试

| 测试文件 | 测试内容 |
|---------|---------|
| `component_test.cpp` | ComponentTypeInfo 生成、Component concept 验证 |
| `archetype_signature_test.cpp` | 签名创建、比较、哈希、包含检查 |
| `chunk_test.cpp` | 内存分配/释放、组件访问、swap-and-pop |
| `archetype_test.cpp` | 实体分配/释放、组件读写、Chunk 管理 |

### 8.2 多线程测试

| 测试文件 | 测试内容 |
|---------|---------|
| `archetype_mt_test.cpp` | 并发读取、压力测试 |

### 8.3 性能测试

| 测试文件 | 测试内容 |
|---------|---------|
| `archetype_perf_test.cpp` | 遍历性能、分配性能、缓存命中率 |

---

## 9. 实现优先级

### Phase 1：核心数据结构（本次实现）

- [x] `ComponentTypeInfo` 和 `get_component_type_info<T>()`
- [x] `ArchetypeSignature` 
- [x] `ArchetypeLayout`
- [x] `Chunk`
- [x] `Archetype`

### Phase 2：Entity 管理（后续）

详细设计见 [entity_manager_design.md](entity_manager_design.md)

- [ ] `EntityId` 生成与回收
- [ ] `EntityLocation` 映射表
- [ ] 实体跨 Archetype 迁移

### Phase 3：查询系统（后续）

详细设计见 [query_system_design.md](query_system_design.md)

- [ ] `ArchetypeGraph`（Archetype 关系图）
- [ ] `Query<T...>` 查询接口
- [ ] 查询缓存

### Phase 4：World 集成（后续）

详细设计见 [world_design.md](world_design.md)

- [ ] `World` 类（ECS 上下文）
- [ ] System 集成
- [ ] Command Buffer

---

## 10. 参考资料

- [Unity DOTS - Archetype 概念](https://docs.unity3d.com/Packages/com.unity.entities@latest)
- [EnTT - 稀疏集合实现](https://github.com/skypjack/entt)
- [Flecs - Archetype 存储](https://github.com/SanderMertens/flecs)
- [Data-Oriented Design - Mike Acton](https://www.dataorienteddesign.com/dodbook/)

---

## 附录 A：示例组件定义

```cpp
// 位置组件
struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// 速度组件
struct Velocity {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// 生命值组件
struct Health {
    int current = 100;
    int max = 100;
};

// 所有组件都满足 Component concept
static_assert(Component<Position>);
static_assert(Component<Velocity>);
static_assert(Component<Health>);
```

## 附录 B：性能预期

| 操作 | 时间复杂度 | 说明 |
|-----|-----------|------|
| 组件访问 | O(1) | 直接指针算术 |
| 实体分配 | O(1) 摊还 | 可能触发 Chunk 分配 |
| 实体释放 | O(组件数) | swap-and-pop |
| Archetype 匹配 | O(组件数) | 签名比较 |
| 全量遍历 | O(实体数) | 缓存友好的线性遍历 |
