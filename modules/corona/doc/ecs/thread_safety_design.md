# ECS 多线程安全设计文档

## 1. 概述

本文档描述 Corona Framework ECS 模块的多线程安全设计方案。核心设计目标是支持**多线程并行读写**场景，在保证数据一致性的前提下最大化并行性能。

### 1.1 核心并发场景

本设计针对以下真实使用场景：

1. **多线程并行执行**：多个 System/Job 在不同线程同时运行
2. **多写者并发**：写操作可能同时发生在多个线程
3. **并行遍历**：多个线程同时遍历实体和组件数据

### 1.2 设计原则

| 原则 | 说明 |
|-----|------|
| **分段/分桶** | 将全局数据结构分割，减少锁竞争（EntityManager 64 段，World 32 桶） |
| **Per-Chunk 锁** | Chunk 级别细粒度锁，不同 Chunk 完全并行 |
| **版本快照** | 遍历时获取快照，不阻塞结构变更 |
| **无锁组件访问** | 不同实体的组件访问完全无锁 |
| **实体级协调** | 同一实体的并发写入使用原子操作或轻量级锁 |
| **延迟结构变更** | 通过 CommandBuffer 延迟执行实体创建/销毁 |

### 1.3 设计目标

- **多写者安全**：支持多线程同时写入不同实体的组件
- **细粒度锁**：Chunk 级别锁，最大化并行度
- **无锁数据访问**：组件数据读写使用原子操作或分区策略
- **结构变更隔离**：通过双缓冲/快照机制隔离结构变更
- **零拷贝遍历**：遍历期间不复制数据，使用版本控制保证一致性

### 1.4 并发场景矩阵

| 场景 | 并发类型 | 策略 | 锁粒度 |
|-----|---------|------|--------|
| 多线程读组件 | 多读 | 无锁 | 无 |
| 多线程写不同实体 | 多写（不相交） | 无锁/原子 | 无 |
| 多线程写同一实体 | 多写（相交） | 原子组件或实体锁 | 实体 |
| 多线程同时遍历 | 多读 | 版本快照 | 无 |
| 遍历 + 组件写入 | 读写混合 | 版本快照 | Archetype |
| 遍历 + 结构变更 | 读写混合 | 双缓冲 CommandBuffer | World |
| 多线程创建实体 | 多写 | 分段锁 + 无锁队列 | EntityManager 分段 |
| 多线程销毁实体 | 多写 | CommandBuffer | 延迟执行 |
| 多线程创建 Archetype | 多写 | 分桶锁 | World 分桶 |

---

## 2. 分层并发模型

### 2.1 层次结构

```
┌─────────────────────────────────────────────────────────────────────┐
│                      World (分段读写锁)                              │
│  - Archetype 表使用分段锁（按 hash 分桶）                            │
│  - 支持多线程同时创建不同 Archetype                                  │
├─────────────────────────────────────────────────────────────────────┤
│              EntityManager (分段锁 + 无锁空闲列表)                    │
│  - ID 分配完全无锁（原子操作 + 无锁队列）                            │
│  - Record 按 index 分段，减少锁竞争                                  │
├─────────────────────────────────────────────────────────────────────┤
│                 Archetype (读写锁 + 版本号)                          │
│  - Chunk 列表修改需要写锁                                            │
│  - 遍历使用版本快照，不阻塞写入                                      │
├─────────────────────────────────────────────────────────────────────┤
│                   Chunk (Per-Chunk 锁)                               │
│  - 每个 Chunk 独立的自旋锁（仅用于结构变更）                          │
│  - 组件数据访问无锁（不同实体可完全并行）                            │
├─────────────────────────────────────────────────────────────────────┤
│                 Component Data (原子/无锁)                           │
│  - 不同实体的组件：完全并行，无同步                                  │
│  - 同一实体的组件：原子操作或调用者协调                              │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 多写者访问规则

| 操作类型 | 同一实体 | 不同实体（同 Chunk） | 不同 Chunk |
|---------|---------|---------------------|-----------|
| 读组件 | 并行 | 并行 | 并行 |
| 写组件 | 原子/协调 | 并行 | 并行 |
| 分配实体 | N/A | Chunk 锁 | 并行 |
| 释放实体 | N/A | Chunk 锁 | 并行 |

### 2.3 版本控制机制

每个结构层次维护版本号，用于检测并发修改：

```cpp
struct VersionedData {
    std::atomic<uint64_t> version{0};
    
    // 开始写入前递增（奇数 = 写入中）
    void begin_write() { version.fetch_add(1, std::memory_order_release); }
    
    // 写入完成后递增（偶数 = 稳定）
    void end_write() { version.fetch_add(1, std::memory_order_release); }
    
    // 检查是否正在写入
    bool is_writing() const { return version.load(std::memory_order_acquire) & 1; }
    
    // 获取稳定版本（等待写入完成）
    uint64_t get_stable_version() const {
        uint64_t v;
        do {
            v = version.load(std::memory_order_acquire);
        } while (v & 1);  // 等待写入完成
        return v;
    }
};
```

---

## 3. EntityManager 线程安全（多写者）

### 3.1 分段锁设计

为支持多线程同时分配/释放实体，使用分段锁减少竞争：

```cpp
class EntityManager {
public:
    static constexpr std::size_t kNumSegments = 64;  // 分段数
    static constexpr std::size_t kSegmentSize = 4096; // 每段预分配大小
    
