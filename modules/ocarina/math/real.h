//
// Created by Z on 2026/4/10.
//

#pragma once

#include "core/stl.h"
#include "basic_traits.h"
#include "half.h"

namespace ocarina {

class real;

namespace detail {
template<typename T>
struct is_real_op_enable : std::bool_constant<std::is_arithmetic_v<std::remove_cvref_t<T>>> {};

template<>
struct is_real_op_enable<real> : std::true_type {};

template<typename T>
requires is_real_op_enable<std::remove_cvref_t<T>>::value
struct binary_op_real_target {
    using raw_t = std::remove_cvref_t<T>;
    using type = std::conditional_t<std::is_floating_point_v<raw_t>, raw_t, real>;
};
}// namespace detail

template<typename T>
static constexpr auto is_real_op_enable_v = detail::is_real_op_enable<std::remove_cvref_t<T>>::value;

template<typename T>
static constexpr auto is_real_mixed_op_enable_v =
    is_real_op_enable_v<T> && !std::is_same_v<std::remove_cvref_t<T>, real>;

template<typename T>
using binary_op_real_target_t = typename detail::binary_op_real_target<std::remove_cvref_t<T>>::type;

class real {
private:
    float value{};

public:
    constexpr real() noexcept = default;

    template<typename T>
    requires is_real_op_enable_v<T>
    constexpr real(T v) noexcept : value(static_cast<float>(v)) {}

    constexpr real(const real &) noexcept = default;
    constexpr real(real &&) noexcept = default;
    constexpr real &operator=(const real &) noexcept = default;
    constexpr real &operator=(real &&) noexcept = default;

#define OC_REAL_CAST_OP(type)                                \
    [[nodiscard]] constexpr operator type() const noexcept { \
        return static_cast<type>(value);                     \
    }
    OC_REAL_CAST_OP(float)
    OC_REAL_CAST_OP(double)
    OC_REAL_CAST_OP(short)
    OC_REAL_CAST_OP(size_t)
    OC_REAL_CAST_OP(int)
    OC_REAL_CAST_OP(half)
    OC_REAL_CAST_OP(bool)
    OC_REAL_CAST_OP(uint)
#undef OC_REAL_CAST_OP

    template<typename T>
    requires is_real_op_enable_v<T>
    constexpr real &operator=(T v) noexcept {
        value = static_cast<float>(v);
        return *this;
    }

#define OC_REAL_BINARY_AND_ASSIGN_OP(op)                                                   \
    constexpr real &operator op##=(const real & rhs) noexcept {                            \
        value op## = rhs.value;                                                            \
        return *this;                                                                      \
    }                                                                                      \
    template<typename T>                                                                   \
    requires is_real_op_enable_v<T>                                                        \
    [[nodiscard]] constexpr binary_op_real_target_t<T> operator op(T rhs) const noexcept { \
        using ret_t = binary_op_real_target_t<T>;                                          \
        return ret_t{value op static_cast<float>(rhs)};                                    \
    }                                                                                      \
    template<typename T>                                                                   \
    requires is_real_mixed_op_enable_v<T>                                                  \
    friend constexpr binary_op_real_target_t<T> operator op(T lhs, real rhs) noexcept {    \
        using ret_t = binary_op_real_target_t<T>;                                          \
        return ret_t{static_cast<float>(lhs) op rhs.value};                                \
    }                                                                                      \
    template<typename T>                                                                   \
    requires is_real_op_enable_v<T>                                                        \
    constexpr real &operator op##=(T rhs) noexcept {                                       \
        value op## = static_cast<float>(rhs);                                              \
        return *this;                                                                      \
    }

    OC_REAL_BINARY_AND_ASSIGN_OP(+)
    OC_REAL_BINARY_AND_ASSIGN_OP(-)
    OC_REAL_BINARY_AND_ASSIGN_OP(*)
    OC_REAL_BINARY_AND_ASSIGN_OP(/)

#undef OC_REAL_BINARY_AND_ASSIGN_OP

#define OC_REAL_COMPARE_OP(op)                                       \
    template<typename T>                                             \
    requires is_real_op_enable_v<T>                                  \
    [[nodiscard]] constexpr bool operator op(T rhs) const noexcept { \
        return value op static_cast<float>(rhs);                     \
    }                                                                \
    template<typename T>                                             \
    requires is_real_mixed_op_enable_v<T>                            \
    friend constexpr bool operator op(T lhs, real rhs) noexcept {    \
        return static_cast<float>(lhs) op rhs.value;                 \
    }

    OC_REAL_COMPARE_OP(==)
    OC_REAL_COMPARE_OP(!=)
    OC_REAL_COMPARE_OP(<)
    OC_REAL_COMPARE_OP(<=)
    OC_REAL_COMPARE_OP(>)
    OC_REAL_COMPARE_OP(>=)

#undef OC_REAL_COMPARE_OP

#define OC_REAL_INC_DEC_OP(op, assign_op, delta) \
    constexpr real &operator op() noexcept {     \
        value assign_op delta;                   \
        return *this;                            \
    }                                            \
    constexpr real operator op(int) noexcept {   \
        real temp = *this;                       \
        value assign_op delta;                   \
        return temp;                             \
    }

    OC_REAL_INC_DEC_OP(++, +=, 1.0f)
    OC_REAL_INC_DEC_OP(--, -=, 1.0f)

#undef OC_REAL_INC_DEC_OP

#define OC_REAL_UNARY_OP(op, expr)                              \
    [[nodiscard]] constexpr real operator op() const noexcept { \
        return expr;                                            \
    }

    OC_REAL_UNARY_OP(+, *this)
    OC_REAL_UNARY_OP(-, real{-value})

#undef OC_REAL_UNARY_OP

    friend std::ostream &operator<<(std::ostream &os, const real &r) {
        os << r.value;
        return os;
    }

    friend std::istream &operator>>(std::istream &is, real &r) {
        is >> r.value;
        return is;
    }

    [[nodiscard]] constexpr uint32_t bits() const noexcept {
        return bit_cast<uint32_t>(value);
    }

    [[nodiscard]] constexpr bool is_nan() const noexcept {
        auto raw = bits();
        return ((raw & 0x7F800000u) == 0x7F800000u) && (raw & 0x007FFFFFu);
    }

    [[nodiscard]] constexpr bool is_inf() const noexcept {
        return (bits() & 0x7FFFFFFFu) == 0x7F800000u;
    }

    [[nodiscard]] constexpr bool is_neg() const noexcept {
        return (bits() & 0x80000000u) != 0u;
    }
};

[[nodiscard]] constexpr float real2float(real r) noexcept {
    return static_cast<float>(r);
}

[[nodiscard]] constexpr real float2real(float v) noexcept {
    return real{v};
}

}// namespace ocarina