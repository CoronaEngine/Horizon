//
// Created by ling.zhu on 2025/6/27.
//

#pragma once

#include <utility>

#include "core/stl.h"
#include "math/basic_types.h"

namespace ocarina {
namespace detail {
template<typename R, typename... Args>
using function_signature = R(Args...);

template<typename T>
struct canonical_signature;

#define OC_MAKE_FUNC_SIGNATURE(...)                        \
    template<typename Ret, typename... Args>               \
    struct canonical_signature<Ret(Args...) __VA_ARGS__> { \
        using type = function_signature<Ret, Args...>;     \
        static constexpr auto arg_count = sizeof...(Args); \
        using ret_type = Ret;                              \
    };
OC_MAKE_FUNC_SIGNATURE()
OC_MAKE_FUNC_SIGNATURE(noexcept)
#undef OC_MAKE_FUNC_SIGNATURE

#define OC_MAKE_FUNC_PTR_SIGNATURE(...)                      \
    template<typename Ret, typename... Args>                 \
    struct canonical_signature<Ret (*)(Args...) __VA_ARGS__> \
        : canonical_signature<Ret(Args...)> {};
OC_MAKE_FUNC_PTR_SIGNATURE()
OC_MAKE_FUNC_PTR_SIGNATURE(noexcept)
#undef OC_MAKE_FUNC_PTR_SIGNATURE

template<typename F>
struct canonical_signature
    : canonical_signature<decltype(&F::operator())> {};

#define OC_MAKE_MEMBER_FUNC_SIGNATURE(...)                        \
    template<typename Ret, typename Cls, typename... Args>        \
    struct canonical_signature<Ret (Cls::*)(Args...) __VA_ARGS__> \
        : canonical_signature<Ret(Args...)> {};

OC_MAKE_MEMBER_FUNC_SIGNATURE()
OC_MAKE_MEMBER_FUNC_SIGNATURE(const)
OC_MAKE_MEMBER_FUNC_SIGNATURE(volatile)
OC_MAKE_MEMBER_FUNC_SIGNATURE(noexcept)
OC_MAKE_MEMBER_FUNC_SIGNATURE(const noexcept)
OC_MAKE_MEMBER_FUNC_SIGNATURE(const volatile)
OC_MAKE_MEMBER_FUNC_SIGNATURE(volatile noexcept)
OC_MAKE_MEMBER_FUNC_SIGNATURE(const volatile noexcept)

#undef OC_MAKE_MEMBER_FUNC_SIGNATURE

}// namespace detail

template<typename T>
using canonical_signature = detail::canonical_signature<std::remove_cvref_t<T>>;

template<typename T>
using canonical_signature_t = typename canonical_signature<T>::type;

namespace detail {
template<typename T>
struct dsl_function {
    using type = typename dsl_function<
        canonical_signature_t<std::remove_cvref_t<T>>>::type;
};
}// namespace detail
}// namespace ocarina