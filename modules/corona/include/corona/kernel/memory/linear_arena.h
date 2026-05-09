#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include "cache_aligned_allocator.h"

namespace Corona::Kernal::Memory {

/// 线性分配器（只分配不释放，整体重置）
class LinearArena {
   public:
    /// 构造函数
    /// @param capacity 容量（字节）
    explicit LinearArena(std::size_t capacity);

    /// 析构函数
    ~LinearArena();

    /// 禁用拷贝
    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;

    /// 支持移动
    LinearArena(LinearArena&& other) noexcept;
    LinearArena& operator=(LinearArena&& other) noexcept;

    /// 分配内存
    /// @param size 分配大小
    /// @param alignment 对齐要求
    /// @return 分配的内存地址，空间不足返回 nullptr
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept;

    /// 分配并构造对象
    /// @param args 构造函数参数
    /// @return 新创建的对象指针
    /// @throw std::bad_alloc 如果分配失败
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args);

    /// 分配数组
    /// @param count 元素数量
    /// @return 数组首元素指针
    /// @throw std::bad_alloc 如果分配失败
    template <typename T>
    [[nodiscard]] T* allocate_array(std::size_t count);

    /// 分配数组（不初始化）
    /// @param count 元素数量
    /// @return 数组首元素指针
    template <typename T>
    [[nodiscard]] T* allocate_array_uninitialized(std::size_t count) noexcept;

    /// 重置分配器（不调用析构函数！）
    void reset() noexcept;

    /// 获取容量
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// 获取已使用大小
    [[nodiscard]] std::size_t used() const noexcept { return offset_; }

    /// 获取可用空间
    [[nodiscard]] std::size_t available() const noexcept { return capacity_ - offset_; }

    /// 检查是否为空
    [[nodiscard]] bool empty() const noexcept { return offset_ == 0; }

    /// 获取使用率
    [[nodiscard]] double utilization() const noexcept {
        return capacity_ > 0 ? static_cast<double>(offset_) / static_cast<double>(capacity_) : 0.0;
    }

   private:
    std::byte* buffer_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
};

// ============================================================================
// 实现
// ============================================================================

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
    if (count == 0) {
        return nullptr;
    }

    void* ptr = allocate(sizeof(T) * count, alignof(T));
    if (!ptr) {
        throw std::bad_alloc();
    }

    // 默认构造所有元素
    T* arr = static_cast<T*>(ptr);
    if constexpr (std::is_default_constructible_v<T>) {
        for (std::size_t i = 0; i < count; ++i) {
            new (arr + i) T();
        }
    }
    return arr;
}

template <typename T>
T* LinearArena::allocate_array_uninitialized(std::size_t count) noexcept {
    if (count == 0) {
        return nullptr;
    }
    return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
}

}  // namespace Corona::Kernal::Memory
