#include "corona/kernel/memory/fixed_pool.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace Corona::Kernal::Memory {

FixedPool::FixedPool(const PoolConfig& config) : config_(config) {
    // 确保块大小至少能容纳一个指针（用于空闲链表）
    if (config_.block_size < sizeof(FreeBlock)) {
        config_.block_size = sizeof(FreeBlock);
    }

    // 确保块大小对齐
    config_.block_size = align_up(config_.block_size, config_.block_alignment);

    // 预分配初始 Chunks
    for (std::size_t i = 0; i < config_.initial_chunks; ++i) {
        ChunkHeader* chunk = allocate_chunk();
        if (!chunk) {
            break;
        }
    }
}

FixedPool::FixedPool(FixedPool&& other) noexcept
    : config_(other.config_),
      first_chunk_(other.first_chunk_),
      free_list_(other.free_list_),
      total_blocks_(other.total_blocks_),
      free_blocks_(other.free_blocks_),
      chunk_count_(other.chunk_count_),
      allocation_count_(other.allocation_count_),
      deallocation_count_(other.deallocation_count_),
      peak_used_blocks_(other.peak_used_blocks_) {
    other.first_chunk_ = nullptr;
    other.free_list_ = nullptr;
    other.total_blocks_ = 0;
    other.free_blocks_ = 0;
    other.chunk_count_ = 0;
    other.allocation_count_ = 0;
    other.deallocation_count_ = 0;
    other.peak_used_blocks_ = 0;
}

FixedPool& FixedPool::operator=(FixedPool&& other) noexcept {
    if (this != &other) {
        clear();

        config_ = other.config_;
        first_chunk_ = other.first_chunk_;
        free_list_ = other.free_list_;
        total_blocks_ = other.total_blocks_;
        free_blocks_ = other.free_blocks_;
        chunk_count_ = other.chunk_count_;
        allocation_count_ = other.allocation_count_;
        deallocation_count_ = other.deallocation_count_;
        peak_used_blocks_ = other.peak_used_blocks_;

        other.first_chunk_ = nullptr;
        other.free_list_ = nullptr;
        other.total_blocks_ = 0;
        other.free_blocks_ = 0;
        other.chunk_count_ = 0;
        other.allocation_count_ = 0;
        other.deallocation_count_ = 0;
        other.peak_used_blocks_ = 0;
    }
    return *this;
}

FixedPool::~FixedPool() { clear(); }

void* FixedPool::allocate() {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocate_impl();
    }
    return allocate_impl();
}

void* FixedPool::allocate_impl() {
    // 尝试从空闲链表分配
    if (free_list_) {
        FreeBlock* block = free_list_;
        free_list_ = block->next;
        --free_blocks_;
        ++allocation_count_;

        // 更新峰值
        std::size_t used = total_blocks_ - free_blocks_;
        if (used > peak_used_blocks_) {
            peak_used_blocks_ = used;
        }

        return block;
    }

    // 空闲链表为空，需要分配新的 Chunk
    if (config_.max_chunks > 0 && chunk_count_ >= config_.max_chunks) {
        return nullptr;  // 达到最大 Chunk 数量限制
    }

    ChunkHeader* chunk = allocate_chunk();
    if (!chunk) {
        return nullptr;
    }

    // 从新 Chunk 的空闲链表取一个块
    assert(free_list_ != nullptr);
    FreeBlock* block = free_list_;
    free_list_ = block->next;
    --free_blocks_;
    ++allocation_count_;

    // 更新峰值
    std::size_t used = total_blocks_ - free_blocks_;
    if (used > peak_used_blocks_) {
        peak_used_blocks_ = used;
    }

    return block;
}

void FixedPool::deallocate(void* ptr) noexcept {
    if (!ptr) {
        return;
    }

    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
        deallocate_impl(ptr);
    } else {
        deallocate_impl(ptr);
    }
}

void FixedPool::deallocate_impl(void* ptr) noexcept {
#if defined(_DEBUG) || defined(CFW_ENABLE_MEMORY_DEBUG)
    // 验证指针属于某个 Chunk
    ChunkHeader* chunk = find_chunk(ptr);
    if (!chunk) {
        assert(false && "Attempting to deallocate pointer not from this pool");
        return;
    }
#endif

    // 将块加入空闲链表头部
    auto* block = static_cast<FreeBlock*>(ptr);
    block->next = free_list_;
    free_list_ = block;
    ++free_blocks_;
    ++deallocation_count_;
}

void FixedPool::reset() noexcept {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }

    // 重新初始化所有 Chunk 的空闲链表
    free_list_ = nullptr;
    free_blocks_ = 0;

    ChunkHeader* chunk = first_chunk_;
    while (chunk) {
        initialize_free_list(chunk);
        chunk = chunk->next_chunk;
    }

    allocation_count_ = 0;
    deallocation_count_ = 0;
}

