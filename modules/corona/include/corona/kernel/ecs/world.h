#pragma once
#include <memory>
#include <unordered_map>

#include "archetype.h"
#include "entity_manager.h"

namespace Corona::Kernel::ECS {

/**
 * @brief ECS 世界
 *
 * World 是 ECS 的顶层容器，整合 EntityManager 和 Archetype 存储，
 * 提供统一的实体操作接口。
 *
 * 主要功能：
 * - 创建/销毁实体
 * - 添加/移除/获取组件
 * - 自动管理 Archetype 的创建和实体迁移
 *
 * 示例：
 * @code
 * World world;
 *
 * // 创建带组件的实体
 * EntityId entity = world.create_entity(
 *     Position{10.0f, 20.0f, 0.0f},
 *     Velocity{1.0f, 0.0f, 0.0f}
 * );
 *
 * // 访问组件
 * if (auto* pos = world.get_component<Position>(entity)) {
 *     pos->x += 1.0f;
 * }
 *
 * // 添加新组件
 * world.add_component<Health>(entity, Health{100, 100});
 *
 * // 销毁实体
 * world.destroy_entity(entity);
 * @endcode
 */
class World {
   public:
    World();
    ~World();

    // 禁止拷贝
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // 支持移动
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;

    // ========================================
    // 实体生命周期
    // ========================================

    /**
     * @brief 创建空实体
     *
     * 创建一个没有任何组件的实体。空实体不属于任何 Archetype，
     * 需要添加组件后才能进行组件操作。
     *
     * @return 新实体的 ID
     */
    [[nodiscard]] EntityId create_entity();

    /**
     * @brief 创建带组件的实体
     *
     * 创建实体并同时设置初始组件。这是最高效的实体创建方式，
     * 因为可以一次性分配到正确的 Archetype。
     *
     * @tparam Ts 组件类型列表
     * @param components 组件初始值
     * @return 新实体的 ID
     *
     * @code
     * auto entity = world.create_entity(
     *     Position{0, 0, 0},
     *     Velocity{1, 0, 0},
     *     Health{100, 100}
     * );
     * @endcode
     */
    template <Component... Ts>
    [[nodiscard]] EntityId create_entity(Ts&&... components);

    /**
     * @brief 销毁实体
     *
     * 销毁实体及其所有组件。销毁后，实体 ID 将变为无效。
     *
     * @param entity 实体 ID
     * @return 销毁成功返回 true，实体不存在返回 false
     */
    bool destroy_entity(EntityId entity);

    /**
     * @brief 检查实体是否存活
     *
     * @param entity 实体 ID
     * @return 存活返回 true
     */
    [[nodiscard]] bool is_alive(EntityId entity) const;

    // ========================================
    // 组件操作
    // ========================================

    /**
     * @brief 添加组件
     *
     * 为实体添加新组件。如果实体已有该组件，操作失败。
     * 添加组件会导致实体迁移到新的 Archetype。
     *
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @param component 组件值
     * @return 添加成功返回 true
     */
    template <Component T>
    bool add_component(EntityId entity, T&& component);

    /**
     * @brief 移除组件
     *
     * 从实体移除指定组件。移除组件会导致实体迁移到新的 Archetype。
     *
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @return 移除成功返回 true
     */
    template <Component T>
    bool remove_component(EntityId entity);

    /**
     * @brief 获取组件
     *
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @return 组件指针，不存在返回 nullptr
     */
    template <Component T>
    [[nodiscard]] T* get_component(EntityId entity);

    template <Component T>
    [[nodiscard]] const T* get_component(EntityId entity) const;

    /**
     * @brief 设置组件值
     *
     * 设置实体的组件值。如果实体没有该组件，操作失败。
     *
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @param component 组件值
     * @return 设置成功返回 true
     */
    template <Component T>
    bool set_component(EntityId entity, T&& component);

    /**
     * @brief 检查实体是否有指定组件
     *
     * @tparam T 组件类型
     * @param entity 实体 ID
     * @return 有该组件返回 true
     */
    template <Component T>
    [[nodiscard]] bool has_component(EntityId entity) const;

