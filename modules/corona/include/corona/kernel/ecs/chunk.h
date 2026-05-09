#pragma once
#include <cassert>
#include <cstddef>
#include <optional>
#include <span>

#include "archetype_layout.h"

namespace Corona::Kernel::ECS {

// 前向声明
class ChunkAllocator;

/**
 * @brief Chunk 内存块
 *
 * Chunk 是 Archetype 内部的内存管理单元，固定大小（默认 16KB）。
 * 每个 Chunk 存储多个实体的组件数据，采用 SoA 布局。
 *
 * 内存布局示意：
 * ```
 * [ComponentA 数组][Padding][ComponentB 数组][Padding]...
 * ```
 *
 * 特性：
 * - 固定大小，便于内存池管理
 * - SoA 布局，缓存友好
 * - swap-and-pop 删除策略，保持数据紧凑
 * - 支持外部内存分配器（ChunkAllocator）
 */
class Chunk {
   public:
    /**
     * @brief 构造函数（自分配内存）
     * @param layout 组件布局信息
     * @param capacity 实体容量（由布局计算得出）
     */
    explicit Chunk(const ArchetypeLayout& layout, std::size_t capacity);

    /**
     * @brief 构造函数（使用外部分配器）
     * @param layout 组件布局信息
     * @param capacity 实体容量
     * @param allocator 内存分配器
     */
    Chunk(const ArchetypeLayout& layout, std::size_t capacity, ChunkAllocator* allocator);

    /// 析构函数，释放内存并调用所有组件的析构函数
    ~Chunk();

    // 禁止拷贝
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    // 支持移动
    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;

    // ========================================
    // 容量信息
    // ========================================

    /// 获取当前实体数量
    [[nodiscard]] std::size_t size() const { return count_; }

    /// 获取最大容量
    [[nodiscard]] std::size_t capacity() const { return capacity_; }

    /// 是否已满
    [[nodiscard]] bool is_full() const { return count_ >= capacity_; }

    /// 是否为空
    [[nodiscard]] bool is_empty() const { return count_ == 0; }

    // ========================================
    // 组件数组访问
    // ========================================

    /**
     * @brief 获取指定组件类型的数据数组起始指针
     * @param type_id 组件类型 ID
     * @return 组件数组指针，类型不存在返回 nullptr
     */
    [[nodiscard]] void* get_component_array(ComponentTypeId type_id);
    [[nodiscard]] const void* get_component_array(ComponentTypeId type_id) const;

    /**
     * @brief 类型安全的组件数组访问
     * @tparam T 组件类型
     * @return 组件数组的 span，可直接遍历
     */
    template <Component T>
    [[nodiscard]] std::span<T> get_components() {
        void* ptr = get_component_array(get_component_type_id<T>());
        if (!ptr) {
            return {};
        }
        return std::span<T>(static_cast<T*>(ptr), count_);
    }

    template <Component T>
    [[nodiscard]] std::span<const T> get_components() const {
        const void* ptr = get_component_array(get_component_type_id<T>());
        if (!ptr) {
            return {};
        }
        return std::span<const T>(static_cast<const T*>(ptr), count_);
    }

    /**
     * @brief 获取指定索引的组件指针
     * @param type_id 组件类型 ID
     * @param index 实体在 Chunk 内的索引
     * @return 组件指针，无效返回 nullptr
     */
    [[nodiscard]] void* get_component_at(ComponentTypeId type_id, std::size_t index);
    [[nodiscard]] const void* get_component_at(ComponentTypeId type_id, std::size_t index) const;

    /**
     * @brief 类型安全的单个组件访问
     * @tparam T 组件类型
     * @param index 实体索引
     * @return 组件指针
     */
    template <Component T>
    [[nodiscard]] T* get_component_at(std::size_t index) {
        return static_cast<T*>(get_component_at(get_component_type_id<T>(), index));
    }

    template <Component T>
    [[nodiscard]] const T* get_component_at(std::size_t index) const {
        return static_cast<const T*>(get_component_at(get_component_type_id<T>(), index));
    }

    // ========================================
    // 实体槽位管理
    // ========================================

    /**
     * @brief 在 Chunk 末尾分配一个新实体槽位
     *
     * 为新实体分配空间并调用所有组件的默认构造函数。
     *
     * @return 新实体在 Chunk 内的索引
     * @pre !is_full()
     */
    [[nodiscard]] std::size_t allocate();

    /**
     * @brief 释放指定索引的实体槽位
     *
     * 使用 swap-and-pop 策略：将最后一个实体移动到被删除的位置，
     * 保持数据紧凑，避免内存碎片。
     *
     * @param index 要释放的实体索引
     * @return 如果发生了交换，返回被移动实体的原索引（即最后一个实体）；
     *         如果删除的是最后一个实体，返回 nullopt
     * @pre index < size()
     */
    std::optional<std::size_t> deallocate(std::size_t index);

    /**
     * @brief 获取布局信息
     * @return 布局引用
     */
    [[nodiscard]] const ArchetypeLayout& layout() const {
        assert(layout_ != nullptr);
        return *layout_;
    }

    /**
     * @brief 重新绑定布局指针
     *
     * 当 Archetype 被移动后，需要更新 Chunk 中的布局指针以指向新的布局地址。
     *
     * @param new_layout 新的布局指针
     */
    void rebind_layout(const ArchetypeLayout* new_layout) { layout_ = new_layout; }

   private:
    /// 初始化内存（构造函数通用逻辑）
    void init_memory();

    /// 调用指定索引实体的所有组件构造函数
    void construct_components_at(std::size_t index);

    /// 调用指定索引实体的所有组件析构函数
    void destruct_components_at(std::size_t index);

    /// 将 src 索引的组件移动构造到 dst 索引（dst 必须是未初始化内存）
    void move_construct_components(std::size_t dst, std::size_t src);

    /// 将 src 索引的组件移动赋值到 dst 索引（dst 必须是已初始化对象）
    void move_assign_components(std::size_t dst, std::size_t src);

    std::byte* data_ = nullptr;                ///< 原始内存块
    std::size_t count_ = 0;                    ///< 当前实体数量
    std::size_t capacity_ = 0;                 ///< 最大实体容量
    const ArchetypeLayout* layout_ = nullptr;  ///< 组件布局（由 Archetype 持有）
    ChunkAllocator* allocator_ = nullptr;      ///< 内存分配器（nullptr 表示自分配）
    bool owns_memory_ = true;                  ///< 是否拥有内存（自分配时为 true）
};

}  // namespace Corona::Kernel::ECS