    /// 多线程安全的 ID 分配
    [[nodiscard]] EntityId allocate() {
        // 策略1：优先从线程本地空闲列表获取
        auto& local_free = get_thread_local_free_list();
        EntityId::IndexType index;
        if (local_free.try_pop(index)) {
            return activate_entity(index);
        }
        
        // 策略2：从全局空闲列表批量获取
        if (global_free_list_.try_pop_batch(local_free, kBatchSize)) {
            if (local_free.try_pop(index)) {
                return activate_entity(index);
            }
        }
        
        // 策略3：分配新的段
        return allocate_from_new_segment();
    }
    
    /// 多线程安全的 ID 释放
    bool deallocate(EntityId id) {
        if (!is_alive(id)) return false;
        
        auto segment_idx = id.index() / kSegmentSize;
        auto& segment = segments_[segment_idx];
        
        // 使用分段锁保护
        std::lock_guard lock(segment.mutex);
        
        auto& record = segment.records[id.index() % kSegmentSize];
        record.alive.store(false, std::memory_order_release);
        record.generation.fetch_add(1, std::memory_order_relaxed);
        
        // 返回到线程本地空闲列表
        get_thread_local_free_list().push(id.index());
        alive_count_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    
    /// 无锁存活检查
    [[nodiscard]] bool is_alive(EntityId id) const {
        auto segment_idx = id.index() / kSegmentSize;
        if (segment_idx >= segments_.size()) return false;
        
        auto& record = segments_[segment_idx].records[id.index() % kSegmentSize];
        return record.alive.load(std::memory_order_acquire) &&
               record.generation.load(std::memory_order_acquire) == id.generation();
    }

private:
    struct Segment {
        SpinLock mutex;  // 轻量级自旋锁
        std::array<AtomicEntityRecord, kSegmentSize> records;
        std::atomic<std::size_t> next_free{0};
    };
    
    EntityId activate_entity(EntityId::IndexType index) {
        auto segment_idx = index / kSegmentSize;
        auto& record = segments_[segment_idx].records[index % kSegmentSize];
        record.alive.store(true, std::memory_order_release);
        alive_count_.fetch_add(1, std::memory_order_relaxed);
        return EntityId(index, record.generation.load(std::memory_order_acquire));
    }
    
    EntityId allocate_from_new_segment() {
        // 原子分配新段
        auto segment_idx = next_segment_.fetch_add(1, std::memory_order_relaxed);
        
        // 延迟初始化段
        if (segment_idx >= segments_.size()) {
            std::lock_guard lock(segments_mutex_);
            while (segments_.size() <= segment_idx) {
                segments_.emplace_back(std::make_unique<Segment>());
            }
        }
        
        auto& segment = *segments_[segment_idx];
        auto local_idx = segment.next_free.fetch_add(1, std::memory_order_relaxed);
        auto global_idx = segment_idx * kSegmentSize + local_idx;
        
        return activate_entity(global_idx);
    }
    
    // 线程本地空闲列表
    static LockFreeQueue<EntityId::IndexType>& get_thread_local_free_list() {
        thread_local LockFreeQueue<EntityId::IndexType> local_free;
        return local_free;
    }
    
    std::vector<std::unique_ptr<Segment>> segments_;
    std::mutex segments_mutex_;  // 仅保护段扩展
    LockFreeQueue<EntityId::IndexType> global_free_list_;
    std::atomic<std::size_t> next_segment_{0};
    std::atomic<std::size_t> alive_count_{0};
    static constexpr std::size_t kBatchSize = 64;
};
```

### 3.2 原子 EntityRecord（支持并发更新）

```cpp
/// 线程安全的实体记录（支持多写者）
struct AtomicEntityRecord {
    std::atomic<ArchetypeId> archetype_id{kInvalidArchetypeId};
    std::atomic<std::size_t> chunk_index{0};
    std::atomic<std::size_t> index_in_chunk{0};
    std::atomic<EntityId::GenerationType> generation{0};
    std::atomic<bool> alive{false};
    std::atomic<uint32_t> version{0};  // 用于检测并发更新
    
    /// 原子更新位置（CAS 保证一致性）
    bool update_location(ArchetypeId arch_id, const EntityLocation& loc) {
        // 使用版本号实现乐观锁
        auto old_version = version.load(std::memory_order_acquire);
        
        // 存储新位置
        archetype_id.store(arch_id, std::memory_order_relaxed);
        chunk_index.store(loc.chunk_index, std::memory_order_relaxed);
        index_in_chunk.store(loc.index_in_chunk, std::memory_order_relaxed);
        
        // CAS 更新版本号
        return version.compare_exchange_strong(
            old_version, old_version + 1,
            std::memory_order_release,
            std::memory_order_relaxed);
    }
    
    /// 读取位置（带版本验证）
    [[nodiscard]] std::optional<std::pair<EntityLocation, uint32_t>> 
    try_get_location() const {
        auto v1 = version.load(std::memory_order_acquire);
        if (v1 & 1) return std::nullopt;  // 正在更新
        
        EntityLocation loc{
            chunk_index.load(std::memory_order_relaxed),
            index_in_chunk.load(std::memory_order_relaxed)
        };
        
        auto v2 = version.load(std::memory_order_acquire);
        if (v1 != v2) return std::nullopt;  // 读取期间被修改
        
        return std::make_pair(loc, v1);
    }
};
```

---

## 4. Archetype 线程安全（并行遍历 + 多写者）

### 4.1 Per-Chunk 锁设计

为支持多线程同时操作不同 Chunk，使用 Per-Chunk 细粒度锁：

```cpp
class Archetype {
public:
    /// 多线程安全的实体分配（Per-Chunk 锁）
    [[nodiscard]] EntityLocation allocate_entity() {
        // 策略1：尝试在现有 Chunk 中分配（无 Archetype 锁）
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            auto& chunk = *chunks_[i];
            if (auto idx = chunk.try_allocate()) {  // 内部使用 Chunk 锁
                return EntityLocation{i, *idx};
            }
        }
        
