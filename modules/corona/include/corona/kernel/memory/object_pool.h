#pragma once

#include <atomic>
#include <cassert>
#include <new>
#include <type_traits>
#include <utility>

#include "fixed_pool.h"

namespace Corona::Kernal::Memory {

/// 类型安全的对象池
template <typename T>
class ObjectPool {
    static_assert(std::is_destructible_v<T>, "T must be destructible");

   public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;

    /// 构造函数
    /// @param initial_capacity 初始容量
    /// @param thread_safe 是否线程安全
    explicit ObjectPool(std::size_t initial_capacity = 64, bool thread_safe = true);

    /// 析构函数（注意：不会自动销毁活跃对象）
    ~ObjectPool() = default;

    /// 禁用拷贝
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    /// 支持移动
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

    /// 创建对象（原地构造）
    /// @param args 构造函数参数
    /// @return 新创建的对象指针
    /// @throw std::bad_alloc 如果分配失败
    template <typename... Args>
    [[nodiscard]] pointer create(Args&&... args);

    /// 销毁对象（调用析构函数并回收内存）
    /// @param obj 要销毁的对象
    void destroy(pointer obj) noexcept;

    /// 获取容量信息
    [[nodiscard]] std::size_t capacity() const noexcept { return pool_.total_blocks(); }

    /// 获取活跃对象数
    [[nodiscard]] std::size_t size() const noexcept { return active_count_.load(std::memory_order_relaxed); }

    /// 获取可用槽位数
    [[nodiscard]] std::size_t available() const noexcept { return pool_.free_blocks(); }

    /// 获取块大小
    [[nodiscard]] std::size_t block_size() const noexcept { return pool_.block_size(); }

    /// 获取统计信息
    [[nodiscard]] PoolStats stats() const noexcept { return pool_.stats(); }

   private:
    FixedPool pool_;
    std::atomic<std::size_t> active_count_{0};
};

// ============================================================================
// 实现
// ============================================================================

template <typename T>
ObjectPool<T>::ObjectPool(std::size_t initial_capacity, bool thread_safe)
    : pool_(PoolConfig{
          .block_size = align_up(sizeof(T), alignof(T)),
          .block_alignment = std::max(alignof(T), CacheLineSize),
          .chunk_size = kDefaultChunkSize,
          .initial_chunks = (initial_capacity * sizeof(T) + kDefaultChunkSize - 1) / kDefaultChunkSize,
          .max_chunks = 0,
          .thread_safe = thread_safe,
          .enable_debug = false,
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
        return obj;
    } catch (...) {
        pool_.deallocate(ptr);
        throw;
    }
}

template <typename T>
void ObjectPool<T>::destroy(pointer obj) noexcept {
    if (!obj) {
        return;
    }

    // 调用析构函数
    obj->~T();

    // 回收内存
    pool_.deallocate(obj);
    active_count_.fetch_sub(1, std::memory_order_relaxed);
}

}  // namespace Corona::Kernal::Memory
