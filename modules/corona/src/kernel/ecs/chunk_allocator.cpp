#include "corona/kernel/ecs/chunk_allocator.h"

#include <cassert>
#include <cstdlib>
#include <cstring>

#include "corona/pal/cfw_platform.h"

namespace Corona::Kernel::ECS {

namespace {

/// 计算对齐大小
[[nodiscard]] constexpr std::size_t align_up(std::size_t size, std::size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/// 分配对齐内存
[[nodiscard]] void* aligned_alloc_impl(std::size_t size, std::size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

#if defined(CFW_PLATFORM_WINDOWS)
    return _aligned_malloc(size, alignment);
#else
    std::size_t aligned_size = align_up(size, alignment);
    return std::aligned_alloc(alignment, aligned_size);
#endif
}

/// 释放对齐内存
void aligned_free_impl(void* ptr) {
    if (!ptr) {
        return;
    }

#if defined(CFW_PLATFORM_WINDOWS)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

}  // namespace

ChunkAllocator::ChunkAllocator(std::size_t chunk_size, std::size_t arena_size, bool thread_safe)
    : chunk_size_(chunk_size),
      arena_size_(arena_size),
      aligned_chunk_size_(align_up(chunk_size, kDefaultAlignment)),
      thread_safe_(thread_safe) {
    // 确保至少能容纳一个 Chunk
    if (arena_size_ < aligned_chunk_size_) {
        arena_size_ = aligned_chunk_size_ * 16;  // 默认预分配 16 个 Chunk 的空间
    }
}

ChunkAllocator::~ChunkAllocator() {
    // 释放所有 Arena 内存
    for (auto& arena : arenas_) {
        aligned_free_impl(arena.data);
    }
    arenas_.clear();
    free_list_ = nullptr;
    allocated_count_ = 0;
    free_count_ = 0;
}

ChunkAllocator::ChunkAllocator(ChunkAllocator&& other) noexcept
    : chunk_size_(other.chunk_size_),
      arena_size_(other.arena_size_),
      aligned_chunk_size_(other.aligned_chunk_size_),
      thread_safe_(other.thread_safe_),
      arenas_(std::move(other.arenas_)),
      free_list_(other.free_list_),
      allocated_count_(other.allocated_count_),
      free_count_(other.free_count_) {
    other.free_list_ = nullptr;
    other.allocated_count_ = 0;
    other.free_count_ = 0;
}

ChunkAllocator& ChunkAllocator::operator=(ChunkAllocator&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        for (auto& arena : arenas_) {
            aligned_free_impl(arena.data);
        }

        // 移动数据
        chunk_size_ = other.chunk_size_;
        arena_size_ = other.arena_size_;
        aligned_chunk_size_ = other.aligned_chunk_size_;
        thread_safe_ = other.thread_safe_;
        arenas_ = std::move(other.arenas_);
        free_list_ = other.free_list_;
        allocated_count_ = other.allocated_count_;
        free_count_ = other.free_count_;

        other.free_list_ = nullptr;
        other.allocated_count_ = 0;
        other.free_count_ = 0;
    }
    return *this;
}

void* ChunkAllocator::allocate() {
    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocate_impl();
    }
    return allocate_impl();
}

void* ChunkAllocator::allocate_impl() {
    // 优先从空闲列表获取
    void* ptr = pop_free_list();
    if (ptr) {
        ++allocated_count_;
        return ptr;
    }

    // 从 Arena 分配
    ptr = allocate_from_arena();
    if (ptr) {
        ++allocated_count_;
    }
    return ptr;
}

void ChunkAllocator::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }

    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        deallocate_impl(ptr);
    } else {
        deallocate_impl(ptr);
    }
}

void ChunkAllocator::deallocate_impl(void* ptr) {
    push_free_list(ptr);
    if (allocated_count_ > 0) {
        --allocated_count_;
    }
}

std::size_t ChunkAllocator::allocated_count() const {
    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocated_count_;
    }
    return allocated_count_;
}

std::size_t ChunkAllocator::free_count() const {
    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        return free_count_;
    }
    return free_count_;
}

std::size_t ChunkAllocator::arena_count() const {
    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        return arenas_.size();
    }
    return arenas_.size();
}

std::size_t ChunkAllocator::total_memory() const {
    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        return arenas_.size() * arena_size_;
    }
    return arenas_.size() * arena_size_;
}

std::size_t ChunkAllocator::used_memory() const {
    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocated_count_ * aligned_chunk_size_;
    }
    return allocated_count_ * aligned_chunk_size_;
}

void ChunkAllocator::reset() {
    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        reset_impl();
    } else {
        reset_impl();
    }
}

void ChunkAllocator::reset_impl() {
    // 释放所有 Arena
    for (auto& arena : arenas_) {
        aligned_free_impl(arena.data);
    }
    arenas_.clear();
    free_list_ = nullptr;
    allocated_count_ = 0;
    free_count_ = 0;
}

void ChunkAllocator::shrink() {
    if (thread_safe_) {
        std::lock_guard<std::mutex> lock(mutex_);
        shrink_impl();
    } else {
        shrink_impl();
    }
}

void ChunkAllocator::shrink_impl() {
    // 简单实现：只清空空闲列表中的内存，不真正释放 Arena
    // 更高级的实现可以追踪每个 Arena 的使用情况并释放完全空闲的 Arena
    // 这里暂时不做，因为需要更复杂的记账
}

void ChunkAllocator::create_arena() {
    void* memory = aligned_alloc_impl(arena_size_, kDefaultAlignment);
    if (!memory) {
        return;  // 分配失败
    }

    // 零初始化
    std::memset(memory, 0, arena_size_);

    std::size_t chunk_capacity = arena_size_ / aligned_chunk_size_;
    arenas_.emplace_back(static_cast<std::byte*>(memory), arena_size_, chunk_capacity);
}

void* ChunkAllocator::allocate_from_arena() {
    // 查找有空间的 Arena
    for (auto& arena : arenas_) {
        std::size_t remaining = arena.size - arena.offset;
        if (remaining >= aligned_chunk_size_) {
            void* ptr = arena.data + arena.offset;
            arena.offset += aligned_chunk_size_;
            ++arena.allocated;
            return ptr;
        }
    }

    // 没有可用空间，创建新 Arena
    create_arena();
    if (arenas_.empty()) {
        return nullptr;  // Arena 创建失败
    }

    // 从新 Arena 分配
    auto& arena = arenas_.back();
    void* ptr = arena.data + arena.offset;
    arena.offset += aligned_chunk_size_;
    ++arena.allocated;
    return ptr;
}

void* ChunkAllocator::pop_free_list() {
    if (!free_list_) {
        return nullptr;
    }

    FreeNode* node = free_list_;
    free_list_ = node->next;
    --free_count_;

    return static_cast<void*>(node);
}

void ChunkAllocator::push_free_list(void* ptr) {
    auto* node = static_cast<FreeNode*>(ptr);
    node->next = free_list_;
    free_list_ = node;
    ++free_count_;
}

// 全局分配器（懒初始化）
ChunkAllocator& get_global_chunk_allocator() {
    static ChunkAllocator instance(kDefaultChunkSize, ChunkAllocator::kDefaultArenaSize, true);
    return instance;
}

}  // namespace Corona::Kernel::ECS
