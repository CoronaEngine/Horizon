//
// Created by Zero on 24/04/2022.
//

#pragma once

#include <filesystem>
#include <cstdint>
#include <cassert>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include "core/util/macro_map.h"
#include "core/runtime/oc_windows.h"

#ifdef OC_AST_EXPORT_DLL
#define OC_AST_API __declspec(dllexport)
#else
#define OC_AST_API __declspec(dllimport)
#endif

#ifdef OC_CORE_EXPORT_DLL
#define OC_CORE_API __declspec(dllexport)
#else
#define OC_CORE_API __declspec(dllimport)
#endif

#ifdef OC_DSL_EXPORT_DLL
#define OC_DSL_API __declspec(dllexport)
#else
#define OC_DSL_API __declspec(dllimport)
#endif

#ifdef OC_GENERATOR_EXPORT_DLL
#define OC_GENERATOR_API __declspec(dllexport)
#else
#define OC_GENERATOR_API __declspec(dllimport)
#endif

#ifdef OC_RHI_EXPORT_DLL
#define OC_RHI_API __declspec(dllexport)
#else
#define OC_RHI_API __declspec(dllimport)
#endif

#ifdef OC_BACKENDS_EXPORT_DLL
#define OC_BACKENDS_API __declspec(dllexport)
#else
#define OC_BACKENDS_API __declspec(dllimport)
#endif

#ifdef OC_UTIL_EXPORT_DLL
#define OC_UTIL_API __declspec(dllexport)
#else
#define OC_UTIL_API __declspec(dllimport)
#endif

#ifdef OC_MATH_EXPORT_DLL
#define OC_MATH_API __declspec(dllexport)
#else
#define OC_MATH_API __declspec(dllimport)
#endif

#ifdef OC_GUI_EXPORT_DLL
#define OC_GUI_API __declspec(dllexport)
#else
#define OC_GUI_API __declspec(dllimport)
#endif

#ifdef OC_EXT_EXPORT_DLL
#define OC_EXT_API __declspec(dllexport)
#else
#define OC_EXT_API __declspec(dllimport)
#endif

#ifdef _MSC_VER
#define OC_OFFSET_OF(type, member) __builtin_offsetof(type, member)
#else
#define OC_OFFSET_OF(type, member) offsetof(type, member)
#endif

#ifdef __cplusplus
#define OC_EXTERN_C extern "C"
#define OC_NOEXCEPT noexcept
#else
#define OC_EXTERN_C
#define OC_NOEXCEPT
#endif

#ifdef _MSC_VER
#define OC_FORCE_INLINE inline
#define OC_NEVER_INLINE __declspec(noinline)
#define OC_DLL
#define OC_EXPORT_API OC_EXTERN_C __declspec(dllexport)
#define OC_IMPORT_API OC_EXTERN_C __declspec(dllimport)
#else
#define OC_FORCE_INLINE [[gnu::always_inline, gnu::hot]] inline
#define OC_NEVER_INLINE [[gnu::noinline]]
#define OC_DLL
#define OC_EXPORT_API OC_EXTERN_C [[gnu::visibility("default")]]
#define OC_IMPORT_API OC_EXTERN_C
#endif

#define OC_USING_SV using namespace std::string_view_literals;

#define OC_ASSERT(...) assert(__VA_ARGS__)

namespace ocarina {
using handle_ty = uint64_t;
}

#define OC_NDSC_INLINE [[nodiscard]] inline
#define OC_NODISCARD [[nodiscard]]

#define TYPE_PREFIX "oc_"

#define OC_MAKE_MEMBER_GETTER(member, modifier)                                     \
    [[nodiscard]] const auto modifier member() const noexcept { return member##_; } \
    [[nodiscard]] auto modifier member() noexcept { return member##_; }

#define OC_MAKE_MEMBER_SETTER(member)                     \
    void set_##member(decltype(member##_) val) noexcept { \
        member##_ = ocarina::move(val);                   \
    }

#define OC_MAKE_MEMBER_GETTER_SETTER(member, modifier) \
    OC_MAKE_MEMBER_GETTER(member, modifier)            \
    OC_MAKE_MEMBER_SETTER(member)

#define OC_COMMA ,

#define OC_MAKE_ENUM_BIT_OPS_IMPL(op, type)                                                   \
    inline auto operator op(type lhs, type rhs) {                                             \
        return static_cast<type>(ocarina::to_underlying(lhs) op ocarina::to_underlying(rhs)); \
    }

#define OC_MAKE_ENUM_BIT_OPS(type, ...) \
    MAP_UD(OC_MAKE_ENUM_BIT_OPS_IMPL, type, ##__VA_ARGS__)

#define OC_MAKE_AUTO_MEMBER_FUNC(func)                           \
    template<typename T, typename... Args>                       \
    auto func(T &&obj, Args &&...args) noexcept {                \
        if constexpr (requires {                                 \
                          obj.func(OC_FORWARD(args)...);         \
                      }) {                                       \
            return obj.func(OC_FORWARD(args)...);                \
        } else if constexpr (requires {                          \
                                 obj->func(OC_FORWARD(args)...); \
                             }) {                                \
            return obj->func(OC_FORWARD(args)...);               \
        } else {                                                 \
            static_assert(ocarina::always_false_v<T>);           \
        }                                                        \
    }

#define OC_MAKE_INSTANCE_CONSTRUCTOR(ClassName, static_ptr) \
private:                                                    \
    static ClassName *static_ptr;                           \
    ClassName() = default;                                  \
                                                            \
public:                                                     \
    ClassName(const ClassName &) = delete;                  \
    ClassName(ClassName &&) = delete;                       \
    ClassName operator=(const ClassName &) = delete;        \
    ClassName operator=(ClassName &&) = delete;

#define OC_MAKE_INSTANCE_FUNC_DECL(ClassName)   \
public:                                         \
    [[nodiscard]] static ClassName &instance(); \
    static void destroy_instance();

#define OC_MAKE_INSTANCE_FUNC_DEF_WITH_HOTFIX(ClassName, s_var_name)              \
    ClassName *ClassName::s_var_name = nullptr;                                   \
    ClassName &ClassName::instance() {                                            \
        if (s_var_name == nullptr) {                                              \
            s_var_name = new ClassName();                                         \
            HotfixSystem::instance().register_static_var(#ClassName, s_var_name); \
        }                                                                         \
        return *s_var_name;                                                       \
    }                                                                             \
                                                                                  \
    void ClassName::destroy_instance() {                                          \
        if (s_var_name) {                                                         \
            HotfixSystem::instance().unregister_static_var(#ClassName);           \
            delete s_var_name;                                                    \
            s_var_name = nullptr;                                                 \
        }                                                                         \
    }

#define OC_MAKE_INSTANCE_FUNC_DEF(ClassName, s_var_name) \
    ClassName *ClassName::s_var_name = nullptr;          \
    ClassName &ClassName::instance() {                   \
        if (s_var_name == nullptr) {                     \
            s_var_name = new ClassName();                \
        }                                                \
        return *s_var_name;                              \
    }                                                    \
                                                         \
    void ClassName::destroy_instance() {                 \
        if (s_var_name) {                                \
            delete s_var_name;                           \
            s_var_name = nullptr;                        \
        }                                                \
    }
