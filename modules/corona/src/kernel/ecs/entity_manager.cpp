#include "corona/kernel/ecs/entity_manager.h"

#include <cassert>

namespace Corona::Kernel::ECS {

EntityManager::EntityManager(std::size_t initial_capacity) {
    reserve(initial_capacity);
}

EntityId EntityManager::create() {
    EntityId::IndexType index;
    EntityId::GenerationType generation;

    if (!free_list_.empty()) {
        // 从空闲列表复用索引
        index = free_list_.back();
        free_list_.pop_back();

        auto& record = records_[index];
        // 版本号已在 destroy 时递增
        generation = record.generation;
    } else {
        // 分配新索引
        index = static_cast<EntityId::IndexType>(records_.size());
        records_.emplace_back();

        auto& record = records_[index];
        record.generation = 1;  // 初始版本号为 1（0 表示无效）
        generation = record.generation;
    }

    // 标记为已分配（archetype_id 将由外部设置）
    // 此时 archetype_id 仍为 kInvalidArchetypeId，但 generation 有效
    ++alive_count_;

    return EntityId(index, generation);
}

bool EntityManager::destroy(EntityId id) {
    if (!is_alive(id)) {
        return false;
    }

    auto& record = records_[id.index()];

    // 重置记录
    record.archetype_id = kInvalidArchetypeId;
    record.location = EntityLocation{};

    // 递增版本号，使旧引用失效
    ++record.generation;

    // 加入空闲列表
    free_list_.push_back(id.index());

    --alive_count_;
    return true;
}

bool EntityManager::is_alive(EntityId id) const {
    if (!is_valid_id(id)) {
        return false;
    }

    const auto& record = records_[id.index()];

    // 检查版本号是否匹配，且记录标记为存活
    // 注意：新创建的实体 archetype_id 可能为 kInvalidArchetypeId
    // 但 generation 匹配表示该 ID 当前有效
    return record.generation == id.generation() && record.generation != 0;
}

EntityRecord* EntityManager::get_record(EntityId id) {
    if (!is_valid_id(id)) {
        return nullptr;
    }

    auto& record = records_[id.index()];
    if (record.generation != id.generation()) {
        return nullptr;
    }

    return &record;
}

const EntityRecord* EntityManager::get_record(EntityId id) const {
    return const_cast<EntityManager*>(this)->get_record(id);
}

bool EntityManager::update_location(EntityId id, ArchetypeId archetype_id,
                                    const EntityLocation& location) {
    auto* record = get_record(id);
    if (!record) {
        return false;
    }

    record->archetype_id = archetype_id;
    record->location = location;
    return true;
}

EntityId EntityManager::find_entity_at(ArchetypeId archetype_id,
                                       const EntityLocation& location) const {
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& record = records_[i];
        if (record.archetype_id == archetype_id && record.location == location &&
            record.generation != 0) {
            return EntityId(static_cast<EntityId::IndexType>(i), record.generation);
        }
    }
    return kInvalidEntity;
}

void EntityManager::reserve(std::size_t capacity) {
    if (capacity > records_.size()) {
        records_.reserve(capacity);
        free_list_.reserve(capacity);
    }
}

void EntityManager::clear() {
    // 保留容量但重置所有状态
    for (auto& record : records_) {
        record.clear();
    }
    free_list_.clear();
    alive_count_ = 0;
}

void EntityManager::grow(std::size_t new_capacity) {
    records_.reserve(new_capacity);
}

bool EntityManager::is_valid_id(EntityId id) const {
    if (!id.is_valid()) {
        return false;
    }

    if (id.index() >= records_.size()) {
        return false;
    }

    return true;
}

}  // namespace Corona::Kernel::ECS
