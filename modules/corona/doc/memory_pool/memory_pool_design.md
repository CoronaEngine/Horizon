# 游戏引擎内存池设计文档

## 1. 概述

本文档描述 Corona Framework 内存管理模块中 **Memory Pool（内存池）** 的设计方案。内存池是游戏引擎中的核心基础设施，用于高效管理频繁分配/释放的小对象，避免系统堆分配的开销和内存碎片化问题。

### 1.1 设计目标

| 目标 | 说明 |
|------|------|
| **高性能分配** | O(1) 时间复杂度的分配与释放 |
| **缓存友好** | 连续内存布局，减少 cache miss |
| **低碎片化** | 固定大小块分配，无外部碎片 |
| **线程安全** | 支持多线程并发访问（可选无锁实现） |
| **内存对齐** | 支持自定义对齐要求（默认缓存行对齐） |
| **调试支持** | 内存泄漏检测、越界检查、使用统计 |

### 1.2 适用场景

- **ECS 组件存储**：大量同类型组件的分配/释放
- **游戏对象池**：子弹、特效、NPC 等频繁创建/销毁的对象
- **帧分配器**：每帧临时数据的快速分配
- **事件系统**：事件对象的高频创建与消费
- **资源句柄**：轻量级资源引用的管理

### 1.3 核心概念

| 术语 | 说明 |
|------|------|
| **Block** | 固定大小的内存块，是分配的最小单位 |
| **Chunk** | 包含多个 Block 的大内存区域，从系统堆一次性申请 |
| **FreeList** | 空闲块链表，追踪可用的 Block |
| **Pool** | 内存池，管理特定大小 Block 的 Chunk 集合 |
| **Arena** | 线性分配器，只分配不释放，帧结束时整体重置 |

---

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        MemoryManager                             │
│  (全局内存管理器 - 统一入口)                                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │  FixedPool   │  │  FixedPool   │  │     FrameArena       │   │
│  │  (32 bytes)  │  │  (64 bytes)  │  │  (线性分配器)         │   │
│  ├──────────────┤  ├──────────────┤  ├──────────────────────┤   │
│  │   Chunk 0    │  │   Chunk 0    │  │  Front Buffer        │   │
│  │   Chunk 1    │  │   Chunk 1    │  │  Back Buffer         │   │
│  │   ...        │  │   ...        │  │                      │   │
│  └──────────────┘  └──────────────┘  └──────────────────────┘   │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                    ObjectPool<T>                         │    │
│  │  (类型安全的对象池，内部使用 FixedPool)                    │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
              ┌───────────────────────────────┐
              │     System Allocator          │
              │  (aligned_malloc / mmap)      │
              └───────────────────────────────┘
```

### 2.2 内存布局

#### Chunk 内存布局

```
┌────────────────────────────────────────────────────────────────┐
│                         Chunk (e.g., 64KB)                      │
├────────────┬────────────┬────────────┬────────────┬────────────┤
│  ChunkHeader │  Block 0   │  Block 1   │  Block 2   │   ...     │
│  (metadata)  │ [aligned]  │ [aligned]  │ [aligned]  │           │
├────────────┴────────────┴────────────┴────────────┴────────────┤
│                                                                  │
│  ChunkHeader:                                                   │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ next_chunk* │ block_size │ block_count │ free_count │ ... │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  Block (空闲时):          Block (已分配时):                       │
│  ┌─────────────────┐      ┌─────────────────────────────────┐   │
│  │ next_free_block*│      │         User Data               │   │
│  │ (嵌入式链表)     │      │    (block_size bytes)           │   │
│  └─────────────────┘      └─────────────────────────────────┘   │
│                                                                  │
└────────────────────────────────────────────────────────────────┘
```

---

## 3. 核心数据结构

### 3.1 基础类型定义

```cpp
namespace Corona::Kernel::Memory {

/// 内存对齐常量
#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLineSize = 64;
#endif

inline constexpr std::size_t kDefaultChunkSize = 64 * 1024;  // 64 KB
inline constexpr std::size_t kMinBlockSize = 16;             // 最小块大小
inline constexpr std::size_t kMaxBlockSize = 4096;           // 最大块大小

/// 对齐辅助函数
[[nodiscard]] constexpr std::size_t align_up(std::size_t size, std::size_t alignment) noexcept {
    return (size + alignment - 1) & ~(alignment - 1);
}

/// 内存池配置
struct PoolConfig {
    std::size_t block_size = 64;                    ///< 每个块的大小
    std::size_t block_alignment = kCacheLineSize;   ///< 块对齐要求
    std::size_t chunk_size = kDefaultChunkSize;     ///< 每个 Chunk 的大小
    std::size_t initial_chunks = 1;                 ///< 初始 Chunk 数量
    std::size_t max_chunks = 0;                     ///< 最大 Chunk 数量 (0 = 无限制)
    bool thread_safe = true;                        ///< 是否线程安全
    bool enable_debug = false;                      ///< 是否启用调试功能
};

} // namespace Corona::Kernel::Memory
```

### 3.2 Chunk 头部结构

```cpp
namespace Corona::Kernel::Memory {

/// Chunk 头部元数据
struct ChunkHeader {
    ChunkHeader* next_chunk = nullptr;    ///< 下一个 Chunk（链表）
    std::size_t block_size = 0;           ///< 每个 Block 的大小
    std::size_t block_count = 0;          ///< Block 总数
    std::size_t free_count = 0;           ///< 空闲 Block 数量
    void* first_block = nullptr;          ///< 第一个 Block 的地址
    void* free_list = nullptr;            ///< 空闲链表头
    
#if CFW_DEBUG
    std::uint32_t magic = 0xDEADBEEF;     ///< 调试魔数
    std::size_t allocation_count = 0;     ///< 分配计数（调试用）
#endif
    
