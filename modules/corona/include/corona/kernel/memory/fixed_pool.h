#pragma once

#include <atomic>
#include <mutex>
#include <new>

#include "chunk.h"
#include "pool_config.h"

namespace Corona::Kernal::Memory {

/// 固定大小块内存池
class FixedPool {
   public:
    /// 构造函数
    /// @param config 内存池配置
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

    /// 清空内存池（释放所有 Chunk）
    void clear() noexcept;

    /// 获取配置信息
    [[nodiscard]] const PoolConfig& config() const noexcept { return config_; }

    /// 获取块大小
    [[nodiscard]] std::size_t block_size() const noexcept { return config_.block_size; }

    /// 获取总块数
    [[nodiscard]] std::size_t total_blocks() const noexcept;

    /// 获取空闲块数
    [[nodiscard]] std::size_t free_blocks() const noexcept;

    /// 获取已使用块数
    [[nodiscard]] std::size_t used_blocks() const noexcept;

    /// 获取 Chunk 数量
    [[nodiscard]] std::size_t chunk_count() const noexcept;

    /// 获取总内存占用（字节）
    [[nodiscard]] std::size_t total_memory() const noexcept;

    /// 获取统计信息
    [[nodiscard]] PoolStats stats() const noexcept;

   private:
    /// 分配新的 Chunk
    [[nodiscard]] ChunkHeader* allocate_chunk();

    /// 初始化 Chunk 的空闲链表
    void initialize_free_list(ChunkHeader* chunk);

    /// 查找包含指定地址的 Chunk
    [[nodiscard]] ChunkHeader* find_chunk(void* ptr) const noexcept;

    /// 释放单个 Chunk
    void free_chunk(ChunkHeader* chunk) noexcept;

    /// 内部分配实现（无锁）
    [[nodiscard]] void* allocate_impl();

    /// 内部释放实现（无锁）
    void deallocate_impl(void* ptr) noexcept;

   private:
    PoolConfig config_;
    ChunkHeader* first_chunk_ = nullptr;
    FreeBlock* free_list_ = nullptr;  // 全局空闲链表（跨 Chunk）

    // 线程安全
    mutable std::mutex mutex_;

    // 统计
    std::size_t total_blocks_ = 0;
    std::size_t free_blocks_ = 0;
    std::size_t chunk_count_ = 0;
    std::size_t allocation_count_ = 0;
    std::size_t deallocation_count_ = 0;
    std::size_t peak_used_blocks_ = 0;
};

}  // namespace Corona::Kernal::Memory
