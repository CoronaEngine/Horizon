#pragma once

#include <cstddef>
#include <cstdint>

#include "pool_config.h"

namespace Corona::Kernal::Memory {

/// 空闲块节点（嵌入式链表）
struct FreeBlock {
    FreeBlock* next = nullptr;
};

/// Chunk 头部元数据
struct ChunkHeader {
    ChunkHeader* next_chunk = nullptr;  ///< 下一个 Chunk（链表）
    std::size_t block_size = 0;         ///< 每个 Block 的大小
    std::size_t block_count = 0;        ///< Block 总数
    std::size_t free_count = 0;         ///< 空闲 Block 数量
    std::byte* first_block = nullptr;   ///< 第一个 Block 的地址
    FreeBlock* free_list = nullptr;     ///< 空闲链表头

#if defined(_DEBUG) || defined(CFW_ENABLE_MEMORY_DEBUG)
    std::uint32_t magic = 0xDEADBEEF;  ///< 调试魔数
    std::size_t allocation_count = 0;  ///< 分配计数（调试用）
#endif

    /// 计算给定 Chunk 大小能容纳的 Block 数量
    [[nodiscard]] static std::size_t calculate_block_count(std::size_t chunk_size, std::size_t block_size,
                                                           std::size_t alignment) noexcept {
        // 减去头部大小后，计算能容纳多少个对齐的 Block
        const std::size_t header_size = align_up(sizeof(ChunkHeader), alignment);
        if (chunk_size <= header_size) {
            return 0;
        }
        const std::size_t available = chunk_size - header_size;
        const std::size_t aligned_block_size = align_up(block_size, alignment);
        return available / aligned_block_size;
    }

    /// 检查指针是否在此 Chunk 范围内
    [[nodiscard]] bool contains(const void* ptr) const noexcept {
        const auto* byte_ptr = static_cast<const std::byte*>(ptr);
        const std::byte* end = first_block + block_size * block_count;
        return byte_ptr >= first_block && byte_ptr < end;
    }

    /// 验证 Chunk 完整性（调试用）
    [[nodiscard]] bool validate() const noexcept {
#if defined(_DEBUG) || defined(CFW_ENABLE_MEMORY_DEBUG)
        if (magic != 0xDEADBEEF) {
            return false;
        }
#endif
        return block_size >= kMinBlockSize && block_count > 0 && free_count <= block_count &&
               first_block != nullptr;
    }
};

}  // namespace Corona::Kernal::Memory