    /// 计算给定 Chunk 大小能容纳的 Block 数量
    [[nodiscard]] static std::size_t calculate_block_count(
        std::size_t chunk_size, 
        std::size_t block_size, 
        std::size_t alignment) noexcept;
};

} // namespace Corona::Kernel::Memory
```

### 3.3 固定大小内存池

```cpp
namespace Corona::Kernel::Memory {

/// 固定大小块内存池
class FixedPool {
public:
    /// 构造函数
    explicit FixedPool(const PoolConfig& config);
    
    /// 禁用拷贝
    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;
    
    /// 支持移动
    FixedPool(FixedPool&& other) noexcept;
    FixedPool& operator=(FixedPool&& other) noexcept;
    
    /// 析构函数
    ~FixedPool();
    
    /// 分配一个块
    /// @return 分配的内存地址，失败返回 nullptr
    [[nodiscard]] void* allocate();
    
    /// 释放一个块
    /// @param ptr 要释放的内存地址
    void deallocate(void* ptr) noexcept;
    
    /// 重置内存池（释放所有块但保留 Chunk）
    void reset() noexcept;
    
    /// 释放所有未使用的 Chunk
    void shrink_to_fit();
    
    /// 获取统计信息
    [[nodiscard]] std::size_t block_size() const noexcept { return config_.block_size; }
    [[nodiscard]] std::size_t total_blocks() const noexcept;
    [[nodiscard]] std::size_t free_blocks() const noexcept;
    [[nodiscard]] std::size_t used_blocks() const noexcept;
    [[nodiscard]] std::size_t chunk_count() const noexcept;
    [[nodiscard]] std::size_t total_memory() const noexcept;
    
private:
    /// 分配新的 Chunk
    [[nodiscard]] ChunkHeader* allocate_chunk();
    
    /// 初始化 Chunk 的空闲链表
    void initialize_free_list(ChunkHeader* chunk);
    
    /// 查找包含指定地址的 Chunk
    [[nodiscard]] ChunkHeader* find_chunk(void* ptr) const noexcept;
    
private:
    PoolConfig config_;
    ChunkHeader* first_chunk_ = nullptr;
    void* free_list_ = nullptr;  // 全局空闲链表（跨 Chunk）
    
    // 线程安全
    mutable std::mutex mutex_;  // 仅当 config_.thread_safe 时使用
    
    // 统计
    std::size_t total_blocks_ = 0;
    std::size_t free_blocks_ = 0;
    std::size_t chunk_count_ = 0;
};

} // namespace Corona::Kernel::Memory
```

### 3.4 类型安全对象池

```cpp
namespace Corona::Kernel::Memory {

/// 类型安全的对象池
template <typename T>
class ObjectPool {
    static_assert(std::is_destructible_v<T>, "T must be destructible");
    
public:
    /// 构造函数
    explicit ObjectPool(std::size_t initial_capacity = 64, bool thread_safe = true);
    