    // ========================================
    // 批量遍历
    // ========================================

    /**
     * @brief 遍历具有指定组件的所有实体
     *
     * @tparam Ts 组件类型列表
     * @tparam Func 回调函数类型
     * @param func 回调函数，签名为 void(Ts&...)
     *
     * @code
     * world.each<Position, Velocity>([](Position& pos, Velocity& vel) {
     *     pos.x += vel.vx;
     *     pos.y += vel.vy;
     * });
     * @endcode
     */
    template <Component... Ts, typename Func>
    void each(Func&& func);

    /**
     * @brief 遍历具有指定组件的所有实体（带 EntityId）
     *
     * @tparam Ts 组件类型列表
     * @tparam Func 回调函数类型
     * @param func 回调函数，签名为 void(EntityId, Ts&...)
     */
    template <Component... Ts, typename Func>
    void each_with_entity(Func&& func);

    // ========================================
    // 统计信息
    // ========================================

    /// 获取存活实体数量
    [[nodiscard]] std::size_t entity_count() const;

    /// 获取 Archetype 数量
    [[nodiscard]] std::size_t archetype_count() const;

    /// 获取 EntityManager（供高级用法）
    [[nodiscard]] EntityManager& entity_manager() { return entity_manager_; }
    [[nodiscard]] const EntityManager& entity_manager() const { return entity_manager_; }

   private:
    /// 获取或创建 Archetype
    Archetype* get_or_create_archetype(const ArchetypeSignature& signature);

    /// 通过 ID 获取 Archetype
    Archetype* get_archetype(ArchetypeId id);
    const Archetype* get_archetype(ArchetypeId id) const;

    /// 迁移实体到新 Archetype
    bool migrate_entity(EntityId entity, const ArchetypeSignature& new_signature);

    /// 查找匹配签名的所有 Archetype
    std::vector<Archetype*> find_archetypes_with(const ArchetypeSignature& required);

    /// 处理 swap-and-pop 后被移动实体的位置更新
    void handle_swap_and_pop(ArchetypeId archetype_id, const EntityLocation& from,
                             const EntityLocation& to);