void FixedPool::shrink_to_fit() {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }

    // 找出完全空闲的 Chunk 并释放
    ChunkHeader* prev = nullptr;
    ChunkHeader* chunk = first_chunk_;

    while (chunk) {
        ChunkHeader* next = chunk->next_chunk;

        // 检查 Chunk 是否完全空闲
        if (chunk->free_count == chunk->block_count) {
            // 从空闲链表中移除该 Chunk 的所有块
            FreeBlock** pp = &free_list_;
            while (*pp) {
                auto* block_ptr = reinterpret_cast<std::byte*>(*pp);
                if (chunk->contains(block_ptr)) {
                    *pp = (*pp)->next;
                    --free_blocks_;
                } else {
                    pp = &(*pp)->next;
                }
            }

            // 从 Chunk 链表中移除
            if (prev) {
                prev->next_chunk = next;
            } else {
                first_chunk_ = next;
            }

            total_blocks_ -= chunk->block_count;
            --chunk_count_;

            free_chunk(chunk);
        } else {
            prev = chunk;
        }

        chunk = next;
    }
}

void FixedPool::clear() noexcept {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }

    // 释放所有 Chunk
    ChunkHeader* chunk = first_chunk_;
    while (chunk) {
        ChunkHeader* next = chunk->next_chunk;
        free_chunk(chunk);
        chunk = next;
    }

    first_chunk_ = nullptr;
    free_list_ = nullptr;
    total_blocks_ = 0;
    free_blocks_ = 0;
    chunk_count_ = 0;
    allocation_count_ = 0;
    deallocation_count_ = 0;
    peak_used_blocks_ = 0;
}

std::size_t FixedPool::total_blocks() const noexcept {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    return total_blocks_;
}

std::size_t FixedPool::free_blocks() const noexcept {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    return free_blocks_;
}

std::size_t FixedPool::used_blocks() const noexcept {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    return total_blocks_ - free_blocks_;
}

std::size_t FixedPool::chunk_count() const noexcept {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    return chunk_count_;
}

std::size_t FixedPool::total_memory() const noexcept {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    return chunk_count_ * config_.chunk_size;
}

PoolStats FixedPool::stats() const noexcept {
    if (config_.thread_safe) {
        std::lock_guard<std::mutex> lock(mutex_);
    }

    PoolStats stats;
    stats.total_memory = chunk_count_ * config_.chunk_size;
    stats.used_memory = (total_blocks_ - free_blocks_) * config_.block_size;
    stats.peak_memory = peak_used_blocks_ * config_.block_size;
    stats.allocation_count = allocation_count_;
    stats.deallocation_count = deallocation_count_;
    stats.chunk_count = chunk_count_;
    stats.block_count = total_blocks_;
    stats.free_block_count = free_blocks_;
    return stats;
}

ChunkHeader* FixedPool::allocate_chunk() {
    // 分配对齐的内存
    void* raw_memory = aligned_malloc(config_.chunk_size, config_.block_alignment);
    if (!raw_memory) {
        return nullptr;
    }

    // 初始化 Chunk 头部
    auto* chunk = new (raw_memory) ChunkHeader();
    chunk->block_size = config_.block_size;
    chunk->block_count =
        ChunkHeader::calculate_block_count(config_.chunk_size, config_.block_size, config_.block_alignment);

    if (chunk->block_count == 0) {
        aligned_free(raw_memory);
        return nullptr;
    }

    // 计算第一个块的地址
    const std::size_t header_size = align_up(sizeof(ChunkHeader), config_.block_alignment);
    chunk->first_block = static_cast<std::byte*>(raw_memory) + header_size;
    chunk->free_count = chunk->block_count;

    // 初始化空闲链表
    initialize_free_list(chunk);

    // 加入 Chunk 链表
    chunk->next_chunk = first_chunk_;
    first_chunk_ = chunk;

    total_blocks_ += chunk->block_count;
    ++chunk_count_;

    return chunk;
}

void FixedPool::initialize_free_list(ChunkHeader* chunk) {
    chunk->free_count = chunk->block_count;

    // 构建空闲链表
    std::byte* block_ptr = chunk->first_block;
    for (std::size_t i = 0; i < chunk->block_count; ++i) {
        auto* block = reinterpret_cast<FreeBlock*>(block_ptr);
        block->next = free_list_;
        free_list_ = block;
        ++free_blocks_;
        block_ptr += config_.block_size;
    }

    chunk->free_list = free_list_;
}

ChunkHeader* FixedPool::find_chunk(void* ptr) const noexcept {
    ChunkHeader* chunk = first_chunk_;
    while (chunk) {
        if (chunk->contains(ptr)) {
            return chunk;
        }
        chunk = chunk->next_chunk;
    }
    return nullptr;
}

void FixedPool::free_chunk(ChunkHeader* chunk) noexcept {
    if (chunk) {
        chunk->~ChunkHeader();
        aligned_free(chunk);
    }
}

}  // namespace Corona::Kernal::Memory