    /// 析构函数（销毁所有活跃对象）
    ~ObjectPool();
    
    /// 禁用拷贝
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    
    /// 创建对象（原地构造）
    template <typename... Args>
    [[nodiscard]] T* create(Args&&... args);
    
    /// 销毁对象（调用析构函数并回收内存）
    void destroy(T* obj) noexcept;
    
    /// 获取容量信息
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;      ///< 活跃对象数
    [[nodiscard]] std::size_t available() const noexcept; ///< 可用槽位数
    
    /// 预分配容量
    void reserve(std::size_t count);
    
    /// 清空所有对象
    void clear();
    
private:
    FixedPool pool_;
    std::atomic<std::size_t> active_count_{0};
    
#if CFW_DEBUG
    std::unordered_set<T*> active_objects_;  // 调试：追踪活跃对象
    mutable std::mutex debug_mutex_;
#endif
};

// ============================================================================
// 实现
// ============================================================================

template <typename T>
ObjectPool<T>::ObjectPool(std::size_t initial_capacity, bool thread_safe)
    : pool_(PoolConfig{
          .block_size = align_up(sizeof(T), alignof(T)),
          .block_alignment = std::max(alignof(T), kCacheLineSize),
          .initial_chunks = (initial_capacity * sizeof(T) + kDefaultChunkSize - 1) / kDefaultChunkSize,
          .thread_safe = thread_safe
      }) {}

template <typename T>
template <typename... Args>
T* ObjectPool<T>::create(Args&&... args) {
    void* ptr = pool_.allocate();
    if (!ptr) {
        throw std::bad_alloc();
    }
    
    try {
        T* obj = new (ptr) T(std::forward<Args>(args)...);
        active_count_.fetch_add(1, std::memory_order_relaxed);
        
#if CFW_DEBUG
        {
            std::lock_guard lock(debug_mutex_);
            active_objects_.insert(obj);
        }
#endif
        return obj;
    } catch (...) {
        pool_.deallocate(ptr);
        throw;
    }
}

template <typename T>
void ObjectPool<T>::destroy(T* obj) noexcept {
    if (!obj) return;
    
#if CFW_DEBUG
    {
        std::lock_guard lock(debug_mutex_);
        assert(active_objects_.count(obj) > 0 && "Double free or invalid pointer");
        active_objects_.erase(obj);
    }
#endif
    
    obj->~T();
    pool_.deallocate(obj);
    active_count_.fetch_sub(1, std::memory_order_relaxed);
}

} // namespace Corona::Kernel::Memory
```

---

## 4. 帧分配器（Frame Arena）

帧分配器用于管理单帧内的临时内存分配，采用线性分配策略，帧结束时整体重置。

### 4.1 设计原理

```
帧 N                          帧 N+1
┌─────────────────────┐      ┌─────────────────────┐
│ ████████░░░░░░░░░░░ │  →   │ ░░░░░░░░░░░░░░░░░░░ │  (重置)
│ ↑ current_offset    │      │ ↑ reset to 0        │
└─────────────────────┘      └─────────────────────┘

████ = 已分配  ░░░ = 可用空间
```

### 4.2 双缓冲帧分配器

```cpp
namespace Corona::Kernel::Memory {

/// 单缓冲区线性分配器
class LinearArena {
public:
    explicit LinearArena(std::size_t capacity);
    ~LinearArena();
    
    /// 分配内存
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
    
    /// 分配并构造对象
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args);
    
    /// 分配数组
    template <typename T>
    [[nodiscard]] T* allocate_array(std::size_t count);
    
    /// 重置分配器（不调用析构函数！）
    void reset() noexcept;
    
    /// 获取使用信息
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t used() const noexcept { return offset_; }
    [[nodiscard]] std::size_t available() const noexcept { return capacity_ - offset_; }
    
private:
    std::byte* buffer_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
};

/// 双缓冲帧分配器（支持跨帧数据）
class FrameArena {
public:
    explicit FrameArena(std::size_t buffer_size = 1024 * 1024);  // 默认 1MB
    
    /// 获取当前帧的分配器
    [[nodiscard]] LinearArena& current() noexcept { return arenas_[current_index_]; }
    
    /// 交换缓冲区（帧结束时调用）
    void swap() noexcept;
    
    /// 帧开始时调用（重置当前缓冲区）
    void begin_frame() noexcept;
    
    /// 便捷分配接口
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));
    
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args);
    
