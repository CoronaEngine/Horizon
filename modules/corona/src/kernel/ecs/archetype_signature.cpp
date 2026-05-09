#include "corona/kernel/ecs/archetype_signature.h"

#include <algorithm>

namespace Corona::Kernel::ECS {

void ArchetypeSignature::add(ComponentTypeId type_id) {
    // 使用二分查找检查是否已存在
    auto it = std::lower_bound(type_ids_.begin(), type_ids_.end(), type_id);
    if (it != type_ids_.end() && *it == type_id) {
        return;  // 已存在，不重复添加
    }

    // 插入到正确位置，保持排序
    type_ids_.insert(it, type_id);
    update_hash();
}

void ArchetypeSignature::remove(ComponentTypeId type_id) {
    auto it = std::lower_bound(type_ids_.begin(), type_ids_.end(), type_id);
    if (it != type_ids_.end() && *it == type_id) {
        type_ids_.erase(it);
        update_hash();
    }
}

bool ArchetypeSignature::contains(ComponentTypeId type_id) const {
    return std::binary_search(type_ids_.begin(), type_ids_.end(), type_id);
}

bool ArchetypeSignature::contains_all(const ArchetypeSignature& other) const {
    // 使用 std::includes 检查是否包含所有元素（两个序列都已排序）
    return std::includes(type_ids_.begin(), type_ids_.end(), other.type_ids_.begin(),
                         other.type_ids_.end());
}

bool ArchetypeSignature::contains_any(const ArchetypeSignature& other) const {
    // 使用双指针遍历两个有序序列
    auto it1 = type_ids_.begin();
    auto it2 = other.type_ids_.begin();

    while (it1 != type_ids_.end() && it2 != other.type_ids_.end()) {
        if (*it1 < *it2) {
            ++it1;
        } else if (*it1 > *it2) {
            ++it2;
        } else {
            return true;  // 找到相同元素
        }
    }
    return false;
}

std::size_t ArchetypeSignature::hash() const {
    return cached_hash_;
}

void ArchetypeSignature::clear() {
    type_ids_.clear();
    cached_hash_ = 0;
}

bool ArchetypeSignature::operator==(const ArchetypeSignature& other) const {
    return type_ids_ == other.type_ids_;
}

auto ArchetypeSignature::operator<=>(const ArchetypeSignature& other) const {
    return type_ids_ <=> other.type_ids_;
}

void ArchetypeSignature::update_hash() {
    // 使用 FNV-1a 哈希算法组合所有类型 ID
    std::size_t hash = 14695981039346656037ULL;  // FNV offset basis
    constexpr std::size_t fnv_prime = 1099511628211ULL;

    for (auto type_id : type_ids_) {
        hash ^= type_id;
        hash *= fnv_prime;
    }

    cached_hash_ = hash;
}

}  // namespace Corona::Kernel::ECS
