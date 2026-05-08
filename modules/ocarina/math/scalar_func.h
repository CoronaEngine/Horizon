//
// Created by Zero on 2024/5/16.
//

#pragma once

#include "half.h"
#include "real.h"
#include "math/constants.h"
#include "core/concepts.h"
#include <numeric>

namespace ocarina {

#define OC_WRAP_STD_UNARY_SCALAR_FUNC(func)                  \
    template<typename T>                                     \
    requires is_scalar_v<T>                                  \
    [[nodiscard]] constexpr auto func(const T &v) noexcept { \
        if constexpr (is_half_v<T> || is_real_v<T>) {        \
            return T(std::func(static_cast<float>(v)));      \
        } else {                                             \
            return std::func(v);                             \
        }                                                    \
    }

#define OC_WRAP_STD_BINARY_SCALAR_FUNC(func)                                 \
    template<typename T>                                                     \
    requires is_scalar_v<T>                                                  \
    [[nodiscard]] constexpr auto func(const T &lhs, const T &rhs) noexcept { \
        if constexpr (is_half_v<T> || is_real_v<T>) {                        \
            return T(std::func(static_cast<float>(lhs),                      \
                               static_cast<float>(rhs)));                    \
        } else {                                                             \
            return std::func(lhs, rhs);                                      \
        }                                                                    \
    }

#define OC_WRAP_STD_TERNARY_SCALAR_FUNC(func)                                        \
    template<typename T>                                                             \
    requires is_scalar_v<T>                                                          \
    [[nodiscard]] constexpr auto func(const T &x, const T &y, const T &z) noexcept { \
        if constexpr (is_half_v<T> || is_real_v<T>) {                                \
            return T(std::func(static_cast<float>(x),                                \
                               static_cast<float>(y),                                \
                               static_cast<float>(z)));                              \
        } else {                                                                     \
            return std::func(x, y, z);                                               \
        }                                                                            \
    }

OC_WRAP_STD_UNARY_SCALAR_FUNC(abs)
OC_WRAP_STD_UNARY_SCALAR_FUNC(acos)
OC_WRAP_STD_UNARY_SCALAR_FUNC(acosh)
OC_WRAP_STD_UNARY_SCALAR_FUNC(asin)
OC_WRAP_STD_UNARY_SCALAR_FUNC(asinh)
OC_WRAP_STD_UNARY_SCALAR_FUNC(atan)
OC_WRAP_STD_UNARY_SCALAR_FUNC(atanh)
OC_WRAP_STD_UNARY_SCALAR_FUNC(ceil)
OC_WRAP_STD_UNARY_SCALAR_FUNC(cos)
OC_WRAP_STD_UNARY_SCALAR_FUNC(cosh)
OC_WRAP_STD_UNARY_SCALAR_FUNC(exp)
OC_WRAP_STD_UNARY_SCALAR_FUNC(exp2)
OC_WRAP_STD_UNARY_SCALAR_FUNC(floor)
OC_WRAP_STD_UNARY_SCALAR_FUNC(log)
OC_WRAP_STD_UNARY_SCALAR_FUNC(log10)
OC_WRAP_STD_UNARY_SCALAR_FUNC(log2)
OC_WRAP_STD_UNARY_SCALAR_FUNC(round)
OC_WRAP_STD_UNARY_SCALAR_FUNC(sin)
OC_WRAP_STD_UNARY_SCALAR_FUNC(sinh)
OC_WRAP_STD_UNARY_SCALAR_FUNC(tan)
OC_WRAP_STD_UNARY_SCALAR_FUNC(tanh)

OC_WRAP_STD_BINARY_SCALAR_FUNC(atan2)
OC_WRAP_STD_BINARY_SCALAR_FUNC(copysign)
OC_WRAP_STD_BINARY_SCALAR_FUNC(fmod)
OC_WRAP_STD_BINARY_SCALAR_FUNC(pow)

OC_WRAP_STD_TERNARY_SCALAR_FUNC(fma)

#undef OC_WRAP_STD_TERNARY_SCALAR_FUNC
#undef OC_WRAP_STD_BINARY_SCALAR_FUNC
#undef OC_WRAP_STD_UNARY_SCALAR_FUNC

using std::roundf;

template<typename T>
requires ocarina::is_scalar_v<T>
[[nodiscard]] constexpr T max(T a, T b) noexcept {
    return std::max(a, b);
}

[[nodiscard]] constexpr half max(half a, half b) noexcept {
    return half2float(a) > half2float(b) ? a : b;
}

[[nodiscard]] constexpr real max(real a, real b) noexcept {
    return real2float(a) > real2float(b) ? a : b;
}

template<typename T>
requires ocarina::is_scalar_v<T>
[[nodiscard]] constexpr auto min(T a, T b) noexcept {
    return std::min(a, b);
}

[[nodiscard]] constexpr half min(half a, half b) noexcept {
    return half2float(a) < half2float(b) ? a : b;
}

[[nodiscard]] constexpr real min(real a, real b) noexcept {
    return real2float(a) < real2float(b) ? a : b;
}

template<typename T, typename F>
requires concepts::selectable<T, F>
[[nodiscard]] constexpr auto select(bool pred, T &&t, F &&f) noexcept {
    using ret_type = decltype(std::declval<T>() + std::declval<F>());
    return pred ? ret_type(t) : ret_type(f);
}

template<typename T>
requires std::is_unsigned_v<T> && (sizeof(T) == 4u || sizeof(T) == 8u)
[[nodiscard]] constexpr auto next_pow2(T v) noexcept {
    v--;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    if constexpr (sizeof(T) == 8u) { v |= v >> 32u; }
    return v + 1u;
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] constexpr T sign(T val) {
    return select(val >= 0, T(1), T(-1));
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] constexpr auto
radians(const T &deg) noexcept {
    return deg * (constants::Pi / 180.0f);
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] constexpr auto
degrees(T rad) noexcept {
    return rad * (constants::InvPi * 180.0f);
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] constexpr auto
rcp(const T &v) {
    return T(1.f) / v;
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] constexpr auto
sqrt(const T &v) {
    if constexpr (is_half_v<T> || is_real_v<T>) {
        return T(std::sqrt(static_cast<float>(v)));
    } else {
        return std::sqrt(v);
    }
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] constexpr auto
rsqrt(const T &v) {
    return rcp(sqrt(v));
}

template<typename T>
requires is_floating_point_v<T>
[[nodiscard]] constexpr auto
fract(const T &v) {
    return v - floor(v);
}

[[nodiscard]] inline float mod(float x, float y) {
    return x - y * floor(x / y);
}

[[nodiscard]] inline half mod(half x, half y) {
    return float2half(x - y * floor(half2float(x / y)));
}

[[nodiscard]] inline real mod(real x, real y) {
    return float2real(real2float(x) - real2float(y) * floor(real2float(x / y)));
}

template<typename T>
//requires is_scalar_v<T>
[[nodiscard]] auto saturate(const T &f) { return min(1.f, max(0.f, f)); }

template<typename T>
requires is_scalar_v<T>
OC_NODISCARD constexpr auto sqr(const T &v) {
    return v * v;
}

[[nodiscard]] inline bool isnan(float x) noexcept {
    auto u = 0u;
    ::memcpy(&u, &x, sizeof(float));
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0u;
}

[[nodiscard]] inline bool isnan(half x) noexcept {
    return x.is_nan();
}

[[nodiscard]] inline bool isnan(real x) noexcept {
    return x.is_nan();
}

[[nodiscard]] inline bool isinf(float x) noexcept {
    auto u = 0u;
    ::memcpy(&u, &x, sizeof(float));
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) == 0u;
}

[[nodiscard]] inline bool isinf(const half &x) noexcept {
    return x.is_inf();
}

[[nodiscard]] inline bool isinf(const real &x) noexcept {
    return x.is_inf();
}

template<typename X, typename A, typename B>
requires is_all_basic_v<X, A, B>
[[nodiscard]] constexpr auto clamp(X x, A a, B b) noexcept {
    return min(max(x, a), b);
}

template<typename X, typename A, typename B>
requires is_all_basic_v<X, A, B>
[[nodiscard]] constexpr auto inverse_lerp(X x, A a, B b) noexcept {
    return (x - a) / (b - a);
}

}// namespace ocarina