        // 策略2：需要新 Chunk（获取 Archetype 写锁）
        std::unique_lock lock(structure_mutex_);
        
        // 双重检查
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            if (auto idx = chunks_[i]->try_allocate()) {
                return EntityLocation{i, *idx};
            }
        }
        
        // 创建新 Chunk
        auto chunk_idx = chunks_.size();
        chunks_.push_back(create_chunk());
        version_.fetch_add(1, std::memory_order_release);
        
        auto idx = chunks_.back()->try_allocate();
        return EntityLocation{chunk_idx, *idx};
    }
    
    /// 多线程安全的实体释放
    std::optional<EntityId> deallocate_entity(const EntityLocation& loc) {
        // 仅锁定目标 Chunk
        auto& chunk = *chunks_[loc.chunk_index];
        return chunk.deallocate_with_swap(loc.index_in_chunk);
    }
    
    /// 组件访问（无锁）
    template <Component T>
    [[nodiscard]] T* get_component(const EntityLocation& loc) {
        return chunks_[loc.chunk_index]->get_component_at<T>(loc.index_in_chunk);
    }
    
    /// 并行安全的遍历（版本快照）
    template <typename Func>
    void parallel_for_each_chunk(Func&& func) {
        // 获取结构快照
        auto snapshot = get_chunk_snapshot();
        
        // 并行处理（无锁）
        tbb::parallel_for_each(snapshot.begin(), snapshot.end(),
            [&func](ChunkSnapshot& snap) {
                func(snap);
            });
    }
    
    /// 获取 Chunk 快照（用于安全遍历）
    [[nodiscard]] std::vector<ChunkSnapshot> get_chunk_snapshot() const {
        std::shared_lock lock(structure_mutex_);
        std::vector<ChunkSnapshot> result;
        result.reserve(chunks_.size());
        
        for (const auto& chunk : chunks_) {
            result.push_back(chunk->create_snapshot());
        }
        return result;
    }
    
    /// 获取结构版本号（用于检测变更）
    [[nodiscard]] uint64_t get_version() const {
        return version_.load(std::memory_order_acquire);
    }

private:
    mutable std::shared_mutex structure_mutex_;  // 仅保护 Chunk 列表结构
    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::atomic<uint64_t> version_{0};
};
```

### 4.2 Chunk 级并发控制

```cpp
class Chunk {
public:
    /// 尝试分配（多线程安全）
    [[nodiscard]] std::optional<std::size_t> try_allocate() {
        std::lock_guard lock(mutex_);
        if (size_ >= capacity_) return std::nullopt;
        return size_++;
    }
    
    /// 释放并交换（多线程安全，返回被移动实体的 ID）
    std::optional<EntityId> deallocate_with_swap(std::size_t index) {
        std::lock_guard lock(mutex_);
        if (index >= size_) return std::nullopt;
        
        auto last_idx = --size_;
        if (index != last_idx) {
            // 交换数据
            swap_entities(index, last_idx);
            // 返回被移动实体的 ID（调用者需要更新其位置）
            return get_entity_id(index);
        }
        return std::nullopt;
    }
    
    /// 创建快照（用于安全遍历）
    [[nodiscard]] ChunkSnapshot create_snapshot() const {
        std::shared_lock lock(mutex_);
        return ChunkSnapshot{
            .chunk = this,
            .size = size_,
            .version = version_
        };
    }
    
    /// 组件数据访问（无锁，但需要保证索引有效）
    template <Component T>
    [[nodiscard]] T* get_component_array() {
        return static_cast<T*>(get_component_array_impl(ComponentTypeId::of<T>()));
    }
    
    /// 原子标记（用于并行写入协调）
    [[nodiscard]] bool try_lock_entity(std::size_t index) {
        return !entity_locks_[index].test_and_set(std::memory_order_acquire);
    }
    
    void unlock_entity(std::size_t index) {
        entity_locks_[index].clear(std::memory_order_release);
    }

private:
    mutable std::shared_mutex mutex_;  // 保护结构变更
    std::size_t size_{0};
    std::size_t capacity_{0};
    uint64_t version_{0};
    
    // 每个实体的轻量级锁（用于同一实体的并发写入保护）
    std::vector<std::atomic_flag> entity_locks_;
};

/// Chunk 快照（用于安全遍历）
struct ChunkSnapshot {
    const Chunk* chunk;
    std::size_t size;     // 快照时的大小
    uint64_t version;     // 快照时的版本
    
