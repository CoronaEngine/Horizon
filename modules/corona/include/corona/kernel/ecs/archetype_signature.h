#pragma once
#include <functional>
#include <vector>

#include "component.h"

namespace Corona::Kernel::ECS {

/**
 * @brief Archetype 类型签名
 *
 * 表示一个 Archetype 包含的组件类型集合。
 * 内部使用排序后的 ComponentTypeId 向量，确保相同组件组合产生相同的签名。
 *
 * 主要用途：
 * - 唯一标识 Archetype
 * - 快速判断组件包含关系
 * - 作为 Archetype 查找的 key
 *
 * 示例：
 * @code
 * auto sig = ArchetypeSignature::create<Position, Velocity, Health>();
 * if (sig.contains<Position>()) {
 *     // ...
 * }
 * @endcode
 */
class ArchetypeSignature {
   public:
    using Iterator = std::vector<ComponentTypeId>::const_iterator;

    ArchetypeSignature() = default;

    /**
     * @brief 从组件类型列表创建签名
     * @tparam Ts 组件类型列表
     * @return 包含指定组件类型的签名
     */
    template <Component... Ts>
    [[nodiscard]] static ArchetypeSignature create() {
        ArchetypeSignature sig;
        (sig.add(get_component_type_id<Ts>()), ...);
        return sig;
    }

    /**
     * @brief 从 ComponentTypeId 列表创建签名
     * @param type_ids 组件类型 ID 列表
     * @return 包含指定组件类型的签名
     */
    [[nodiscard]] static ArchetypeSignature create(std::initializer_list<ComponentTypeId> type_ids) {
        ArchetypeSignature sig;
        for (auto id : type_ids) {
            sig.add(id);
        }
        return sig;
    }

    /**
     * @brief 添加组件类型
     * @param type_id 要添加的组件类型 ID
     *
     * 如果类型已存在，则不重复添加。添加后保持排序。
     */
    void add(ComponentTypeId type_id);

    /**
     * @brief 添加组件类型（模板版本）
     * @tparam T 组件类型
     */
    template <Component T>
    void add() {
        add(get_component_type_id<T>());
    }

    /**
     * @brief 移除组件类型
     * @param type_id 要移除的组件类型 ID
     */
    void remove(ComponentTypeId type_id);

    /**
     * @brief 移除组件类型（模板版本）
     * @tparam T 组件类型
     */
    template <Component T>
    void remove() {
        remove(get_component_type_id<T>());
    }

    /**
     * @brief 检查是否包含指定组件类型
     * @param type_id 组件类型 ID
     * @return 包含返回 true
     */
    [[nodiscard]] bool contains(ComponentTypeId type_id) const;

    /**
     * @brief 检查是否包含指定组件类型（模板版本）
     * @tparam T 组件类型
     * @return 包含返回 true
     */
    template <Component T>
    [[nodiscard]] bool contains() const {
        return contains(get_component_type_id<T>());
    }

    /**
     * @brief 检查是否包含另一个签名的所有组件类型
     * @param other 另一个签名
     * @return 包含所有类型返回 true
     */
    [[nodiscard]] bool contains_all(const ArchetypeSignature& other) const;

    /**
     * @brief 检查是否包含另一个签名的任一组件类型
     * @param other 另一个签名
     * @return 包含任一类型返回 true
     */
    [[nodiscard]] bool contains_any(const ArchetypeSignature& other) const;

    /**
     * @brief 获取组件数量
     * @return 组件类型数量
     */
    [[nodiscard]] std::size_t size() const { return type_ids_.size(); }

    /**
     * @brief 判断是否为空
     * @return 无组件返回 true
     */
    [[nodiscard]] bool empty() const { return type_ids_.empty(); }

    /**
     * @brief 获取哈希值
     *
     * 用于 Archetype 快速查找，相同的组件组合产生相同的哈希值。
     *
     * @return 签名的哈希值
     */
    [[nodiscard]] std::size_t hash() const;

    /**
     * @brief 清空所有组件类型
     */
    void clear();

    /**
     * @brief 获取组件类型 ID 列表
     * @return 排序后的组件类型 ID 向量引用
     */
    [[nodiscard]] const std::vector<ComponentTypeId>& type_ids() const { return type_ids_; }

    // 比较运算符
    [[nodiscard]] bool operator==(const ArchetypeSignature& other) const;
    [[nodiscard]] auto operator<=>(const ArchetypeSignature& other) const;

    // 迭代器支持
    [[nodiscard]] Iterator begin() const { return type_ids_.begin(); }
    [[nodiscard]] Iterator end() const { return type_ids_.end(); }

   private:
    void update_hash();

    std::vector<ComponentTypeId> type_ids_;  ///< 排序后的组件类型 ID
    std::size_t cached_hash_ = 0;            ///< 缓存的哈希值
};

}  // namespace Corona::Kernel::ECS

// std::hash 特化
namespace std {
template <>
struct hash<Corona::Kernel::ECS::ArchetypeSignature> {
    std::size_t operator()(const Corona::Kernel::ECS::ArchetypeSignature& sig) const noexcept {
        return sig.hash();
    }
};
}  // namespace std