private:
    std::array<LinearArena, 2> arenas_;
    std::size_t current_index_ = 0;
};

// ============================================================================
// 实现
// ============================================================================

inline void* LinearArena::allocate(std::size_t size, std::size_t alignment) {
    std::size_t aligned_offset = align_up(offset_, alignment);
    
    if (aligned_offset + size > capacity_) {
        return nullptr;  // 空间不足
    }
    
    void* ptr = buffer_ + aligned_offset;
    offset_ = aligned_offset + size;
    return ptr;
}

template <typename T, typename... Args>
T* LinearArena::create(Args&&... args) {
    void* ptr = allocate(sizeof(T), alignof(T));
    if (!ptr) {
        throw std::bad_alloc();
    }
    return new (ptr) T(std::forward<Args>(args)...);
}

template <typename T>
T* LinearArena::allocate_array(std::size_t count) {
    void* ptr = allocate(sizeof(T) * count, alignof(T));
    if (!ptr) {
        throw std::bad_alloc();
    }
    // 默认构造所有元素
    T* arr = static_cast<T*>(ptr);
    for (std::size_t i = 0; i < count; ++i) {
        new (arr + i) T();
    }
    return arr;
}

} // namespace Corona::Kernel::Memory
```

---

## 5. 线程安全策略

### 5.1 锁策略选择

| 场景 | 推荐策略 | 说明 |
|------|----------|------|
| 低竞争 | `std::mutex` | 简单可靠，足够高效 |
| 高竞争 | `std::atomic` + Lock-Free | 无锁空闲链表 |
| 线程专属 | Thread-Local Pool | 每线程独立池，无需同步 |
| 帧分配 | 单线程 | FrameArena 通常在主线程使用 |

### 5.2 无锁空闲链表

```cpp
namespace Corona::Kernel::Memory {

/// 无锁空闲链表节点
struct alignas(kCacheLineSize) LockFreeNode {
    std::atomic<LockFreeNode*> next{nullptr};
};

/// 无锁空闲链表（LIFO 栈）
class LockFreeStack {
public:
    LockFreeStack() = default;
    
    /// 压入节点
    void push(LockFreeNode* node) noexcept {
        LockFreeNode* old_head = head_.load(std::memory_order_relaxed);
        do {
            node->next.store(old_head, std::memory_order_relaxed);
        } while (!head_.compare_exchange_weak(
            old_head, node,
            std::memory_order_release,
            std::memory_order_relaxed));
    }
    
    /// 弹出节点
    [[nodiscard]] LockFreeNode* pop() noexcept {
        LockFreeNode* old_head = head_.load(std::memory_order_acquire);
        while (old_head) {
            LockFreeNode* next = old_head->next.load(std::memory_order_relaxed);
            if (head_.compare_exchange_weak(
                    old_head, next,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                return old_head;
            }
        }
        return nullptr;
    }
    
    /// 检查是否为空
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == nullptr;
    }
    
private:
    std::atomic<LockFreeNode*> head_{nullptr};
};

} // namespace Corona::Kernel::Memory
```

### 5.3 线程本地池

```cpp
namespace Corona::Kernel::Memory {

/// 线程本地对象池
template <typename T>
class ThreadLocalPool {
public:
    /// 获取当前线程的池实例
    static ObjectPool<T>& instance() {
        thread_local ObjectPool<T> pool(256, false);  // 线程内无需同步
        return pool;
    }
    
    /// 便捷创建接口
    template <typename... Args>
    [[nodiscard]] static T* create(Args&&... args) {
        return instance().create(std::forward<Args>(args)...);
    }
    
    /// 便捷销毁接口
    static void destroy(T* obj) noexcept {
        instance().destroy(obj);
    }
};

} // namespace Corona::Kernel::Memory
```

---

## 6. 调试与诊断

### 6.1 调试功能

```cpp
namespace Corona::Kernel::Memory {

#if CFW_DEBUG

/// 内存块调试信息
struct BlockDebugInfo {
    void* address;
    std::size_t size;
    const char* file;
    int line;
    std::chrono::steady_clock::time_point alloc_time;
    std::thread::id thread_id;
};

/// 内存池诊断接口
class IPoolDiagnostics {
public:
    virtual ~IPoolDiagnostics() = default;
    
    /// 获取所有活跃分配
    [[nodiscard]] virtual std::vector<BlockDebugInfo> get_active_allocations() const = 0;
    
