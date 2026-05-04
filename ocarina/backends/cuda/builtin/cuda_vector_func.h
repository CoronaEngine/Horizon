
#pragma once

#include "cuda_device_vector.h"
#include "cuda_device_builtin.h"

#define OC_MAKE_FUNCTION_GLOBAL(func_name)                                      \
    template<typename... Args>                                                  \
    [[nodiscard]] static constexpr auto oc_##func_name(Args... args) noexcept { \
        return ocarina::func_name(args...);                                     \
    }

namespace ocarina {

namespace detail {

template<size_t N, template<typename, size_t> typename Container,
         typename T, typename F, size_t... i>
[[nodiscard]] constexpr auto select_helper(Container<bool, N> pred, Container<T, N> t,
                                           Container<F, N> f, ocarina::index_sequence<i...>) {
    using scalar_type = decltype(ocarina::select(bool{}, T{}, F{}));
    return Container<scalar_type, N>{ocarina::select(pred[i], t[i], f[i])...};
}

}// namespace detail

template<size_t N, template<typename, size_t> typename Container, typename T, typename F>
[[nodiscard]] constexpr auto select(Container<bool, N> pred, Container<T, N> t,
                                    Container<F, N> f) {
    return detail::select_helper(pred, t, f, ocarina::make_index_sequence<N>());
}

template<size_t N, template<typename, size_t> typename Container, typename T, typename F>
[[nodiscard]] constexpr auto select(bool pred, Container<T, N> t,
                                    Container<F, N> f) {
    return select(Container<bool, N>(pred), t, f);
}

}// namespace ocarina

OC_MAKE_FUNCTION_GLOBAL(select)

namespace ocarina {

template<typename T, typename U, typename V,
         ocarina::enable_if_t<is_all_scalar_v<T, U, V>, int> = 0>
static constexpr auto lerp(T t, U a, V b) {
    return a + (b - a) * t;
}

namespace detail {
template<template<typename, size_t> typename Container, typename T,
         typename U, typename V, size_t N, size_t... i>
static constexpr auto lerp_helper(Container<T, N> t, Container<U, N> a,
                                  Container<V, N> b, ocarina::index_sequence<i...>) {
    using scalar_type = decltype(lerp(T{}, U{}, V{}));
    return Container<scalar_type, N>{lerp(t[i], a[i], b[i])...};
}
}// namespace detail

template<template<typename, size_t> typename Container, typename T, typename U, typename V,
         size_t N, ocarina::void_t<decltype(lerp(T{}, U{}, V{}))> * = nullptr>
static constexpr auto lerp(Container<T, N> t, Container<U, N> a, Container<V, N> b) {
    return detail::lerp_helper(t, a, b, ocarina::make_index_sequence<N>{});
}
}// namespace ocarina

OC_MAKE_FUNCTION_GLOBAL(lerp)

namespace ocarina {

template<typename T, typename U, typename V,
         ocarina::enable_if_t<is_all_scalar_v<T, U, V>, int> = 0>
static constexpr auto clamp(T v0, U v1, V v2) {
    return oc_min(v2, oc_max(v1, v0));
}

namespace detail {
template<template<typename, size_t> typename Container, typename T,
         typename U, typename V, size_t N, size_t... i>
static constexpr auto clamp_helper(Container<T, N> v0, Container<U, N> v1,
                                   Container<V, N> v2, ocarina::index_sequence<i...>) {
    using scalar_type = decltype(clamp(T{}, U{}, V{}));
    return Container<scalar_type, N>{clamp(v0[i], v1[i], v2[i])...};
}
}// namespace detail

template<template<typename, size_t> typename Container, typename T, typename U, typename V,
         size_t N, ocarina::void_t<decltype(clamp(T{}, U{}, V{}))> * = nullptr>
static constexpr auto clamp(Container<T, N> v0, Container<U, N> v1, Container<V, N> v2) {
    return detail::clamp_helper(v0, v1, v2, ocarina::make_index_sequence<N>{});
}

}// namespace ocarina

OC_MAKE_FUNCTION_GLOBAL(clamp)

namespace ocarina {

template<typename T, typename U, typename V,
         ocarina::enable_if_t<is_all_scalar_v<T, U, V>, int> = 0>
static constexpr auto fma(T v0, U v1, V v2) {
    using ret_type = decltype(T{} * U{} + V{});
    if constexpr (is_half_v<ret_type>) {
        return __hfma(v0, v1, v2);
    } else if constexpr (is_float_v<ret_type>) {
        return fmaf(v0, v1, v2);
    } else {
        static_assert(always_false_v<T>);
        return v0 * v1 + v2;
    }
}

namespace detail {
template<template<typename, size_t> typename Container, typename T,
         typename U, typename V, size_t N, size_t... i>
static constexpr auto fma_helper(Container<T, N> v0, Container<U, N> v1,
                                 Container<V, N> v2, ocarina::index_sequence<i...>) {
    using scalar_type = decltype(fma(T{}, U{}, V{}));
    return Container<scalar_type, N>{fma(v0[i], v1[i], v2[i])...};
}
}// namespace detail

template<template<typename, size_t> typename Container, typename T, typename U, typename V,
         size_t N, ocarina::void_t<decltype(fma(T{}, U{}, V{}))> * = nullptr>
static constexpr auto fma(Container<T, N> v0, Container<U, N> v1, Container<V, N> v2) {
    return detail::fma_helper(v0, v1, v2, ocarina::make_index_sequence<N>{});
}

}// namespace ocarina