    /// 检查快照是否仍然有效
    [[nodiscard]] bool is_valid() const {
        return chunk->get_version() == version;
    }
};
```

### 4.3 并行遍历 + 并行写入

支持多个线程同时遍历并写入组件：

```cpp
/// 并行遍历示例（多线程同时读写）
void parallel_process_chunks(Archetype& archetype) {
    auto snapshots = archetype.get_chunk_snapshot();
    
    tbb::parallel_for_each(snapshots.begin(), snapshots.end(), 
        [](ChunkSnapshot& snap) {
            auto* chunk = const_cast<Chunk*>(snap.chunk);
            auto positions = chunk->get_component_array<Position>();
            auto velocities = chunk->get_component_array<Velocity>();
            
            // 不同实体可以并行写入（无锁）
            for (std::size_t i = 0; i < snap.size; ++i) {
                positions[i].x += velocities[i].vx;
                positions[i].y += velocities[i].vy;
            }
        });
}

/// 同一实体的并发写入保护
void update_entity_safe(Chunk& chunk, std::size_t index) {
    // 获取实体级锁
    if (chunk.try_lock_entity(index)) {
        auto guard = ScopeGuard([&] { chunk.unlock_entity(index); });
        
        // 安全修改
        auto* health = chunk.get_component_at<Health>(index);
        health->current -= 10;
    }
    // 锁定失败可以跳过或重试
}
```

---

## 5. World 线程安全（分段锁 + 并行遍历）

### 5.1 分段锁设计

为支持多线程同时创建不同 Archetype 和并行遍历，使用分段锁：

```cpp
class World {
public:
    static constexpr std::size_t kNumBuckets = 32;  // 分桶数
    
    /// 多线程安全的实体创建
    [[nodiscard]] EntityId create_entity() {
        return entity_manager_.allocate();
    }
    
    /// 多线程安全的 Archetype 获取或创建（分桶锁）
    Archetype& get_or_create_archetype(const ArchetypeSignature& sig) {
        auto hash = sig.hash();
        auto bucket_idx = hash % kNumBuckets;
        auto& bucket = buckets_[bucket_idx];
        
        // 先尝试读锁查找
        {
            std::shared_lock lock(bucket.mutex);
            auto it = bucket.archetypes.find(hash);
            if (it != bucket.archetypes.end()) {
                return *it->second;
            }
        }
        
        // 升级到写锁创建（仅锁定一个桶）
        std::unique_lock lock(bucket.mutex);
        
        // 双重检查
        auto it = bucket.archetypes.find(hash);
        if (it != bucket.archetypes.end()) {
            return *it->second;
        }
        
        auto id = next_archetype_id_.fetch_add(1, std::memory_order_relaxed);
        auto archetype = std::make_unique<Archetype>(id, sig);
        auto* ptr = archetype.get();
        bucket.archetypes[hash] = std::move(archetype);
        
        // 原子更新全局列表（用于遍历）
        {
            std::lock_guard global_lock(all_archetypes_mutex_);
            all_archetypes_.push_back(ptr);
        }
        
        // 异步通知查询缓存
        query_cache_.on_archetype_created_async(ptr);
        
        return *ptr;
    }
    
    /// 并行安全的 Query（使用快照）
    template <typename... Components>
    [[nodiscard]] Query<Components...> query() {
        return Query<Components...>(&query_cache_);
    }
    
    /// 获取所有 Archetype 的快照
    [[nodiscard]] std::vector<Archetype*> get_archetype_snapshot() const {
        std::shared_lock lock(all_archetypes_mutex_);
        return all_archetypes_;
    }

private:
    struct Bucket {
        std::shared_mutex mutex;
        std::unordered_map<std::size_t, std::unique_ptr<Archetype>> archetypes;
    };
    
    std::array<Bucket, kNumBuckets> buckets_;
    mutable std::shared_mutex all_archetypes_mutex_;
    std::vector<Archetype*> all_archetypes_;  // 用于快速遍历
    std::atomic<ArchetypeId> next_archetype_id_{0};
    EntityManager entity_manager_;
    QueryCache query_cache_;
};
```

### 5.2 QueryCache 并行更新

```cpp
class QueryCache {
public:
    /// 异步通知新 Archetype（无锁队列）
    void on_archetype_created_async(Archetype* archetype) {
        pending_archetypes_.push(archetype);
    }
    
    /// 获取匹配的 Archetype（带惰性更新）
    std::vector<Archetype*> get_matched_archetypes(QueryId id) {
        // 处理待更新的 Archetype
        process_pending();
        
        std::shared_lock lock(cache_mutex_);
        auto it = cache_.find(id);
        if (it != cache_.end()) {
            return it->second;
        }
        return {};
    }

private:
    void process_pending() {
        Archetype* arch;
        while (pending_archetypes_.try_pop(arch)) {
            std::unique_lock lock(cache_mutex_);
            // 检查所有已注册的 Query
            for (auto& [id, archetypes] : cache_) {
                if (matches_query(arch, queries_[id])) {
                    archetypes.push_back(arch);
                }
            }
        }
    }
    
    LockFreeQueue<Archetype*> pending_archetypes_;
    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<QueryId, std::vector<Archetype*>> cache_;
    std::unordered_map<QueryId, QueryDesc> queries_;
};
```

### 5.3 并行友好的 Query（支持多写者）

```cpp
template <typename... Components>
class Query {
public:
    /// 并行 for_each（支持多线程同时写入）
    template <typename Func>
    void parallel_for_each(Func&& func) {
        auto archetypes = cache_->get_matched_archetypes(query_id_);
        
        // 收集所有 Chunk 快照
        std::vector<ChunkSnapshot> work_items;
        for (Archetype* arch : archetypes) {
            auto snapshots = arch->get_chunk_snapshot();
            work_items.insert(work_items.end(), snapshots.begin(), snapshots.end());
        }
        
        // 完全并行处理（不同 Chunk 无竞争）
        tbb::parallel_for_each(work_items.begin(), work_items.end(),
            [&func](ChunkSnapshot& snap) {
                auto* chunk = const_cast<Chunk*>(snap.chunk);
                auto arrays = std::make_tuple(
                    chunk->get_component_array<Components>()...
                );
                
                // 不同实体可完全并行写入
                for (std::size_t i = 0; i < snap.size; ++i) {
                    func(std::get<ComponentIndex<Components>>(arrays)[i]...);
                }
            });
    }
    