    /// 检查内存泄漏
    [[nodiscard]] virtual bool check_leaks() const = 0;
    
    /// 验证内存完整性（检查越界等）
    [[nodiscard]] virtual bool validate_memory() const = 0;
    
    /// 获取统计报告
    [[nodiscard]] virtual std::string get_stats_report() const = 0;
};

/// 带调试信息的分配宏
#define POOL_ALLOC(pool) \
    pool.allocate_debug(__FILE__, __LINE__)

#define POOL_CREATE(pool, ...) \
    pool.create_debug(__FILE__, __LINE__, __VA_ARGS__)

#else

#define POOL_ALLOC(pool) pool.allocate()
#define POOL_CREATE(pool, ...) pool.create(__VA_ARGS__)

#endif

} // namespace Corona::Kernel::Memory
```

### 6.2 内存统计

```cpp
namespace Corona::Kernel::Memory {

/// 内存池统计信息
struct PoolStats {
    std::size_t total_memory = 0;      ///< 总分配内存（字节）
    std::size_t used_memory = 0;       ///< 已使用内存（字节）
    std::size_t peak_memory = 0;       ///< 峰值内存使用（字节）
    std::size_t allocation_count = 0;  ///< 分配次数
    std::size_t deallocation_count = 0;///< 释放次数
    std::size_t chunk_count = 0;       ///< Chunk 数量
    std::size_t block_count = 0;       ///< Block 总数
    std::size_t free_block_count = 0;  ///< 空闲 Block 数
    double fragmentation = 0.0;        ///< 碎片率 (0.0 - 1.0)
    
    /// 计算使用率
    [[nodiscard]] double utilization() const noexcept {
        return total_memory > 0 ? static_cast<double>(used_memory) / total_memory : 0.0;
    }
};

/// 全局内存统计收集器
class MemoryStatsCollector {
public:
    static MemoryStatsCollector& instance();
    
    void register_pool(std::string_view name, const PoolStats& stats);
    void unregister_pool(std::string_view name);
    
    [[nodiscard]] PoolStats get_pool_stats(std::string_view name) const;
    [[nodiscard]] PoolStats get_total_stats() const;
    
    void dump_stats(std::ostream& out) const;
    void dump_stats_json(std::ostream& out) const;
    
private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, PoolStats> pools_;
};

} // namespace Corona::Kernel::Memory
```

---

## 7. 使用示例

### 7.1 基础用法

```cpp
#include "corona/kernel/memory/memory_pool.h"

using namespace Corona::Kernel::Memory;

// 1. 固定大小内存池
void example_fixed_pool() {
    PoolConfig config{
        .block_size = 128,
        .chunk_size = 64 * 1024,
        .thread_safe = true
    };
    
    FixedPool pool(config);
    
    // 分配
    void* ptr1 = pool.allocate();
    void* ptr2 = pool.allocate();
    
    // 使用内存...
    
    // 释放
    pool.deallocate(ptr1);
    pool.deallocate(ptr2);
}

// 2. 类型安全对象池
struct Bullet {
    float x, y, z;
    float velocity;
    int damage;
    
    Bullet(float px, float py, float pz, float v, int d)
        : x(px), y(py), z(pz), velocity(v), damage(d) {}
};

void example_object_pool() {
    ObjectPool<Bullet> bullet_pool(1024);  // 预分配 1024 个槽位
    
    // 创建对象
    Bullet* b1 = bullet_pool.create(0.f, 0.f, 0.f, 100.f, 10);
    Bullet* b2 = bullet_pool.create(1.f, 2.f, 3.f, 150.f, 20);
    
    // 使用对象...
    
    // 销毁对象
    bullet_pool.destroy(b1);
    bullet_pool.destroy(b2);
}

// 3. 帧分配器
void example_frame_arena() {
    FrameArena arena(1024 * 1024);  // 1MB
    
    // 游戏主循环
    while (running) {
        arena.begin_frame();  // 重置当前缓冲区
        
        // 帧内临时分配（无需手动释放）
        auto* temp_data = arena.create<TempData>();
        auto* positions = arena.current().allocate_array<Vec3>(100);
        
        // 处理游戏逻辑...
        
        arena.swap();  // 交换双缓冲
    }
}
```

### 7.2 与 ECS 集成

```cpp
#include "corona/kernel/ecs/archetype.h"
#include "corona/kernel/memory/memory_pool.h"

