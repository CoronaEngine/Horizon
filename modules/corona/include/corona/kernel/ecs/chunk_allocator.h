#pragma once
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "ecs_types.h"

namespace Corona::Kernel::ECS {

/**
 * @brief Chunk 内存分配器
 *
 * 专门用于 ECS Chunk 内存管理的分配器。使用预分配的大内存块（Arena）
 * 来管理固定大小的 Chunk 内存，避免频繁的系统调用。
 *
 * 特性：
 * - 固定大小分配：所有 Chunk 大小相同（默认 16KB）
 * - 内存池复用：释放的 Chunk 内存会被复用
 * - 缓存行对齐：内存按 64 字节对齐
 * - 线程安全：可选的线程安全模式
 *
 * 使用示例：
 * @code
 * ChunkAllocator allocator(16 * 1024);  // 16KB chunks
 *
 * void* memory = allocator.allocate();
 * // ... use memory ...
 * allocator.deallocate(memory);
 * @endcode
 */
class ChunkAllocator {
   public:
    /// 默认对齐大小（缓存行）
    static constexpr std::size_t kDefaultAlignment = 64;

    /// 默认 Arena 大小（1MB，可容纳 64 个 16KB Chunk）
    static constexpr std::size_t kDefaultArenaSize = 1024 * 1024;

    /**
     * @brief 构造函数
     * @param chunk_size 每个 Chunk 的大小（默认 16KB）
     * @param arena_size 每个 Arena 的大小（默认 1MB）
     * @param thread_safe 是否线程安全（默认 true）
     */
    explicit ChunkAllocator(std::size_t chunk_size = kDefaultChunkSize,
                            std::size_t arena_size = kDefaultArenaSize, bool thread_safe = true);

    /// 析构函数
    ~ChunkAllocator();

    // 禁止拷贝
    ChunkAllocator(const ChunkAllocator&) = delete;
    ChunkAllocator& operator=(const ChunkAllocator&) = delete;

    // 支持移动
    ChunkAllocator(ChunkAllocator&& other) noexcept;
    ChunkAllocator& operator=(ChunkAllocator&& other) noexcept;

    // ========================================
    // 分配与释放
    // ========================================

    /**
     * @brief 分配一块 Chunk 内存
     *
     * 优先从空闲列表中获取，如果没有则从 Arena 中分配新内存。
     * 返回的内存已按 kDefaultAlignment 对齐。
     *
     * @return 分配的内存指针，失败返回 nullptr
     */
    [[nodiscard]] void* allocate();

    /**
     * @brief 释放 Chunk 内存
     *
     * 将内存放回空闲列表供复用，不会立即归还给系统。
     *
     * @param ptr 要释放的内存指针
     */
    void deallocate(void* ptr);

    // ========================================
    // 统计信息
    // ========================================

    /// 获取 Chunk 大小
    [[nodiscard]] std::size_t chunk_size() const { return chunk_size_; }

    /// 获取已分配的 Chunk 数量
    [[nodiscard]] std::size_t allocated_count() const;

    /// 获取空闲 Chunk 数量
    [[nodiscard]] std::size_t free_count() const;

    /// 获取 Arena 数量
    [[nodiscard]] std::size_t arena_count() const;

    /// 获取总内存使用量（字节）
    [[nodiscard]] std::size_t total_memory() const;

    /// 获取已使用内存量（字节）
    [[nodiscard]] std::size_t used_memory() const;

    // ========================================
    // 内存管理
    // ========================================

    /**
     * @brief 重置分配器
     *
     * 释放所有 Arena 内存，重置到初始状态。
     * 注意：调用此方法前必须确保所有分配的 Chunk 已不再使用。
     */
    void reset();

    /**
     * @brief 收缩空闲内存
     *
     * 释放完全空闲的 Arena（如果有）。
     * 这是一个优化操作，可以在内存压力大时调用。
     */
    void shrink();

   private:
    /// Arena 内存块
    struct Arena {
        std::byte* data = nullptr;       ///< 内存起始地址
        std::size_t size = 0;            ///< Arena 大小
        std::size_t offset = 0;          ///< 当前分配偏移
        std::size_t chunk_capacity = 0;  ///< 可容纳的 Chunk 数量
        std::size_t allocated = 0;       ///< 已分配的 Chunk 数量（用于 shrink）

        Arena() = default;
        Arena(std::byte* d, std::size_t s, std::size_t cc)
            : data(d), size(s), offset(0), chunk_capacity(cc), allocated(0) {}
    };

    /// 空闲链表节点（复用释放的 Chunk 内存）
    struct FreeNode {
        FreeNode* next = nullptr;
    };

    /// 创建新的 Arena
    void create_arena();

    /// 分配实现（不加锁）
    [[nodiscard]] void* allocate_impl();

    /// 释放实现（不加锁）
    void deallocate_impl(void* ptr);

    /// 重置实现（不加锁）
    void reset_impl();

    /// 收缩实现（不加锁）
    void shrink_impl();

    /// 从 Arena 分配（不加锁）
    [[nodiscard]] void* allocate_from_arena();

    /// 从空闲列表获取（不加锁）
    [[nodiscard]] void* pop_free_list();

    /// 加入空闲列表（不加锁）
    void push_free_list(void* ptr);

    std::size_t chunk_size_;           ///< Chunk 大小
    std::size_t arena_size_;           ///< Arena 大小
    std::size_t aligned_chunk_size_;   ///< 对齐后的 Chunk 大小
    bool thread_safe_;                 ///< 是否线程安全
    std::vector<Arena> arenas_;        ///< Arena 列表
    FreeNode* free_list_ = nullptr;    ///< 空闲链表头
    std::size_t allocated_count_ = 0;  ///< 已分配 Chunk 数量
    std::size_t free_count_ = 0;       ///< 空闲 Chunk 数量
    mutable std::mutex mutex_;         ///< 线程安全锁
};

/**
 * @brief 全局 Chunk 分配器
 *
 * 提供一个全局共享的 ChunkAllocator 实例。
 * 适用于大多数 ECS 使用场景。
 */
ChunkAllocator& get_global_chunk_allocator();

}  // namespace Corona::Kernel::ECS
