#pragma once
#include <memory>
#include <vector>

#include "chunk.h"
#include "chunk_allocator.h"

namespace Corona::Kernel::ECS {

/**
 * @brief Archetype - 存储具有相同组件组合的所有实体
 *
 * Archetype 是 ECS 架构中的核心数据结构，管理一组具有相同组件类型的实体。
 * 每个 Archetype 内部使用多个 Chunk 来存储实体数据。
 *
 * 特性：
 * - 组件数据紧凑存储，缓存友好
 * - O(1) 组件访问
 * - 支持高效的批量遍历
 * - 使用内存池管理 Chunk 内存
 *
 * 示例：
 * @code
 * auto signature = ArchetypeSignature::create<Position, Velocity>();
 * Archetype archetype(1, std::move(signature));
 *
 * // 分配实体
 * auto loc = archetype.allocate_entity();
 *
 * // 设置组件
 * archetype.set_component<Position>(loc, {10.0f, 20.0f, 0.0f});
 *
 * // 批量遍历
 * for (auto& chunk : archetype.chunks()) {
 *     auto positions = chunk.get_components<Position>();
 *     for (auto& pos : positions) {
 *         pos.x += 1.0f;
 *     }
 * }
 * @endcode
 */
class Archetype {
   public:
    /**
     * @brief 构造函数
     * @param id Archetype 唯一标识
     * @param signature 组件类型签名
     * @param allocator Chunk 内存分配器（可选，nullptr 使用全局分配器）
     */
    explicit Archetype(ArchetypeId id, ArchetypeSignature signature,
                       ChunkAllocator* allocator = nullptr);

    /// 析构函数
    ~Archetype();

    // 禁止拷贝
    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;

    // 支持移动
    Archetype(Archetype&&) noexcept;
    Archetype& operator=(Archetype&&) noexcept;

    // ========================================
    // 基本信息
    // ========================================

    /// 获取 Archetype ID
    [[nodiscard]] ArchetypeId id() const { return id_; }

    /// 获取组件签名
    [[nodiscard]] const ArchetypeSignature& signature() const { return signature_; }

    /// 获取布局信息
    [[nodiscard]] const ArchetypeLayout& layout() const { return layout_; }

    /// 获取实体总数
    [[nodiscard]] std::size_t entity_count() const;

    /// 获取 Chunk 数量
    [[nodiscard]] std::size_t chunk_count() const { return chunks_.size(); }

    /// 检查是否包含指定组件类型
    [[nodiscard]] bool has_component(ComponentTypeId type_id) const;

    /// 检查是否包含指定组件类型（模板版本）
    template <Component T>
    [[nodiscard]] bool has_component() const {
        return has_component(get_component_type_id<T>());
    }

    /// 检查 Archetype 是否为空（无实体）
    [[nodiscard]] bool empty() const { return entity_count() == 0; }

    // ========================================
    // 实体管理
    // ========================================

    /**
     * @brief 分配一个新实体槽位
     *
     * 在可用的 Chunk 中分配空间，如果没有可用空间则创建新 Chunk。
     *
     * @return 实体在 Archetype 内的位置
     */
    [[nodiscard]] EntityLocation allocate_entity();

    /**
     * @brief 释放实体槽位
     *
     * 使用 swap-and-pop 策略释放实体，保持数据紧凑。
     *
     * @param location 实体位置
     * @return 如果发生了 swap-and-pop，返回被移动实体的原位置；否则返回 nullopt
     */
    std::optional<EntityLocation> deallocate_entity(const EntityLocation& location);

    // ========================================
    // 组件访问
    // ========================================

    /**
     * @brief 获取指定位置实体的组件指针
     * @param location 实体位置
     * @param type_id 组件类型 ID
     * @return 组件指针，类型不存在或位置无效返回 nullptr
     */
    [[nodiscard]] void* get_component(const EntityLocation& location, ComponentTypeId type_id);
    [[nodiscard]] const void* get_component(const EntityLocation& location,
                                            ComponentTypeId type_id) const;

    /**
     * @brief 类型安全的组件访问
     * @tparam T 组件类型
     * @param location 实体位置
     * @return 组件指针
     */
    template <Component T>
    [[nodiscard]] T* get_component(const EntityLocation& location) {
        return static_cast<T*>(get_component(location, get_component_type_id<T>()));
    }

