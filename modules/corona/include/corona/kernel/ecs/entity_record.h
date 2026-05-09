#pragma once
#include "ecs_types.h"
#include "entity_id.h"

namespace Corona::Kernel::ECS {

/**
 * @brief 实体记录（存储实体位置信息）
 *
 * 每个实体槽位对应一条记录，存储实体在 ECS 中的位置信息。
 * 包括所属 Archetype、在 Chunk 中的位置，以及版本号用于悬空引用检测。
 *
 * 内存布局：
 * ```
 * EntityRecord Array:
 * ┌─────────┬─────────┬─────────┬─────────┬─────────┐
 * │ Record0 │ Record1 │ Record2 │ Record3 │   ...   │
 * └─────────┴─────────┴─────────┴─────────┴─────────┘
 *      ↑         ↑
 *  alive=true  alive=false (可回收)
 * ```
 */
struct EntityRecord {
    ArchetypeId archetype_id = kInvalidArchetypeId;  ///< 所属 Archetype（无效表示未分配）
    EntityLocation location;                         ///< 在 Archetype/Chunk 中的位置
    EntityId::GenerationType generation = 0;         ///< 版本号（用于悬空引用检测）

    /// 检查实体是否存活
    [[nodiscard]] bool is_alive() const noexcept {
        return archetype_id != kInvalidArchetypeId;
    }

    /// 重置为初始状态（保留 generation）
    void reset() noexcept {
        archetype_id = kInvalidArchetypeId;
        location = EntityLocation{};
        // generation 递增，不重置
    }

    /// 完全清空（用于初始化）
    void clear() noexcept {
        archetype_id = kInvalidArchetypeId;
        location = EntityLocation{};
        generation = 0;
    }
};

}  // namespace Corona::Kernel::ECS
