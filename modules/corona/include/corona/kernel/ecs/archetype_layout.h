#pragma once
#include <vector>

#include "archetype_signature.h"

namespace Corona::Kernel::ECS {

/**
 * @brief 单个组件在 Chunk 内的布局信息
 *
 * 描述组件数组在 Chunk 内存中的位置和属性。
 */
struct ComponentLayout {
    ComponentTypeId type_id = kInvalidComponentTypeId;  ///< 组件类型 ID
    std::size_t array_offset = 0;                       ///< 组件数组在 Chunk 内的起始偏移
    std::size_t size = 0;                               ///< 单个组件大小
    std::size_t alignment = 0;                          ///< 对齐要求
    const ComponentTypeInfo* type_info = nullptr;       ///< 类型信息指针

    [[nodiscard]] bool is_valid() const {
        return type_id != kInvalidComponentTypeId && size > 0;
    }
};

/**
 * @brief Archetype 内存布局
 *
 * 采用 SoA（Structure of Arrays）布局，每个组件类型的数据连续存储：
 *
 * ```
 * Chunk:
 * [CompA_0][CompA_1]...[CompA_N] | [CompB_0][CompB_1]...[CompB_N] | ...
 * ```
 *
 * 这种布局的优势：
 * - 缓存友好：遍历单个组件类型时数据连续
 * - SIMD 友好：同类型数据连续，便于向量化
 * - 灵活对齐：每个组件数组独立对齐
 */
struct ArchetypeLayout {
    std::vector<ComponentLayout> components;  ///< 各组件布局信息
    std::size_t total_size_per_entity = 0;    ///< 每个实体所有组件的总大小
    std::size_t entities_per_chunk = 0;       ///< 每个 Chunk 可容纳的实体数
    std::size_t chunk_data_size = 0;          ///< Chunk 实际数据区大小

    /**
     * @brief 计算 Archetype 布局
     *
     * 根据签名中的组件类型计算内存布局，确保每个组件数组正确对齐。
     *
     * @param signature 组件签名
     * @param chunk_size Chunk 大小（默认 16KB）
     * @return 计算后的布局信息
     */
    [[nodiscard]] static ArchetypeLayout calculate(const ArchetypeSignature& signature,
                                                   std::size_t chunk_size = kDefaultChunkSize);

    /**
     * @brief 查找指定组件的布局信息
     * @param type_id 组件类型 ID
     * @return 组件布局指针，未找到返回 nullptr
     */
    [[nodiscard]] const ComponentLayout* find_component(ComponentTypeId type_id) const;

    /**
     * @brief 查找指定组件的布局信息（模板版本）
     * @tparam T 组件类型
     * @return 组件布局指针，未找到返回 nullptr
     */
    template <Component T>
    [[nodiscard]] const ComponentLayout* find_component() const {
        return find_component(get_component_type_id<T>());
    }

    /**
     * @brief 获取组件在 Chunk 中的数组起始偏移
     * @param type_id 组件类型 ID
     * @return 偏移量，类型不存在返回 -1
     */
    [[nodiscard]] std::ptrdiff_t get_array_offset(ComponentTypeId type_id) const;

    /**
     * @brief 检查布局是否有效
     * @return 有效返回 true
     */
    [[nodiscard]] bool is_valid() const { return !components.empty() && entities_per_chunk > 0; }

    /**
     * @brief 获取组件数量
     * @return 组件类型数量
     */
    [[nodiscard]] std::size_t component_count() const { return components.size(); }
};

}  // namespace Corona::Kernel::ECS