    template <Component T>
    [[nodiscard]] const T* get_component(const EntityLocation& location) const {
        return static_cast<const T*>(get_component(location, get_component_type_id<T>()));
    }

    /**
     * @brief 设置组件值
     * @tparam T 组件类型
     * @param location 实体位置
     * @param value 组件值
     */
    template <Component T>
    void set_component(const EntityLocation& location, T&& value) {
        T* ptr = get_component<T>(location);
        if (ptr) {
            *ptr = std::forward<T>(value);
        }
    }

    // ========================================
    // Chunk 访问（用于批量处理）
    // ========================================

    /**
     * @brief 获取指定索引的 Chunk
     * @param index Chunk 索引
     * @return Chunk 引用
     * @pre index < chunk_count()
     */
    [[nodiscard]] Chunk& get_chunk(std::size_t index);
    [[nodiscard]] const Chunk& get_chunk(std::size_t index) const;

    /// Chunk 迭代器类型
    using ChunkIterator = std::vector<std::unique_ptr<Chunk>>::iterator;
    using ConstChunkIterator = std::vector<std::unique_ptr<Chunk>>::const_iterator;

    /// Chunk 范围视图
    class ChunkRange {
       public:
        ChunkRange(ChunkIterator begin, ChunkIterator end) : begin_(begin), end_(end) {}

        class Iterator {
           public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = Chunk;
            using difference_type = std::ptrdiff_t;
            using pointer = Chunk*;
            using reference = Chunk&;

            explicit Iterator(ChunkIterator it) : it_(it) {}

            reference operator*() const { return **it_; }
            pointer operator->() const { return it_->get(); }

            Iterator& operator++() {
                ++it_;
                return *this;
            }

            Iterator operator++(int) {
                Iterator tmp = *this;
                ++it_;
                return tmp;
            }

            bool operator==(const Iterator& other) const { return it_ == other.it_; }
            bool operator!=(const Iterator& other) const { return it_ != other.it_; }

           private:
            ChunkIterator it_;
        };

        [[nodiscard]] Iterator begin() { return Iterator(begin_); }
        [[nodiscard]] Iterator end() { return Iterator(end_); }

       private:
        ChunkIterator begin_;
        ChunkIterator end_;
    };

    /// Const Chunk 范围视图
    class ConstChunkRange {
       public:
        ConstChunkRange(ConstChunkIterator begin, ConstChunkIterator end)
            : begin_(begin), end_(end) {}

        class Iterator {
           public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = const Chunk;
            using difference_type = std::ptrdiff_t;
            using pointer = const Chunk*;
            using reference = const Chunk&;

            explicit Iterator(ConstChunkIterator it) : it_(it) {}

            reference operator*() const { return **it_; }
            pointer operator->() const { return it_->get(); }

            Iterator& operator++() {
                ++it_;
                return *this;
            }

            Iterator operator++(int) {
                Iterator tmp = *this;
                ++it_;
                return tmp;
            }

            bool operator==(const Iterator& other) const { return it_ == other.it_; }
            bool operator!=(const Iterator& other) const { return it_ != other.it_; }

           private:
            ConstChunkIterator it_;
        };

        [[nodiscard]] Iterator begin() const { return Iterator(begin_); }
        [[nodiscard]] Iterator end() const { return Iterator(end_); }

       private:
        ConstChunkIterator begin_;
        ConstChunkIterator end_;
    };

    /// 获取 Chunk 范围（用于 range-based for）
    [[nodiscard]] ChunkRange chunks() { return ChunkRange(chunks_.begin(), chunks_.end()); }
    [[nodiscard]] ConstChunkRange chunks() const {
        return ConstChunkRange(chunks_.cbegin(), chunks_.cend());
    }

   private:
    /// 确保有可用的 Chunk 空间
    void ensure_capacity();

    /// 创建新的 Chunk
    Chunk& create_chunk();

    /// 查找有空闲空间的 Chunk 索引，没有则返回 -1
    [[nodiscard]] std::ptrdiff_t find_available_chunk() const;

    ArchetypeId id_;                              ///< Archetype 唯一标识
    ArchetypeSignature signature_;                ///< 组件类型签名
    ArchetypeLayout layout_;                      ///< 内存布局
    std::vector<std::unique_ptr<Chunk>> chunks_;  ///< Chunk 列表
    ChunkAllocator* allocator_ = nullptr;         ///< Chunk 内存分配器
};

}  // namespace Corona::Kernel::ECS
