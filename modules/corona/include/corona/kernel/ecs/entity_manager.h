#pragma once
#include <cstddef>
#include <vector>

#include "entity_id.h"
#include "entity_record.h"

namespace Corona::Kernel::ECS {

/**
 * @brief 实体管理器
 *
 * 管理实体的生命周期和 ID 分配，主要职责：
 * - 分配和回收 EntityId
 * - 维护 EntityId → EntityRecord 的映射
 * - 管理空闲 ID 列表（用于 ID 复用）
 * - 提供实体存活检查
 *
 * ID 复用策略：
 * - 销毁实体时，将索引加入空闲列表
 * - 创建实体时，优先从空闲列表获取索引
 * - 复用索引时递增版本号，防止悬空引用
 *
 * 示例：
 * @code
 * EntityManager manager;
 *
 * // 创建实体
 * EntityId e1 = manager.create();
 * EntityId e2 = manager.create();
 *
 * // 检查存活
 * assert(manager.is_alive(e1));
 *
 * // 销毁实体
 * manager.destroy(e1);
 * assert(!manager.is_alive(e1));
 *
 * // 创建新实体（可能复用 e1 的索引）
 * EntityId e3 = manager.create();
 * // e1 和 e3 可能有相同索引但不同版本号
 * @endcode
 */
class EntityManager {
   public:
    /// 默认初始容量
    static constexpr std::size_t kDefaultInitialCapacity = 1024;

    /// 默认构造
    EntityManager() = default;

    /// 带初始容量的构造
    explicit EntityManager(std::size_t initial_capacity);

    // 禁止拷贝
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;

    // 支持移动
    EntityManager(EntityManager&&) noexcept = default;
    EntityManager& operator=(EntityManager&&) noexcept = default;

    ~EntityManager() = default;

    // ========================================
    // 实体生命周期管理
    // ========================================

    /**
     * @brief 创建新实体
     *
     * 分配一个新的 EntityId。如果有可复用的索引，优先使用；
     * 否则扩展记录数组。
     *
     * @return 新创建实体的 ID
     */
    [[nodiscard]] EntityId create();

    /**
     * @brief 销毁实体
     *
     * 将实体标记为已销毁，递增版本号，并将索引加入空闲列表。
     * 销毁后，使用旧 EntityId 的 is_alive 检查将返回 false。
     *
     * @param id 要销毁的实体 ID
     * @return 销毁成功返回 true，ID 无效或已销毁返回 false
     */
    bool destroy(EntityId id);

    /**
     * @brief 检查实体是否存活
     *
     * 验证 EntityId 的索引和版本号是否都有效。
     *
     * @param id 要检查的实体 ID
     * @return 存活返回 true
     */
    [[nodiscard]] bool is_alive(EntityId id) const;

    // ========================================
    // 实体记录访问
    // ========================================

    /**
     * @brief 获取实体记录（可修改）
     *
     * @param id 实体 ID
     * @return 实体记录指针，ID 无效返回 nullptr
     */
    [[nodiscard]] EntityRecord* get_record(EntityId id);

    /**
     * @brief 获取实体记录（只读）
     *
     * @param id 实体 ID
     * @return 实体记录指针，ID 无效返回 nullptr
     */
    [[nodiscard]] const EntityRecord* get_record(EntityId id) const;

    /**
     * @brief 更新实体位置
     *
     * 当实体在 Archetype 间迁移或在 Chunk 内移动时调用。
     *
     * @param id 实体 ID
     * @param archetype_id 新的 Archetype ID
     * @param location 新的位置
     * @return 更新成功返回 true
     */
    bool update_location(EntityId id, ArchetypeId archetype_id, const EntityLocation& location);

    /**
     * @brief 查找位于指定位置的实体
     *
     * 遍历所有活跃实体，找到位于指定 Archetype 和位置的实体。
     * 这是一个 O(N) 操作，主要用于 swap-and-pop 后更新被移动实体的记录。
     *
     * @param archetype_id Archetype ID
     * @param location 实体位置
     * @return 找到的实体 ID，未找到返回 kInvalidEntity
     */
    [[nodiscard]] EntityId find_entity_at(ArchetypeId archetype_id,
                                          const EntityLocation& location) const;

    // ========================================
    // 统计信息
    // ========================================

    /// 获取存活实体数量
    [[nodiscard]] std::size_t alive_count() const noexcept { return alive_count_; }

    /// 获取记录总容量
    [[nodiscard]] std::size_t capacity() const noexcept { return records_.size(); }

    /// 获取空闲槽位数量
    [[nodiscard]] std::size_t free_count() const noexcept { return free_list_.size(); }

    /// 预分配容量
    void reserve(std::size_t capacity);

    /// 清空所有实体（保留容量）
    void clear();

   private:
    /// 扩展记录数组
    void grow(std::size_t new_capacity);

    /// 验证 ID 有效性（仅检查边界和版本号）
    [[nodiscard]] bool is_valid_id(EntityId id) const;

    std::vector<EntityRecord> records_;           ///< 实体记录数组
    std::vector<EntityId::IndexType> free_list_;  ///< 空闲索引列表
    std::size_t alive_count_ = 0;                 ///< 存活实体计数
};

}  // namespace Corona::Kernel::ECS