    EntityManager entity_manager_;  ///< 实体管理器
    std::unordered_map<std::size_t, std::unique_ptr<Archetype>>
        archetypes_;                                               ///< Archetype 存储（key = signature hash）
    std::unordered_map<ArchetypeId, Archetype*> archetype_by_id_;  ///< ID -> Archetype 映射
    ArchetypeId next_archetype_id_ = 0;                            ///< Archetype ID 分配器
};

// ========================================
// 辅助函数前向声明
// ========================================

/// 拷贝两个 Archetype 之间的共有组件（前向声明，定义在文件末尾）
inline void copy_common_components(Archetype& src, const EntityLocation& src_loc, Archetype& dst,
                                   const EntityLocation& dst_loc);

// ========================================
// 模板方法实现
// ========================================

template <Component... Ts>
EntityId World::create_entity(Ts&&... components) {
    // 注册组件类型
    (CORONA_REGISTER_COMPONENT(std::decay_t<Ts>), ...);

    // 构建签名
    auto signature = ArchetypeSignature::create<std::decay_t<Ts>...>();

    // 获取或创建 Archetype
    Archetype* archetype = get_or_create_archetype(signature);
    if (!archetype) {
        return kInvalidEntity;
    }

    // 分配实体 ID
    EntityId entity = entity_manager_.create();

    // 在 Archetype 中分配槽位
    EntityLocation location = archetype->allocate_entity();

    // 更新实体记录
    entity_manager_.update_location(entity, archetype->id(), location);

    // 设置组件值
    (set_component_impl<std::decay_t<Ts>>(*archetype, location, std::forward<Ts>(components)), ...);

    return entity;
}

template <Component T>
bool World::add_component(EntityId entity, T&& component) {
    if (!is_alive(entity)) {
        return false;
    }

    // 注册组件类型
    CORONA_REGISTER_COMPONENT(std::decay_t<T>);

    auto* record = entity_manager_.get_record(entity);
    if (!record) {
        return false;
    }

    // 获取当前 Archetype
    Archetype* current_archetype = get_archetype(record->archetype_id);

    // 构建新签名
    ArchetypeSignature new_signature;
    if (current_archetype) {
        // 检查是否已有该组件
        if (current_archetype->has_component<T>()) {
            return false;  // 已有该组件
        }
        new_signature = current_archetype->signature();
    }
    new_signature.add<T>();

    // 获取或创建目标 Archetype
    Archetype* target_archetype = get_or_create_archetype(new_signature);
    if (!target_archetype) {
        return false;
    }

    // 在目标 Archetype 分配新槽位
    EntityLocation new_location = target_archetype->allocate_entity();

    // 如果有旧 Archetype，拷贝共有组件
    if (current_archetype) {
        copy_common_components(*current_archetype, record->location, *target_archetype,
                               new_location);

        // 从旧 Archetype 释放
        ArchetypeId arch_id = current_archetype->id();
        EntityLocation old_loc = record->location;
        auto moved_from = current_archetype->deallocate_entity(old_loc);

        // 处理 swap-and-pop 影响
        if (moved_from.has_value()) {
            handle_swap_and_pop(arch_id, *moved_from, old_loc);
        }
    }

    // 设置新组件
    set_component_impl<std::decay_t<T>>(*target_archetype, new_location,
                                        std::forward<T>(component));

    // 更新实体记录
    entity_manager_.update_location(entity, target_archetype->id(), new_location);

    return true;
}

template <Component T>
bool World::remove_component(EntityId entity) {
    if (!is_alive(entity)) {
        return false;
    }

    auto* record = entity_manager_.get_record(entity);
    if (!record) {
        return false;
    }

    // 获取当前 Archetype
    Archetype* current_archetype = get_archetype(record->archetype_id);
    if (!current_archetype || !current_archetype->has_component<T>()) {
        return false;  // 没有该组件
    }

    // 构建新签名
    ArchetypeSignature new_signature = current_archetype->signature();
    new_signature.remove<T>();

    if (new_signature.empty()) {
        // 移除所有组件，实体变为空实体
        ArchetypeId arch_id = current_archetype->id();
        EntityLocation old_loc = record->location;
        auto moved_from = current_archetype->deallocate_entity(old_loc);
        if (moved_from.has_value()) {
            handle_swap_and_pop(arch_id, *moved_from, old_loc);
        }
        record->archetype_id = kInvalidArchetypeId;
        record->location = EntityLocation{};
        return true;
    }

    // 获取或创建目标 Archetype
    Archetype* target_archetype = get_or_create_archetype(new_signature);
    if (!target_archetype) {
        return false;
    }

    // 在目标 Archetype 分配新槽位
    EntityLocation new_location = target_archetype->allocate_entity();

    // 拷贝共有组件（不包括被移除的）
    copy_common_components(*current_archetype, record->location, *target_archetype, new_location);

    // 从旧 Archetype 释放
    ArchetypeId arch_id = current_archetype->id();
    EntityLocation old_loc = record->location;
    auto moved_from = current_archetype->deallocate_entity(old_loc);
    if (moved_from.has_value()) {
        handle_swap_and_pop(arch_id, *moved_from, old_loc);
    }

    // 更新实体记录
    entity_manager_.update_location(entity, target_archetype->id(), new_location);

    return true;
}

template <Component T>
T* World::get_component(EntityId entity) {
    if (!is_alive(entity)) {
        return nullptr;
    }

    auto* record = entity_manager_.get_record(entity);
    if (!record) {
        return nullptr;
    }

    Archetype* archetype = get_archetype(record->archetype_id);
    if (!archetype) {
        return nullptr;
    }

    return archetype->get_component<T>(record->location);
}

template <Component T>
const T* World::get_component(EntityId entity) const {
    return const_cast<World*>(this)->get_component<T>(entity);
}

template <Component T>
bool World::set_component(EntityId entity, T&& component) {
    T* comp = get_component<T>(entity);
    if (!comp) {
        return false;
    }
    *comp = std::forward<T>(component);
    return true;
}

template <Component T>
bool World::has_component(EntityId entity) const {
    if (!is_alive(entity)) {
        return false;
    }

    auto* record = entity_manager_.get_record(entity);
    if (!record) {
        return false;
    }

    const Archetype* archetype = get_archetype(record->archetype_id);
    if (!archetype) {
        return false;
    }

    return archetype->has_component<T>();
}

template <Component... Ts, typename Func>
void World::each(Func&& func) {
    // 构建查询签名
    auto required = ArchetypeSignature::create<Ts...>();

    // 遍历所有匹配的 Archetype
    for (auto& [hash, archetype] : archetypes_) {
        if (!archetype->signature().contains_all(required)) {
            continue;
        }

        // 遍历该 Archetype 的所有 Chunk
        for (std::size_t chunk_idx = 0; chunk_idx < archetype->chunk_count(); ++chunk_idx) {
            auto& chunk = archetype->get_chunk(chunk_idx);
            auto count = chunk.size();

            if (count == 0) {
                continue;
            }

            // 获取组件数组
            auto components = std::make_tuple(chunk.get_components<Ts>()...);

            // 遍历实体
            for (std::size_t i = 0; i < count; ++i) {
                func(std::get<std::span<Ts>>(components)[i]...);
            }
        }
    }
}

template <Component... Ts, typename Func>
void World::each_with_entity(Func&& func) {
    // 构建查询签名
    auto required = ArchetypeSignature::create<Ts...>();

    // 遍历所有匹配的 Archetype
    for (auto& [hash, archetype] : archetypes_) {
        if (!archetype->signature().contains_all(required)) {
            continue;
        }

        // 遍历该 Archetype 的所有 Chunk
        for (std::size_t chunk_idx = 0; chunk_idx < archetype->chunk_count(); ++chunk_idx) {
            auto& chunk = archetype->get_chunk(chunk_idx);
            auto count = chunk.size();

            if (count == 0) {
                continue;
            }

            // 获取组件数组
            auto components = std::make_tuple(chunk.get_components<Ts>()...);

            // 遍历实体
            for (std::size_t i = 0; i < count; ++i) {
                // 注意：此处无法直接获取 EntityId，需要 Chunk 存储实体 ID
                // 暂时传递无效 ID，后续需要扩展 Chunk 存储
                func(kInvalidEntity, std::get<std::span<Ts>>(components)[i]...);
            }
        }
    }
}

// ========================================
// 私有辅助模板
// ========================================

namespace detail {

template <Component T>
void set_component_at(Archetype& archetype, const EntityLocation& location, T&& value) {
    T* comp = archetype.get_component<T>(location);
    if (comp) {
        *comp = std::forward<T>(value);
    }
}

}  // namespace detail

template <Component T>
void set_component_impl(Archetype& archetype, const EntityLocation& location, T&& value) {
    detail::set_component_at(archetype, location, std::forward<T>(value));
}

/// 拷贝两个 Archetype 之间的共有组件
inline void copy_common_components(Archetype& src, const EntityLocation& src_loc, Archetype& dst,
                                   const EntityLocation& dst_loc) {
    const auto& src_layout = src.layout();
    const auto& dst_layout = dst.layout();

    for (const auto& src_comp : src_layout.components) {
        const auto* dst_comp = dst_layout.find_component(src_comp.type_id);
        if (!dst_comp) {
            continue;  // 目标没有此组件
        }

        void* src_ptr = src.get_component(src_loc, src_comp.type_id);
        void* dst_ptr = dst.get_component(dst_loc, src_comp.type_id);

        if (src_ptr && dst_ptr && src_comp.type_info) {
            if (src_comp.type_info->is_trivially_copyable) {
                std::memcpy(dst_ptr, src_ptr, src_comp.size);
            } else if (src_comp.type_info->move_assign) {
                src_comp.type_info->move_assign(dst_ptr, src_ptr);
            }
        }
    }
}

}  // namespace Corona::Kernel::ECS