    /// 带实体 ID 的并行遍历
    template <typename Func>
    void parallel_for_each_with_entity(Func&& func) {
        auto archetypes = cache_->get_matched_archetypes(query_id_);
        
        std::vector<ChunkSnapshot> work_items;
        for (Archetype* arch : archetypes) {
            auto snapshots = arch->get_chunk_snapshot();
            work_items.insert(work_items.end(), snapshots.begin(), snapshots.end());
        }
        
        tbb::parallel_for_each(work_items.begin(), work_items.end(),
            [&func](ChunkSnapshot& snap) {
                auto* chunk = const_cast<Chunk*>(snap.chunk);
                auto entity_ids = chunk->get_entity_id_array();
                auto arrays = std::make_tuple(
                    chunk->get_component_array<Components>()...
                );
                
                for (std::size_t i = 0; i < snap.size; ++i) {
                    func(entity_ids[i], 
                         std::get<ComponentIndex<Components>>(arrays)[i]...);
                }
            });
    }
    
    /// 分区并行（细粒度控制）
    template <typename Func>
    void partitioned_for_each(Func&& func, std::size_t partition_size = 256) {
        auto archetypes = cache_->get_matched_archetypes(query_id_);
        
        // 扁平化所有工作项
        struct WorkItem {
            Chunk* chunk;
            std::size_t start;
            std::size_t end;
        };
        std::vector<WorkItem> work_items;
        
        for (Archetype* arch : archetypes) {
            for (auto& snap : arch->get_chunk_snapshot()) {
                std::size_t size = snap.size;
                for (std::size_t start = 0; start < size; start += partition_size) {
                    work_items.push_back({
                        const_cast<Chunk*>(snap.chunk),
                        start,
                        std::min(start + partition_size, size)
                    });
                }
            }
        }
        
        // 细粒度并行
        tbb::parallel_for_each(work_items.begin(), work_items.end(),
            [&func](WorkItem& item) {
                auto arrays = std::make_tuple(
                    item.chunk->get_component_array<Components>()...
                );
                
                for (std::size_t i = item.start; i < item.end; ++i) {
                    func(std::get<ComponentIndex<Components>>(arrays)[i]...);
                }
            });
    }
};
```

---

## 6. CommandBuffer 并行设计（多写者支持）

### 6.1 线程本地 CommandBuffer

```cpp
/// 线程本地命令缓冲
class ThreadLocalCommandBuffer {
public:
    /// 获取当前线程的缓冲区
    static CommandBuffer& current() {
        thread_local CommandBuffer buffer;
        return buffer;
    }
};

/// 并行安全的命令缓冲管理器
class ParallelCommandBufferManager {
public:
    /// 注册线程的缓冲区
    void register_buffer(CommandBuffer* buffer) {
        std::lock_guard lock(mutex_);
        buffers_.push_back(buffer);
    }
    
    /// 合并所有缓冲区并执行
    void flush_all(World& world) {
        std::lock_guard lock(mutex_);
        
        // 先收集所有命令
        std::vector<std::unique_ptr<Command>> all_commands;
        for (auto* buffer : buffers_) {
            buffer->drain_to(all_commands);
        }
        
        // 排序（可选：按类型或优先级）
        sort_commands(all_commands);
        
        // 顺序执行（结构变更不能并行）
        for (auto& cmd : all_commands) {
            cmd->execute(world);
        }
    }

private:
    std::mutex mutex_;
    std::vector<CommandBuffer*> buffers_;
};
```

### 6.2 无锁命令队列

```cpp
/// 无锁命令缓冲区（MPSC: 多生产者单消费者）
class LockFreeCommandBuffer {
public:
    /// 添加命令（多线程安全）
    template <typename Cmd, typename... Args>
    void emplace(Args&&... args) {
        auto* node = new CommandNode{
            std::make_unique<Cmd>(std::forward<Args>(args)...),
            nullptr
        };
        
        // CAS 插入到链表头
        CommandNode* old_head = head_.load(std::memory_order_relaxed);
        do {
            node->next = old_head;
        } while (!head_.compare_exchange_weak(
            old_head, node,
            std::memory_order_release,
            std::memory_order_relaxed));
        
        count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    /// 执行所有命令（单线程）
    void flush(World& world) {
        // 原子交换获取所有命令
        CommandNode* node = head_.exchange(nullptr, std::memory_order_acquire);
        
        // 反转链表（恢复插入顺序）
        CommandNode* reversed = nullptr;
        while (node) {
            CommandNode* next = node->next;
            node->next = reversed;
            reversed = node;
            node = next;
        }
        
        // 执行
        while (reversed) {
            reversed->command->execute(world);
            CommandNode* next = reversed->next;
            delete reversed;
            reversed = next;
        }
        
        count_.store(0, std::memory_order_relaxed);
    }

private:
    struct CommandNode {
        std::unique_ptr<Command> command;
        CommandNode* next;
    };
    
    std::atomic<CommandNode*> head_{nullptr};
    std::atomic<std::size_t> count_{0};
};
```

---

## 7. 并行 System 执行

### 7.1 依赖图调度

```cpp
/// System 依赖关系
struct SystemDependency {
    std::vector<std::string> reads;   // 读取的组件类型
    std::vector<std::string> writes;  // 写入的组件类型
};

/// 并行 System 调度器
class ParallelSystemScheduler {
public:
    /// 注册 System 及其依赖
    template <typename T, typename... Args>
    T& add_system(SystemDependency deps, Args&&... args) {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        auto* ptr = system.get();
        
        SystemNode node;
        node.system = std::move(system);
        node.dependency = std::move(deps);
        systems_.push_back(std::move(node));
        
        return *ptr;
    }
    
