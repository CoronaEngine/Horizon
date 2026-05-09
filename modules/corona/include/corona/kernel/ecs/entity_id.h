#pragma once
#include <cstdint>
#include <functional>
#include <limits>

namespace Corona::Kernel::ECS {

/**
 * @brief 实体 ID（索引 + 版本号）
 *
 * 采用索引 + 版本号的复合结构，使用 64 位整数编码：
 *
 * ```
 * EntityId (64 bits):
 * ┌─────────────────────────────────┬─────────────────────────────────┐
 * │         Index (32 bits)         │       Generation (32 bits)      │
 * └─────────────────────────────────┴─────────────────────────────────┘
 * ```
 *
 * - **Index**：实体在 EntityRecord 数组中的索引
 * - **Generation**：版本号，每次 ID 被回收并重新分配时递增
 *
 * 版本号用于检测悬空引用：
 * @code
 * EntityId enemy = world.create_entity();  // index=5, gen=1
 * world.destroy_entity(enemy);              // ID 回收
 * EntityId npc = world.create_entity();     // index=5, gen=2 (复用索引)
 *
 * world.is_alive(enemy);  // false (gen=1 != 当前 gen=2)
 * world.is_alive(npc);    // true
 * @endcode
 */
class EntityId {
   public:
    using IndexType = std::uint32_t;
    using GenerationType = std::uint32_t;

    /// 无效索引值
    static constexpr IndexType kInvalidIndex = std::numeric_limits<IndexType>::max();

    /// 无效版本号
    static constexpr GenerationType kInvalidGeneration = 0;

    /// 默认构造（无效 ID）
    constexpr EntityId() noexcept : id_(pack(kInvalidIndex, kInvalidGeneration)) {}

    /// 从索引和版本号构造
    constexpr EntityId(IndexType index, GenerationType generation) noexcept
        : id_(pack(index, generation)) {}

    /// 从原始 64 位值构造
    static constexpr EntityId from_raw(std::uint64_t raw) noexcept {
        EntityId id;
        id.id_ = raw;
        return id;
    }

    /// 获取索引部分
    [[nodiscard]] constexpr IndexType index() const noexcept {
        return static_cast<IndexType>(id_ >> 32);
    }

    /// 获取版本号部分
    [[nodiscard]] constexpr GenerationType generation() const noexcept {
        return static_cast<GenerationType>(id_ & 0xFFFFFFFF);
    }

    /// 获取原始 64 位值
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return id_; }

    /// 检查是否为有效 ID
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return index() != kInvalidIndex && generation() != kInvalidGeneration;
    }

    /// 创建无效 ID
    [[nodiscard]] static constexpr EntityId invalid() noexcept { return EntityId{}; }

    /// 相等比较
    [[nodiscard]] constexpr bool operator==(const EntityId& other) const noexcept = default;

    /// 三路比较
    [[nodiscard]] constexpr auto operator<=>(const EntityId& other) const noexcept = default;

   private:
    /// 将索引和版本号打包为 64 位整数
    static constexpr std::uint64_t pack(IndexType index, GenerationType generation) noexcept {
        return (static_cast<std::uint64_t>(index) << 32) | static_cast<std::uint64_t>(generation);
    }

    std::uint64_t id_;
};

/// 无效实体 ID 常量
inline constexpr EntityId kInvalidEntity = EntityId::invalid();

}  // namespace Corona::Kernel::ECS

// std::hash 特化
namespace std {
template <>
struct hash<Corona::Kernel::ECS::EntityId> {
    std::size_t operator()(const Corona::Kernel::ECS::EntityId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.raw());
    }
};
}  // namespace std