OC_MAKE_FUNCTION_GLOBAL(fma)

namespace ocarina {

template<typename T, typename U, typename V,
         ocarina::enable_if_t<is_all_scalar_v<T, U, V>, int> = 0>
static constexpr auto inverse_lerp(T v0, U v1, V v2) {
    return (v0 - v1) / (v2 - v1);
}

namespace detail {
template<template<typename, size_t> typename Container, typename T,
         typename U, typename V, size_t N, size_t... i>
static constexpr auto inverse_lerp_helper(Container<T, N> v0, Container<U, N> v1,
                                          Container<V, N> v2, ocarina::index_sequence<i...>) {
    using scalar_type = decltype(inverse_lerp(T{}, U{}, V{}));
    return Container<scalar_type, N>{inverse_lerp(v0[i], v1[i], v2[i])...};
}
}// namespace detail

template<template<typename, size_t> typename Container, typename T, typename U, typename V,
         size_t N, ocarina::void_t<decltype(inverse_lerp(T{}, U{}, V{}))> * = nullptr>
static constexpr auto inverse_lerp(Container<T, N> v0, Container<U, N> v1, Container<V, N> v2) {
    return detail::inverse_lerp_helper(v0, v1, v2, ocarina::make_index_sequence<N>{});
}

}// namespace ocarina

OC_MAKE_FUNCTION_GLOBAL(inverse_lerp)

namespace ocarina {

namespace detail {
template<size_t N, template<typename, size_t> typename Container, size_t... i>
static constexpr auto any_helper(Container<bool, N> v) {
    return ((v[i]) || ...);
}
}// namespace detail
template<template<typename, size_t> typename Container, size_t N>
static constexpr auto any(Container<bool, N> v) {
    return detail::any_helper(v, ocarina::make_index_sequence<N>());
}

namespace detail {
template<size_t N, template<typename, size_t> typename Container, size_t... i>
static constexpr auto all_helper(Container<bool, N> v) {
    return ((v[i]) && ...);
}
}// namespace detail

template<template<typename, size_t> typename Container, size_t N>
static constexpr auto all(Container<bool, N> v) {
    return detail::all_helper(v, ocarina::make_index_sequence<N>());
}

template<template<typename, size_t> typename Container, size_t N>
static constexpr auto none(Container<bool, N> v) {
    return !any(v);
}
}// namespace ocarina

OC_MAKE_FUNCTION_GLOBAL(any)
OC_MAKE_FUNCTION_GLOBAL(all)
OC_MAKE_FUNCTION_GLOBAL(none)

namespace ocarina {

namespace detail {

template<template<typename, size_t> typename Container, size_t N,
         typename T, typename U, size_t... index>
static constexpr auto dot_helper(Container<T, N> a, Container<U, N> b,
                                 ocarina::index_sequence<index...>) {
    return ((a[index] * b[index]) + ...);
}
}// namespace detail

template<template<typename, size_t> typename Container,
         size_t N, typename T, typename U>
static constexpr auto dot(Container<T, N> lhs, Container<U, N> rhs) {
    return detail::dot_helper(lhs, rhs, ocarina::make_index_sequence<N>());
}

template<template<typename, size_t> typename Container,
         size_t N, typename T>
static constexpr auto length_squared(Container<T, N> v) {
    return dot(v, v);
}

template<template<typename, size_t> typename Container,
         size_t N, typename T>
static constexpr auto length(Container<T, N> v) {
    return oc_sqrt(length_squared(v));
}

template<template<typename, size_t> typename Container,
         size_t N, typename T, typename U>
static constexpr auto distance(Container<T, N> a, Container<U, N> b) {
    return length(a - b);
}

template<template<typename, size_t> typename Container,
         size_t N, typename T, typename U>
static constexpr auto distance_squared(Container<T, N> a, Container<U, N> b) {
    return length_squared(a - b);
}

template<template<typename, size_t> typename Container, size_t N, typename T>
static constexpr auto normalize(Container<T, N> v) {
    return v * oc_rsqrt(dot(v, v));
}

template<template<typename, size_t> typename Container, typename T, typename U>
static constexpr auto cross(Container<T, 3> a, Container<U, 3> b) {
    using scalar_type = decltype(a.x * b.x);
    return Container<scalar_type, 3>{a.y * b.z - a.z * b.y,
                                     a.z * b.x - a.x * b.z,
                                     a.x * b.y - a.y * b.x};
}

}// namespace ocarina