    /// 构建依赖图
    void build_dependency_graph() {
        // 分析读写冲突，构建 DAG
        for (std::size_t i = 0; i < systems_.size(); ++i) {
            for (std::size_t j = i + 1; j < systems_.size(); ++j) {
                if (has_conflict(systems_[i].dependency, systems_[j].dependency)) {
                    // j 依赖 i（假设按注册顺序）
                    systems_[j].dependencies.push_back(i);
                }
            }
        }
    }
    
    /// 并行执行
    void update_parallel(World& world, float dt) {
        std::vector<std::atomic<int>> remaining_deps(systems_.size());
        std::vector<std::atomic<bool>> completed(systems_.size());
        
        // 初始化依赖计数
        for (std::size_t i = 0; i < systems_.size(); ++i) {
            remaining_deps[i].store(systems_[i].dependencies.size());
            completed[i].store(false);
        }
        
        // 任务图执行
        tbb::task_group group;
        
        std::function<void(std::size_t)> schedule_system = [&](std::size_t idx) {
            group.run([&, idx]() {
                // 执行 system
                systems_[idx].system->update(world, dt);
                completed[idx].store(true, std::memory_order_release);
                
                // 通知依赖者
                for (std::size_t i = 0; i < systems_.size(); ++i) {
                    for (auto dep : systems_[i].dependencies) {
                        if (dep == idx) {
                            if (remaining_deps[i].fetch_sub(1) == 1) {
                                schedule_system(i);
                            }
                        }
                    }
                }
            });
        };
        
        // 启动无依赖的 system
        for (std::size_t i = 0; i < systems_.size(); ++i) {
            if (systems_[i].dependencies.empty()) {
                schedule_system(i);
            }
        }
        
        group.wait();
    }

private:
    struct SystemNode {
        std::unique_ptr<ISystem> system;
        SystemDependency dependency;
        std::vector<std::size_t> dependencies;  // 前置依赖
    };
    