using namespace Corona::Kernel;

// ECS Chunk 使用内存池管理
class ChunkAllocator {
public:
    static constexpr std::size_t kChunkSize = 16 * 1024;  // 16KB per chunk
    
    ChunkAllocator() 
        : pool_(Memory::PoolConfig{
              .block_size = kChunkSize,
              .block_alignment = Memory::kCacheLineSize,
              .thread_safe = true
          }) {}
    
    [[nodiscard]] void* allocate_chunk() {
        return pool_.allocate();
    }
    
    void deallocate_chunk(void* chunk) noexcept {
        pool_.deallocate(chunk);
    }
    
private:
    Memory::FixedPool pool_;
};
```

### 7.3 线程本地池用法

```cpp
#include "corona/kernel/memory/thread_local_pool.h"

struct Event {
    int type;
    void* data;
};

void worker_thread() {
    // 每个线程使用独立的池，无锁竞争
    for (int i = 0; i < 10000; ++i) {
        Event* e = ThreadLocalPool<Event>::create();
        e->type = i;
        
        // 处理事件...
        
        ThreadLocalPool<Event>::destroy(e);
    }
}
```

---

## 8. 性能考量

### 8.1 基准测试参考

| 操作 | FixedPool | std::malloc | 倍率 |
|------|-----------|-------------|------|
| 分配 (单线程) | ~15 ns | ~80 ns | 5.3x |
| 释放 (单线程) | ~10 ns | ~60 ns | 6.0x |
| 分配 (8线程) | ~50 ns | ~200 ns | 4.0x |

*注：实际性能取决于具体硬件和使用模式*

### 8.2 优化建议

1. **预分配**：根据预估使用量设置 `initial_chunks`
2. **对齐选择**：小对象使用 16 字节对齐，大对象使用缓存行对齐
3. **线程策略**：高竞争场景使用线程本地池
4. **Chunk 大小**：平衡内存占用和分配频率（推荐 16KB - 64KB）
5. **帧分配优先**：临时数据优先使用 FrameArena

### 8.3 内存对齐注意事项

```cpp
// ✓ 推荐：对齐到缓存行，避免 false sharing
struct alignas(64) PerThreadData {
    std::atomic<int> counter;
    char padding[60];  // 填充到 64 字节
};

// ✗ 避免：小于指针大小的 block_size
PoolConfig bad_config{
    .block_size = 4  // 太小！会导致空闲链表指针无法存储
};
```

---

## 9. 接口汇总

### 9.1 核心类

| 类名 | 用途 | 线程安全 |
|------|------|----------|
| `FixedPool` | 固定大小块分配 | 可配置 |
| `ObjectPool<T>` | 类型安全对象池 | 可配置 |
| `LinearArena` | 线性分配器 | 否 |
| `FrameArena` | 双缓冲帧分配器 | 否 |
| `ThreadLocalPool<T>` | 线程本地对象池 | 是（隔离） |
| `LockFreeStack` | 无锁空闲链表 | 是 |

### 9.2 配置参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `block_size` | `size_t` | 64 | 每个块的大小 |
| `block_alignment` | `size_t` | 64 | 块对齐要求 |
| `chunk_size` | `size_t` | 65536 | 每个 Chunk 的大小 |
| `initial_chunks` | `size_t` | 1 | 初始 Chunk 数量 |
| `max_chunks` | `size_t` | 0 | 最大 Chunk 数量 |
| `thread_safe` | `bool` | true | 是否线程安全 |
| `enable_debug` | `bool` | false | 是否启用调试 |

---

## 10. 后续规划

- [ ] 实现 `PoolAllocator` 适配 STL 容器
- [ ] 添加内存池可视化工具
- [ ] 实现 NUMA 感知内存分配
- [ ] 集成 Tracy 或 Optick 性能分析
- [ ] 支持自定义上游分配器（如 jemalloc、mimalloc）

---

## 参考资料

- [Game Engine Architecture - Memory Systems](https://www.gameenginebook.com/)
- [CppCon 2017: John Lakos "Local ('Arena') Memory Allocators"](https://www.youtube.com/watch?v=nZNd5FjSquk)
- [Bitsquid Foundation Library - Memory](https://github.com/niklas-ourmachinery/bitsquid-foundation)
- [EnTT - ECS Memory Management](https://github.com/skypjack/entt)
