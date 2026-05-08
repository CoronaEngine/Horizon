
#pragma once

#include "cuda_device_std.h"


oc_half oc_float2half(oc_float f) {
    return __float2half(f);
}

oc_float oc_half2float(oc_half h) {
    return __half2float(h);
}

template<typename Dst, typename Src>
Dst oc_static_cast(Src src) {
    return static_cast<Dst>(src);
}

namespace ocarina {
namespace detail {
template<typename T>
struct is_half_op_enable : ocarina::false_type {};

template<>
struct is_half_op_enable<float> : ocarina::true_type {};

template<>
struct is_half_op_enable<int> : ocarina::true_type {};

template<>
struct is_half_op_enable<oc_uint> : ocarina::true_type {};

template<>
struct is_half_op_enable<double> : ocarina::true_type {};
}// namespace detail

template<typename T>
static constexpr auto is_half_op_enable_v = detail::is_half_op_enable<remove_cvref_t<T>>::value;

namespace detail {

template<typename T, ocarina::enable_if_t<is_half_op_enable_v<T>, int> = 0>
struct binary_op_half_target {
    using type = ocarina::conditional_t<is_integral_v<T>, half, T>;
};

}// namespace detail

template<typename T>
using binary_op_half_target_t = typename detail::binary_op_half_target<ocarina::remove_cvref_t<T>>::type;

template<typename P, typename T, typename F, enable_if_t<is_selectable<T, F>::value, int> = 0>
constexpr auto select(P condition, T t, F f) noexcept {
    using ret_type = decltype(t + f);
    return static_cast<bool>(condition) ? static_cast<ret_type>(t) : static_cast<ret_type>(f);
}

}// namespace ocarina

// Half operator implementations
#define OC_HALF_BINARY_AND_ASSIGN_OP(op)                                                 \
    template<typename T, ocarina::enable_if_t<ocarina::is_half_op_enable_v<T>, int> = 0> \
    constexpr ocarina::binary_op_half_target_t<T>                                        \
    operator op(oc_half lhs, T rhs) {                                                    \
        using ret_type = ocarina::binary_op_half_target_t<T>;                            \
        return oc_static_cast<ret_type>(lhs) op oc_static_cast<ret_type>(rhs);           \
    }                                                                                    \
    template<typename T, ocarina::enable_if_t<ocarina::is_half_op_enable_v<T>, int> = 0> \
    constexpr ocarina::binary_op_half_target_t<T>                                        \
    operator op(T lhs, oc_half rhs) {                                                    \
        using ret_type = ocarina::binary_op_half_target_t<T>;                            \
        return oc_static_cast<ret_type>(lhs) op oc_static_cast<ret_type>(rhs);           \
    }                                                                                    \
    template<typename T, ocarina::enable_if_t<ocarina::is_half_op_enable_v<T>, int> = 0> \
    constexpr oc_half &                                                                  \
    operator op## = (oc_half & lhs, T rhs) {                                             \
        lhs = lhs op rhs;                                                                \
        return lhs;                                                                      \
    }

OC_HALF_BINARY_AND_ASSIGN_OP(+)
OC_HALF_BINARY_AND_ASSIGN_OP(-)
OC_HALF_BINARY_AND_ASSIGN_OP(*)
OC_HALF_BINARY_AND_ASSIGN_OP(/)

#undef OC_HALF_BINARY_AND_ASSIGN_OP

#define OC_HALF_COMPARE_OP(op)                                                           \
    template<typename T, ocarina::enable_if_t<ocarina::is_half_op_enable_v<T>, int> = 0> \
    constexpr bool operator op(oc_half lhs, T rhs) {                                     \
        using type = ocarina::binary_op_half_target_t<T>;                                \
        return oc_static_cast<type>(lhs) op oc_static_cast<type>(rhs);                   \
    }                                                                                    \
    template<typename T, ocarina::enable_if_t<ocarina::is_half_op_enable_v<T>, int> = 0> \
    constexpr bool operator op(T lhs, oc_half rhs) {                                     \
        using type = ocarina::binary_op_half_target_t<T>;                                \
        return oc_static_cast<type>(lhs) op oc_static_cast<type>(rhs);                   \
    }

OC_HALF_COMPARE_OP(==)
OC_HALF_COMPARE_OP(!=)
OC_HALF_COMPARE_OP(>)
OC_HALF_COMPARE_OP(<)
OC_HALF_COMPARE_OP(>=)
OC_HALF_COMPARE_OP(<=)

#undef OC_HALF_COMPARE_OP