    std::vector<SystemNode> systems_;
};
```

### 7.2 读写冲突检测

```cpp
/// 检测两个 System 是否存在冲突
bool has_conflict(const SystemDependency& a, const SystemDependency& b) {
    // 写-写冲突
    for (const auto& wa : a.writes) {
        for (const auto& wb : b.writes) {
            if (wa == wb) return true;
        }
    }
    
    // 读-写冲突
    for (const auto& ra : a.reads) {
        for (const auto& wb : b.writes) {
            if (ra == wb) return true;
        }
    }
    
    for (const auto& wa : a.writes) {
        for (const auto& rb : b.reads) {
            if (wa == rb) return true;
        }
    }
    
    return false;
}
```

---

## 8. 同步原语

### 8.1 SpinLock（低竞争场景）

```cpp
/// 自旋锁（适用于短临界区）
class SpinLock {
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // 自旋等待，可加入 pause 指令
            #if defined(_MSC_VER)
                _mm_pause();
            #elif defined(__GNUC__) || defined(__clang__)
                __builtin_ia32_pause();
            #endif
        }
    }
    
    void unlock() {
        flag_.clear(std::memory_order_release);
    }
    
    bool try_lock() {
        return !flag_.test_and_set(std::memory_order_acquire);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
```

### 8.2 SeqLock（读多写少）

```cpp
/// 顺序锁（适用于读多写少，读不阻塞）
template <typename T>
class SeqLock {
public:
    /// 写入（独占）
    void write(const T& value) {
        std::lock_guard lock(write_mutex_);
        seq_.fetch_add(1, std::memory_order_release);  // 奇数 = 写入中
        data_ = value;
        seq_.fetch_add(1, std::memory_order_release);  // 偶数 = 写入完成
    }
    
    /// 读取（无锁，可能重试）
    [[nodiscard]] T read() const {
        T result;
        std::uint32_t seq0, seq1;
        
        do {
            seq0 = seq_.load(std::memory_order_acquire);
            if (seq0 & 1) continue;  // 写入中，重试
            
            result = data_;
            
            seq1 = seq_.load(std::memory_order_acquire);
        } while (seq0 != seq1);  // 读取期间发生写入，重试
        
        return result;
    }

private:
    T data_;
    std::atomic<std::uint32_t> seq_{0};
    std::mutex write_mutex_;
};
```

---

## 9. 使用模式（多写者场景）

### 9.1 多线程并行读取

```cpp
// 多个线程同时读取，完全无锁
auto query = world.query<Read<Position>, Read<Velocity>>();

// 在多个线程中同时调用
query.parallel_for_each([](const Position& pos, const Velocity& vel) {
    // 只读操作，完全并行
    log_position(pos);
});
```

### 9.2 多线程并行写入（不同实体）

```cpp
// 多个线程同时写入不同实体，完全并行无锁
auto query = world.query<Write<Position>, Read<Velocity>>();

query.parallel_for_each([dt](Position& pos, const Velocity& vel) {
    // 每个线程处理不同的实体，无竞争
    pos.x += vel.vx * dt;
    pos.y += vel.vy * dt;
});
```

### 9.3 多线程写入同一实体（需要协调）

```cpp
// 当多个线程可能写入同一实体时，使用实体级锁
void multi_thread_update_entity(Chunk& chunk, std::size_t index) {
    // 方案1：使用 try_lock（非阻塞）
    if (chunk.try_lock_entity(index)) {
        auto guard = ScopeGuard([&] { chunk.unlock_entity(index); });
        auto* health = chunk.get_component_at<Health>(index);
        health->current -= 10;
    }
    
    // 方案2：使用原子组件
    auto* atomic_health = chunk.get_component_at<AtomicHealth>(index);
    atomic_health->current.fetch_sub(10, std::memory_order_relaxed);
}
```

### 9.4 多线程同时遍历 + 写入

```cpp
// 线程 A：遍历并写入位置
void thread_a_work(World& world) {
    world.query<Write<Position>>()
        .parallel_for_each([](Position& pos) {
            pos.x += 1.0f;
        });
}

// 线程 B：同时遍历并写入速度
void thread_b_work(World& world) {
    world.query<Write<Velocity>>()
        .parallel_for_each([](Velocity& vel) {
            vel.vx *= 0.99f;  // 摩擦力
        });
}

// 可以并行执行（访问不同组件）
std::thread t1(thread_a_work, std::ref(world));
std::thread t2(thread_b_work, std::ref(world));
t1.join();
t2.join();
```

### 9.5 多线程结构变更

```cpp
// 多个线程同时创建/销毁实体
void multi_thread_entity_ops() {
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&world, i]() {
            auto& cmd = ThreadLocalCommandBuffer::current();
            
            // 每个线程创建自己的实体
            for (int j = 0; j < 1000; ++j) {
                cmd.create_entity<Position, Velocity>();
            }
            
            // 遍历时延迟销毁
            world.query<Read<Health>>()
                .parallel_for_each_with_entity([&](EntityId e, const Health& h) {
                    if (h.current <= 0) {
                        cmd.destroy_entity(e);
                    }
                });
        });
    }
    
    for (auto& t : threads) t.join();
    
    // 统一执行所有命令
    command_manager.flush_all(world);
}
```

### 9.6 遍历期间的结构稳定性

```cpp
// 使用版本快照保证遍历一致性
void safe_iteration_during_changes(World& world) {
    // 获取快照（此时结构是稳定的）
    auto archetypes = world.get_archetype_snapshot();
    
    for (Archetype* arch : archetypes) {
        auto snapshots = arch->get_chunk_snapshot();
        
        for (auto& snap : snapshots) {
            // 快照包含固定的 size，即使 Chunk 在变化
            for (std::size_t i = 0; i < snap.size; ++i) {
                // 处理实体...
                // 注意：新增的实体不会被遍历
                // 删除的实体可能已无效（需要检查）
            }
            
            // 可选：检查快照是否仍然有效
            if (!snap.is_valid()) {
                // 结构已变更，可以选择重试或忽略
            }
        }
    }
}
```

---

## 10. 性能指南（多写者场景）

### 10.1 最佳实践

| 场景 | 推荐方案 | 原因 |
|-----|---------|------|
| 多线程只读 | `parallel_for_each` | 完全无锁 |
| 多线程写不同实体 | `parallel_for_each` | 无竞争，最大并行度 |
| 多线程写同一实体 | 实体级锁或原子组件 | 细粒度同步 |
| 多线程同时遍历 | Chunk 快照 | 结构稳定性 |
| 多线程创建实体 | 分段 EntityManager | 减少锁竞争 |
| 多线程销毁实体 | CommandBuffer | 延迟执行避免竞争 |
| 多线程创建 Archetype | 分桶锁 | 不同签名并行 |

### 10.2 并发度分析

```
场景                          并发度      瓶颈
─────────────────────────────────────────────────
多线程读组件                   O(n)       无（完全并行）
多线程写不同 Chunk             O(chunks)  无（完全并行）
多线程写同一 Chunk 不同实体    O(n)       无（不同索引无竞争）
多线程写同一实体               O(1)       实体级锁
多线程分配实体                 O(segments) 分段锁
多线程创建 Archetype           O(buckets)  分桶锁
```

### 10.3 避免的模式

```cpp
// ❌ 避免：全局锁保护整个遍历
std::lock_guard lock(global_mutex);
query.for_each([](auto& comp) { ... });

// ✅ 正确：使用快照 + 并行遍历
query.parallel_for_each([](auto& comp) { ... });


// ❌ 避免：在并行遍历中直接修改结构
query.parallel_for_each([&world](const Trigger& trigger) {
    if (trigger.active) {
        world.create_entity(...);  // 危险！
    }
});

// ✅ 正确：使用线程本地 CommandBuffer
query.parallel_for_each([](const Trigger& trigger) {
    if (trigger.active) {
        ThreadLocalCommandBuffer::current().create_entity(...);
    }
});


// ❌ 避免：多线程同时写入同一实体无保护
void thread_a() { entity.get<Health>().current -= 10; }
void thread_b() { entity.get<Health>().current -= 20; }

