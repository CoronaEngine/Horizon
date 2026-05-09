#include "corona/kernel/ecs/world.h"

namespace Corona::Kernel::ECS {

World::World() = default;

World::~World() = default;

World::World(World&& other) noexcept
    : entity_manager_(std::move(other.entity_manager_)),
      archetypes_(std::move(other.archetypes_)),
      archetype_by_id_(std::move(other.archetype_by_id_)),
      next_archetype_id_(other.next_archetype_id_) {
    other.next_archetype_id_ = 0;
}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        entity_manager_ = std::move(other.entity_manager_);
        archetypes_ = std::move(other.archetypes_);
        archetype_by_id_ = std::move(other.archetype_by_id_);
        next_archetype_id_ = other.next_archetype_id_;
        other.next_archetype_id_ = 0;
    }
    return *this;
}

EntityId World::create_entity() {
    return entity_manager_.create();
}

bool World::destroy_entity(EntityId entity) {
    if (!is_alive(entity)) {
        return false;
    }

    auto* record = entity_manager_.get_record(entity);
    if (!record) {
        return false;
    }

    // 如果实体在 Archetype 中，需要释放槽位
    if (record->archetype_id != kInvalidArchetypeId) {
        Archetype* archetype = get_archetype(record->archetype_id);
        if (archetype) {
            ArchetypeId arch_id = record->archetype_id;
            EntityLocation old_loc = record->location;
            auto moved_from = archetype->deallocate_entity(old_loc);

            // 处理 swap-and-pop 影响
            if (moved_from.has_value()) {
                handle_swap_and_pop(arch_id, *moved_from, old_loc);
            }
        }
    }

    // 销毁实体 ID
    return entity_manager_.destroy(entity);
}

bool World::is_alive(EntityId entity) const {
    return entity_manager_.is_alive(entity);
}

std::size_t World::entity_count() const {
    return entity_manager_.alive_count();
}

std::size_t World::archetype_count() const {
    return archetypes_.size();
}

Archetype* World::get_or_create_archetype(const ArchetypeSignature& signature) {
    auto hash = signature.hash();

    // 查找现有 Archetype
    auto it = archetypes_.find(hash);
    if (it != archetypes_.end()) {
        return it->second.get();
    }

    // 创建新 Archetype
    ArchetypeId id = next_archetype_id_++;
    auto archetype = std::make_unique<Archetype>(id, signature);
    Archetype* ptr = archetype.get();

    archetypes_[hash] = std::move(archetype);
    archetype_by_id_[id] = ptr;

    return ptr;
}

Archetype* World::get_archetype(ArchetypeId id) {
    auto it = archetype_by_id_.find(id);
    if (it != archetype_by_id_.end()) {
        return it->second;
    }
    return nullptr;
}

const Archetype* World::get_archetype(ArchetypeId id) const {
    return const_cast<World*>(this)->get_archetype(id);
}

bool World::migrate_entity(EntityId entity, const ArchetypeSignature& new_signature) {
    auto* record = entity_manager_.get_record(entity);
    if (!record) {
        return false;
    }

    Archetype* current = get_archetype(record->archetype_id);
    Archetype* target = get_or_create_archetype(new_signature);

    if (!target) {
        return false;
    }

    // 分配新槽位
    EntityLocation new_location = target->allocate_entity();

    // 拷贝共有组件
    if (current) {
        copy_common_components(*current, record->location, *target, new_location);

        // 释放旧槽位
        ArchetypeId arch_id = current->id();
        EntityLocation old_loc = record->location;
        auto moved_from = current->deallocate_entity(old_loc);
        if (moved_from.has_value()) {
            handle_swap_and_pop(arch_id, *moved_from, old_loc);
        }
    }

    // 更新记录
    entity_manager_.update_location(entity, target->id(), new_location);

    return true;
}

std::vector<Archetype*> World::find_archetypes_with(const ArchetypeSignature& required) {
    std::vector<Archetype*> result;

    for (auto& [hash, archetype] : archetypes_) {
        if (archetype->signature().contains_all(required)) {
            result.push_back(archetype.get());
        }
    }

    return result;
}

void World::handle_swap_and_pop(ArchetypeId archetype_id, const EntityLocation& from,
                                const EntityLocation& to) {
    // 查找被移动的实体（它原来在 from 位置，现在被移到了 to 位置）
    EntityId moved_entity = entity_manager_.find_entity_at(archetype_id, from);
    if (moved_entity.is_valid()) {
        entity_manager_.update_location(moved_entity, archetype_id, to);
    }
}

}  // namespace Corona::Kernel::ECS
