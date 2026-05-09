#pragma once

#include <cstddef>

#include "cache_aligned_allocator.h"

namespace Corona::Kernal::Memory {

/// 默认常量
inline constexpr std::size_t kDefaultChunkSize = 64 * 1024;  // 64 KB
inline constexpr std::size_t kMinBlockSize = 16;             // 最小块大小（需要能存放指针）
inline constexpr std::size_t kMaxBlockSize = 4096;           // 最大块大小

/// 内存池配置
struct PoolConfig {
    std::size_t block_size = 64;                  ///< 每个块的大小
    std::size_t block_alignment = CacheLineSize;  ///< 块对齐要求
    std::size_t chunk_size = kDefaultChunkSize;   ///< 每个 Chunk 的大小
    std::size_t initial_chunks = 1;               ///< 初始 Chunk 数量
    std::size_t max_chunks = 0;                   ///< 最大 Chunk 数量 (0 = 无限制)
    bool thread_safe = true;                      ///< 是否线程安全
    bool enable_debug = false;                    ///< 是否启用调试功能

    /// 验证配置是否有效
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return block_size >= kMinBlockSize && block_size <= kMaxBlockSize &&
               is_power_of_two(block_alignment) && chunk_size > block_size &&
               (max_chunks == 0 || max_chunks >= initial_chunks);
    }

   private:
    [[nodiscard]] static constexpr bool is_power_of_two(std::size_t value) noexcept {
        return value != 0 && (value & (value - 1)) == 0;
    }
};

/// 内存池统计信息
struct PoolStats {
    std::size_t total_memory = 0;        ///< 总分配内存（字节）
    std::size_t used_memory = 0;         ///< 已使用内存（字节）
    std::size_t peak_memory = 0;         ///< 峰值内存使用（字节）
    std::size_t allocation_count = 0;    ///< 分配次数
    std::size_t deallocation_count = 0;  ///< 释放次数
    std::size_t chunk_count = 0;         ///< Chunk 数量
    std::size_t block_count = 0;         ///< Block 总数
    std::size_t free_block_count = 0;    ///< 空闲 Block 数

    /// 计算使用率
    [[nodiscard]] double utilization() const noexcept {
        return total_memory > 0 ? static_cast<double>(used_memory) / static_cast<double>(total_memory) : 0.0;
    }

    /// 计算碎片率
    [[nodiscard]] double fragmentation() const noexcept {
        if (block_count == 0) return 0.0;
        return 1.0 - static_cast<double>(block_count - free_block_count) / static_cast<double>(block_count);
    }
};

}  // namespace Corona::Kernal::Memory
