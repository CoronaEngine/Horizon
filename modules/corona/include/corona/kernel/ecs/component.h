#pragma once
#include <cstring>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

#include "corona/pal/cfw_platform.h"
#include "ecs_types.h"

namespace Corona::Kernel::ECS {

/**
 * @brief 组件类型约束（C++20 概念）
 *
 * 定义了作为组件的类型必须满足的要求：
 * - 可默认构造：组件在分配时需要默认初始化
 * - 可移动构造：支持高效的组件传递
 * - 可析构：组件生命周期管理
 *
 * 示例：
 * @code
 * struct Position {
 *     float x = 0.0f, y = 0.0f, z = 0.0f;
 * };  // 满足 Component 概念
 * @endcode
 */
template <typename T>
concept Component = std::is_default_constructible_v<T> && std::is_move_constructible_v<T> &&
                    std::is_destructible_v<T>;

/**
 * @brief 组件类型信息
 *
 * 存储组件类型的元信息，包括大小、对齐、以及类型擦除的操作函数指针。
 * 用于在运行时管理不同类型组件的生命周期。
 */
struct ComponentTypeInfo {
    ComponentTypeId id = kInvalidComponentTypeId;  ///< 类型唯一标识
    std::size_t size = 0;                          ///< sizeof(T)
    std::size_t alignment = 0;                     ///< alignof(T)
    std::string_view name;                         ///< 类型名称（调试用）

    /// 默认构造函数
    void (*construct)(void* dst) = nullptr;

    /// 析构函数
    void (*destruct)(void* dst) = nullptr;

    /// 移动构造函数
    void (*move_construct)(void* dst, void* src) = nullptr;

    /// 移动赋值函数
    void (*move_assign)(void* dst, void* src) = nullptr;

    /// 拷贝构造函数（可选，非 trivially copyable 类型需要）
    void (*copy_construct)(void* dst, const void* src) = nullptr;

    /// 是否为 trivially copyable
    bool is_trivially_copyable = false;

    /// 是否为 trivially destructible
    bool is_trivially_destructible = false;

    [[nodiscard]] bool is_valid() const { return id != kInvalidComponentTypeId && size > 0; }
};

namespace detail {

/// 获取类型名称的辅助函数
template <typename T>
[[nodiscard]] constexpr std::string_view get_type_name() {
#if defined(CFW_COMPILER_CLANG)
    constexpr std::string_view prefix = "[T = ";
    constexpr std::string_view suffix = "]";
    constexpr std::string_view function = __PRETTY_FUNCTION__;
#elif defined(CFW_COMPILER_GCC)
    constexpr std::string_view prefix = "with T = ";
    constexpr std::string_view suffix = "]";
    constexpr std::string_view function = __PRETTY_FUNCTION__;
#elif defined(CFW_COMPILER_MSVC)
    constexpr std::string_view prefix = "get_type_name<";
    constexpr std::string_view suffix = ">(void)";
    constexpr std::string_view function = __FUNCSIG__;
#else
    return typeid(T).name();
#endif

#if defined(CFW_COMPILER_CLANG) || defined(CFW_COMPILER_GCC) || defined(CFW_COMPILER_MSVC)
    const auto start = function.find(prefix) + prefix.size();
    const auto end = function.rfind(suffix);
    return function.substr(start, end - start);
#endif
}

/// 构造函数包装器
template <Component T>
void construct_impl(void* dst) {
    new (dst) T();
}

/// 析构函数包装器
template <Component T>
void destruct_impl(void* dst) {
    static_cast<T*>(dst)->~T();
}

/// 移动构造函数包装器
template <Component T>
void move_construct_impl(void* dst, void* src) {
    new (dst) T(std::move(*static_cast<T*>(src)));
}

/// 移动赋值函数包装器
template <Component T>
void move_assign_impl(void* dst, void* src) {
    *static_cast<T*>(dst) = std::move(*static_cast<T*>(src));
}

/// 拷贝构造函数包装器
template <Component T>
void copy_construct_impl(void* dst, const void* src) {
    if constexpr (std::is_copy_constructible_v<T>) {
        new (dst) T(*static_cast<const T*>(src));
    }
}

}  // namespace detail

/**
 * @brief 获取组件类型 ID
 *
 * 基于 std::type_index 的哈希值生成唯一的类型 ID。
 *
 * @tparam T 组件类型
 * @return 组件类型 ID
 */
template <Component T>
[[nodiscard]] ComponentTypeId get_component_type_id() {
    return std::type_index(typeid(T)).hash_code();
}

/**
 * @brief 获取组件类型信息
 *
 * 返回指定类型的完整元信息，包括大小、对齐、操作函数等。
 * 使用静态局部变量确保每个类型只初始化一次。
 *
 * @tparam T 组件类型
 * @return 组件类型信息的常量引用
 */
template <Component T>
[[nodiscard]] const ComponentTypeInfo& get_component_type_info() {
    static const ComponentTypeInfo info = []() {
        ComponentTypeInfo result;
        result.id = get_component_type_id<T>();
        result.size = sizeof(T);
        result.alignment = alignof(T);
        result.name = detail::get_type_name<T>();
        result.construct = detail::construct_impl<T>;
        result.destruct = detail::destruct_impl<T>;
        result.move_construct = detail::move_construct_impl<T>;
        result.move_assign = detail::move_assign_impl<T>;
        result.is_trivially_copyable = std::is_trivially_copyable_v<T>;
        result.is_trivially_destructible = std::is_trivially_destructible_v<T>;

        if constexpr (std::is_copy_constructible_v<T>) {
            result.copy_construct = detail::copy_construct_impl<T>;
        }

        return result;
    }();
    return info;
}

/**
 * @brief 组件类型注册表
 *
 * 管理所有已注册的组件类型信息，用于运行时类型查找。
 */
class ComponentRegistry {
   public:
    /// 获取单例实例
    static ComponentRegistry& instance() {
        static ComponentRegistry registry;
        return registry;
    }

    /// 注册组件类型
    template <Component T>
    void register_component() {
        const auto& info = get_component_type_info<T>();
        type_infos_[info.id] = &info;
    }

    /// 获取组件类型信息
    [[nodiscard]] const ComponentTypeInfo* get_type_info(ComponentTypeId id) const {
        auto it = type_infos_.find(id);
        return it != type_infos_.end() ? it->second : nullptr;
    }

    /// 检查类型是否已注册
    [[nodiscard]] bool is_registered(ComponentTypeId id) const {
        return type_infos_.find(id) != type_infos_.end();
    }

   private:
    ComponentRegistry() = default;
    std::unordered_map<ComponentTypeId, const ComponentTypeInfo*> type_infos_;
};

/// 便捷的组件注册宏
#define CORONA_REGISTER_COMPONENT(Type) \
    Corona::Kernel::ECS::ComponentRegistry::instance().register_component<Type>()

}  // namespace Corona::Kernel::ECS
