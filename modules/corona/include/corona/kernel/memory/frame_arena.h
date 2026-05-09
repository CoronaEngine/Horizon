#pragma once

#include <array>
#include <cstddef>
#include <utility>

#include "linear_arena.h"

namespace Corona::Kernal::Memory {

/// 默认帧分配器缓冲区大小（1MB）
inline constexpr std::size_t kDefaultFrameArenaSize = 1024 * 1024;

/// 双缓冲帧分配器（支持跨帧数据）
///
/// 设计原理：
/// - 使用两个 LinearArena 交替使用
/// - 当前帧使用一个缓冲区分配
/// - 帧结束时交换缓冲区，新帧重置当前缓冲区
/// - 上一帧的数据在下一帧仍然有效
class FrameArena {
   public:
    /// 构造函数
    /// @param buffer_size 每个缓冲区的大小
    explicit FrameArena(std::size_t buffer_size = kDefaultFrameArenaSize);

    /// 禁用拷贝
    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    /// 支持移动
    FrameArena(FrameArena&&) noexcept = default;
    FrameArena& operator=(FrameArena&&) noexcept = default;

    /// 析构函数
    ~FrameArena() = default;

    /// 获取当前帧的分配器
    [[nodiscard]] LinearArena& current() noexcept { return arenas_[current_index_]; }

    /// 获取当前帧的分配器（const 版本）
    [[nodiscard]] const LinearArena& current() const noexcept { return arenas_[current_index_]; }

    /// 获取上一帧的分配器
    [[nodiscard]] LinearArena& previous() noexcept { return arenas_[1 - current_index_]; }

    /// 获取上一帧的分配器（const 版本）
    [[nodiscard]] const LinearArena& previous() const noexcept { return arenas_[1 - current_index_]; }

    /// 交换缓冲区（帧结束时调用）
    void swap() noexcept;

    /// 帧开始时调用（重置当前缓冲区）
    void begin_frame() noexcept;

    /// 帧结束时调用（交换缓冲区）
    void end_frame() noexcept;

    /// 分配内存（从当前帧分配器）
    /// @param size 分配大小
    /// @param alignment 对齐要求
    /// @return 分配的内存地址，空间不足返回 nullptr
    [[nodiscard]] void* allocate(std::size_t size,
                                 std::size_t alignment = alignof(std::max_align_t)) noexcept;

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

    /// 获取当前帧索引
    [[nodiscard]] std::size_t current_index() const noexcept { return current_index_; }

    /// 获取单个缓冲区容量
    [[nodiscard]] std::size_t buffer_capacity() const noexcept { return arenas_[0].capacity(); }

    /// 获取当前帧已使用大小
    [[nodiscard]] std::size_t current_used() const noexcept { return arenas_[current_index_].used(); }

    /// 获取当前帧可用空间
    [[nodiscard]] std::size_t current_available() const noexcept {
        return arenas_[current_index_].available();
    }

    /// 重置两个缓冲区
    void reset_all() noexcept;

   private:
    std::array<LinearArena, 2> arenas_;
    std::size_t current_index_ = 0;
};

// ============================================================================
// 实现
// ============================================================================

template <typename T, typename... Args>
T* FrameArena::create(Args&&... args) {
    return current().create<T>(std::forward<Args>(args)...);
}

template <typename T>
T* FrameArena::allocate_array(std::size_t count) {
    return current().allocate_array<T>(count);
}

}  // namespace Corona::Kernal::Memory
