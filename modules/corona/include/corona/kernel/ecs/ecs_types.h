#pragma once
#include <cstddef>
#include <limits>

namespace Corona::Kernel::ECS {

/// 组件类型 ID（基于 type_index 的哈希值）
using ComponentTypeId = std::size_t;

/// Archetype ID 类型
using ArchetypeId = std::size_t;

// EntityId 类定义在 entity_id.h 中

/// 无效的 Archetype ID
inline constexpr ArchetypeId kInvalidArchetypeId = std::numeric_limits<ArchetypeId>::max();

/// 无效的组件类型 ID
inline constexpr ComponentTypeId kInvalidComponentTypeId = 0;

/// 默认 Chunk 大小（16KB，通常为 4 个内存页）
inline constexpr std::size_t kDefaultChunkSize = 16 * 1024;

/// 实体在 Archetype 内的位置
struct EntityLocation {
    std::size_t chunk_index = 0;     ///< Chunk 索引
    std::size_t index_in_chunk = 0;  ///< Chunk 内索引

    [[nodiscard]] bool operator==(const EntityLocation&) const = default;
};

/// 无效的实体位置
inline constexpr EntityLocation kInvalidEntityLocation{
    std::numeric_limits<std::size_t>::max(),
    std::numeric_limits<std::size_t>::max()};

}  // namespace Corona::Kernel::ECS