// ✅ 正确：使用原子操作或实体锁
void thread_a() { 
    entity.get<AtomicHealth>().current.fetch_sub(10); 
}
void thread_b() { 
    entity.get<AtomicHealth>().current.fetch_sub(20); 
}
```

### 10.4 锁粒度选择（更新版）

| 数据结构 | 锁类型 | 并发度 | 说明 |
|---------|--------|--------|------|
| EntityManager | 分段锁 | O(64) | 64 个段可并行分配 |
| EntityRecord | 原子 + 版本 | O(n) | 完全无锁读写 |
| World.archetypes | 分桶锁 | O(32) | 32 个桶可并行创建 |
| Archetype.chunks | shared_mutex | O(1) | 结构变更串行 |
| Chunk.structure | SpinLock | O(chunks) | Per-Chunk 独立 |
| Chunk.data | 无锁 | O(n) | 不同实体完全并行 |
| 同一实体 | 实体级锁/原子 | O(1) | 需要协调 |
| QueryCache | shared_mutex + 无锁队列 | O(1) 更新 | 惰性更新 |

### 10.5 性能调优建议

1. **减少锁竞争**
   - 增加分段/分桶数量
   - 使用线程本地缓存
   - 批量操作减少锁获取次数

2. **提高并行度**
   - 使用 `partitioned_for_each` 细粒度划分工作
   - 避免在 Query 中混合读写不同组件
   - 将相关组件放在同一 Archetype

3. **减少缓存失效**
   - 按 Chunk 顺序遍历
   - 避免跨 Chunk 随机访问
   - 使用 SoA 布局提高缓存命中率

4. **避免伪共享**
   - 组件数据 64 字节对齐
   - 原子变量独立缓存行
   - 分段数据结构间隔

---

## 11. 测试计划（多写者验证）

| 测试文件 | 测试内容 |
|---------|---------|
| `entity_manager_mt_test.cpp` | 多线程并发分配/释放压力测试 |
| `archetype_mt_test.cpp` | 多线程并发实体分配、Chunk 遍历 |
| `query_parallel_test.cpp` | 多线程并行 for_each 正确性 |
| `command_buffer_mt_test.cpp` | 多线程命令写入 |
| `world_mt_test.cpp` | 完整多写者并发场景测试 |
| `concurrent_iteration_test.cpp` | 多线程同时遍历 + 写入测试 |
| `same_entity_write_test.cpp` | 多线程写同一实体测试 |

### 11.1 多写者压力测试用例

```cpp
TEST(ConcurrencyTest, MultipleWritersToSameChunk) {
    World world;
    // 创建足够多的实体填满多个 Chunk
    for (int i = 0; i < 10000; ++i) {
        world.create_entity<Position, Velocity>();
    }
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    // 启动多个写入线程
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t]() {
            world.query<Write<Position>>()
                .parallel_for_each([&, t](Position& pos) {
                    pos.x += t;  // 每个线程写入不同值
                    success_count.fetch_add(1);
                });
        });
    }
    
    for (auto& t : threads) t.join();
    
    // 验证所有写入都完成
    ASSERT_EQ(success_count.load(), 8 * 10000);
}

TEST(ConcurrencyTest, ConcurrentIterationAndStructureChange) {
    World world;
    ParallelCommandBufferManager cmd_mgr;
    
    // 初始实体
    for (int i = 0; i < 1000; ++i) {
        world.create_entity<Position>();
    }
    
    std::atomic<bool> running{true};
    
    // 线程1：持续遍历
    std::thread reader([&]() {
        while (running) {
            world.query<Read<Position>>()
                .parallel_for_each([](const Position& pos) {
                    // 读取操作
                    volatile float x = pos.x;
                });
        }
    });
    
    // 线程2：持续创建/销毁实体
    std::thread writer([&]() {
        for (int i = 0; i < 100; ++i) {
            auto& cmd = ThreadLocalCommandBuffer::current();
            cmd.create_entity<Position>();
            cmd_mgr.flush_all(world);
        }
    });
    
    writer.join();
    running = false;
    reader.join();
    
    // 验证没有崩溃和数据损坏
}
```

---

## 12. 实现优先级

### Phase MT.1：基础多写者支持

- [ ] EntityManager 分段锁设计
- [ ] Archetype Per-Chunk 锁
- [ ] World 分桶锁
- [ ] 版本控制机制

### Phase MT.2：并行遍历支持

- [ ] ChunkSnapshot 快照机制
- [ ] Query::parallel_for_each（多写者安全）
- [ ] Query::partitioned_for_each
- [ ] 实体级锁（可选）

### Phase MT.3：CommandBuffer 多写者

- [ ] 线程本地 CommandBuffer
- [ ] 无锁命令队列（MPSC）
- [ ] ParallelCommandBufferManager
- [ ] 批量命令执行

### Phase MT.4：高级并发特性

- [ ] 原子组件支持
- [ ] 依赖图 System 调度器
- [ ] 自动读写冲突检测

---

## 参考资料

- [Unity DOTS - Job System](https://docs.unity3d.com/Manual/JobSystem.html)
- [EnTT - 多线程指南](https://github.com/skypjack/entt/wiki/Crash-Course:-entity-component-system#multithreading)
- [Flecs - 多线程](https://www.flecs.dev/flecs/md_docs_2Manual.html#multithreading)
- [C++ Concurrency in Action](https://www.manning.com/books/c-plus-plus-concurrency-in-action)
