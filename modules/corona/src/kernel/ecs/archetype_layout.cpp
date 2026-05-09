#include "corona/kernel/ecs/archetype_layout.h"

#include <algorithm>

namespace Corona::Kernel::ECS {

namespace {

/// 向上对齐到指定对齐边界
[[nodiscard]] std::size_t align_up(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

ArchetypeLayout ArchetypeLayout::calculate(const ArchetypeSignature& signature,
                                           std::size_t chunk_size) {
    ArchetypeLayout layout;

    if (signature.empty()) {
        return layout;
    }

    // 收集所有组件类型信息
    std::vector<const ComponentTypeInfo*> type_infos;
    type_infos.reserve(signature.size());

    std::size_t total_size = 0;
    std::size_t max_alignment = 1;

    for (auto type_id : signature) {
        const auto* info = ComponentRegistry::instance().get_type_info(type_id);
        if (!info) {
            // 类型未注册，尝试通过其他方式获取（这里简化处理，实际应该报错）
            continue;
        }
        type_infos.push_back(info);
        total_size += info->size;
        max_alignment = std::max(max_alignment, info->alignment);
    }

    if (type_infos.empty()) {
        return layout;
    }

    // 计算每个实体的对齐后大小
    layout.total_size_per_entity = align_up(total_size, max_alignment);

    // 计算每个 Chunk 可容纳的实体数
    layout.entities_per_chunk = chunk_size / layout.total_size_per_entity;
    if (layout.entities_per_chunk == 0) {
        // 单个实体太大，至少容纳一个
        layout.entities_per_chunk = 1;
    }

    // 计算 SoA 布局中每个组件数组的偏移
    // 布局：[CompA * N][CompB * N][CompC * N]...
    std::size_t current_offset = 0;
    layout.components.reserve(type_infos.size());

    for (const auto* info : type_infos) {
        // 对齐到组件的对齐要求
        current_offset = align_up(current_offset, info->alignment);

        ComponentLayout comp_layout;
        comp_layout.type_id = info->id;
        comp_layout.array_offset = current_offset;
        comp_layout.size = info->size;
        comp_layout.alignment = info->alignment;
        comp_layout.type_info = info;

        layout.components.push_back(comp_layout);

        // 移动到下一个组件数组的起始位置
        current_offset += info->size * layout.entities_per_chunk;
    }

    layout.chunk_data_size = current_offset;

    return layout;
}

const ComponentLayout* ArchetypeLayout::find_component(ComponentTypeId type_id) const {
    for (const auto& comp : components) {
        if (comp.type_id == type_id) {
            return &comp;
        }
    }
    return nullptr;
}

std::ptrdiff_t ArchetypeLayout::get_array_offset(ComponentTypeId type_id) const {
    const auto* comp = find_component(type_id);
    if (comp) {
        return static_cast<std::ptrdiff_t>(comp->array_offset);
    }
    return -1;
}

}  // namespace Corona::Kernel::ECS