OC_MAKE_FUNCTION_GLOBAL(dot)
OC_MAKE_FUNCTION_GLOBAL(length_squared)
OC_MAKE_FUNCTION_GLOBAL(length)
OC_MAKE_FUNCTION_GLOBAL(distance)
OC_MAKE_FUNCTION_GLOBAL(distance_squared)
OC_MAKE_FUNCTION_GLOBAL(normalize)
OC_MAKE_FUNCTION_GLOBAL(cross)

#define OC_MAKE_ARRAY_BINARY_OPERATOR(op, name)                                    \
    template<typename T, typename U, size_t N, size_t... i>                        \
    auto array_##name##_impl(const oc_array<T, N> &lhs, const oc_array<U, N> &rhs, \
                             ocarina::index_sequence<i...>) {                      \
        using ret_type = decltype(T {} op U{});                                    \
        return oc_array<ret_type, N>{(lhs[i] op rhs[i])...};                       \
    }                                                                              \
                                                                                   \
    template<typename T, typename U, size_t N>                                     \
    auto operator op(oc_array<T, N> lhs, oc_array<U, N> rhs) {                     \
        return array_##name##_impl(lhs, rhs, ocarina::make_index_sequence<N>());   \
    }                                                                              \
                                                                                   \
    template<typename T, typename U>                                               \
    auto operator op(oc_array<T, 1> lhs, oc_array<U, 1> rhs) {                     \
        oc_array<decltype(T {} op U{}), 1> ret;                                    \
        ret[0] = lhs[0] op rhs[0];                                                 \
        return ret;                                                                \
    }                                                                              \
                                                                                   \
    template<typename T, typename U, size_t N, size_t... i>                        \
    auto array_##name##_impl(const oc_array<T, N> &lhs, const U &rhs,              \
                             ocarina::index_sequence<i...>) {                      \
        using ret_type = decltype(T {} op U{});                                    \
        return oc_array<ret_type, N>{(lhs[i] op rhs)...};                          \
    }                                                                              \
                                                                                   \
    template<typename T, typename U, size_t N>                                     \
    auto operator op(oc_array<T, N> lhs, U rhs) {                                  \
        return array_##name##_impl(lhs, rhs, ocarina::make_index_sequence<N>());   \
    }                                                                              \
                                                                                   \
    template<typename T, typename U, size_t N>                                     \
    auto operator op(oc_array<T, N> lhs, oc_array<U, 1> rhs) {                     \
        return lhs op rhs[0];                                                      \
    }                                                                              \
                                                                                   \
    template<typename T, typename U, size_t N, size_t... i>                        \
    auto array_##name##_impl(const T &lhs, const oc_array<U, N> &rhs,              \
                             ocarina::index_sequence<i...>) {                      \
        using ret_type = decltype(T {} op U{});                                    \
        return oc_array<ret_type, N>{(lhs op rhs[i])...};                          \
    }                                                                              \
                                                                                   \
    template<typename T, typename U, size_t N>                                     \
    auto operator op(T lhs, oc_array<U, N> rhs) {                                  \
        return array_##name##_impl(lhs, rhs, ocarina::make_index_sequence<N>());   \
    }                                                                              \
                                                                                   \
    template<typename T, typename U, size_t N>                                     \
    auto operator op(oc_array<T, 1> lhs, oc_array<U, N> rhs) {                     \
        return lhs[0] op rhs;                                                      \
    }

OC_MAKE_ARRAY_BINARY_OPERATOR(+, add)
OC_MAKE_ARRAY_BINARY_OPERATOR(-, sub)
OC_MAKE_ARRAY_BINARY_OPERATOR(*, mul)
OC_MAKE_ARRAY_BINARY_OPERATOR(/, div)
OC_MAKE_ARRAY_BINARY_OPERATOR(%, mod)
OC_MAKE_ARRAY_BINARY_OPERATOR(==, eq)
OC_MAKE_ARRAY_BINARY_OPERATOR(!=, ne)
OC_MAKE_ARRAY_BINARY_OPERATOR(>, gt)
OC_MAKE_ARRAY_BINARY_OPERATOR(<, lt)
OC_MAKE_ARRAY_BINARY_OPERATOR(>=, ge)
OC_MAKE_ARRAY_BINARY_OPERATOR(<=, le)
OC_MAKE_ARRAY_BINARY_OPERATOR(&&, logical_and)
OC_MAKE_ARRAY_BINARY_OPERATOR(||, logical_or)
OC_MAKE_ARRAY_BINARY_OPERATOR(&, bit_and)
OC_MAKE_ARRAY_BINARY_OPERATOR(|, bit_or)
OC_MAKE_ARRAY_BINARY_OPERATOR(^, bit_xor)
OC_MAKE_ARRAY_BINARY_OPERATOR(<<, lshift)
OC_MAKE_ARRAY_BINARY_OPERATOR(>>, rshift)

#undef OC_MAKE_ARRAY_BINARY_OPERATOR