//
// Created by z on 2026/1/20.
//

#pragma once

#include "core/stl.h"
#include "basic_traits.h"

namespace ocarina {

class half;

namespace detail {
template<typename T>
struct is_half_op_enable : std::false_type {};

template<>
struct is_half_op_enable<float> : std::true_type {};

template<>
struct is_half_op_enable<int> : std::true_type {};

template<>
struct is_half_op_enable<uint> : std::true_type {};

template<>
struct is_half_op_enable<double> : std::true_type {};
}// namespace detail

template<typename T>
static constexpr auto is_half_op_enable_v = detail::is_half_op_enable<std::remove_cvref_t<T>>::value;

namespace detail {

template<typename T>
requires is_half_op_enable_v<T>
struct binary_op_half_target {
    using type = std::conditional_t<is_integral_v<T>, half, T>;
};

}// namespace detail

template<typename T>
using binary_op_half_target_t = typename detail::binary_op_half_target<std::remove_cvref_t<T>>::type;

static constexpr uint16_t float_to_half(float f) {
    uint32_t bits_ = bit_cast<uint32_t>(f);
    uint16_t sign = (bits_ >> 31) & 0x1;
    uint16_t exp = (bits_ >> 23) & 0xFF;
    uint32_t mantissa = bits_ & 0x7FFFFF;

    if (exp == 0xFF) {
        // inf or nan
        return (sign << 15) | 0x7C00 | (mantissa ? 0x200 : 0);
    }

    if (exp == 0) {
        if (mantissa == 0) {
            return sign << 15;
        }
        int shift = 0;
        while ((mantissa & 0x400000) == 0) {
            mantissa <<= 1;
            shift++;
        }
        mantissa &= 0x7FFFFF;
        exp = 1 - shift;
    }

    exp = exp - 127 + 15;

    if (exp >= 31) {
        return (sign << 15) | 0x7C00;
    } else if (exp <= 0) {
        uint32_t shifted = (mantissa | 0x800000) >> (1 - exp);
        return (sign << 15) | (shifted >> 13);
    }

    uint16_t half_exp = exp & 0x1F;
    uint16_t half_mantissa = (mantissa >> 13) & 0x3FF;
    return (sign << 15) | (half_exp << 10) | half_mantissa;
}
static constexpr float half_to_float(uint16_t h) {
    uint16_t sign = (h >> 15) & 0x1;
    uint16_t exp = (h >> 10) & 0x1F;
    uint16_t mantissa = h & 0x3FF;

    if (exp == 0x1F) {
        // inf or nan
        uint32_t result = (sign << 31) | 0x7F800000 | (mantissa ? 0x00400000 : 0);
        return *reinterpret_cast<float *>(&result);
    }

    if (exp == 0) {
        if (mantissa == 0) {
            uint32_t result = sign << 31;
            return *reinterpret_cast<float *>(&result);
        }
        uint32_t result = (sign << 31) | 0x00800000;
        while ((mantissa & 0x400) == 0) {
            mantissa <<= 1;
            result -= 0x00800000;
        }
        result |= (mantissa & 0x3FF) << 13;
        return *reinterpret_cast<float *>(&result);
    }

    uint32_t result = (sign << 31) | ((exp - 15 + 127) << 23) | (mantissa << 13);
    return *reinterpret_cast<float *>(&result);
}

template<typename T>
requires is_half_op_enable_v<T>
static constexpr float cast_to_float(T val) {
    if constexpr (is_half_v<T>) {
        return half_to_float(val.bits());
    } else {
        return static_cast<float>(val);
    }
}

class OC_MATH_API half {
private:
    uint16_t bits_;

public:
    constexpr half() : bits_(0) {}
    template<typename T>
    requires is_half_op_enable_v<T>
    constexpr half(T &&val) : bits_(float_to_half(static_cast<float>(std::forward<T>(val)))) {}

#define OC_HALF_CAST_OP(type)                           \
    constexpr operator type() const {                   \
        return static_cast<type>(half_to_float(bits_)); \
    }
    OC_HALF_CAST_OP(float)
    OC_HALF_CAST_OP(int)
    OC_HALF_CAST_OP(uint)
    OC_HALF_CAST_OP(size_t)
    OC_HALF_CAST_OP(double)
    OC_HALF_CAST_OP(bool)
#undef OC_HALF_CAST_OP
    operator real() const;
    constexpr half &operator=(const half &other) {
        if (this != &other) {
            bits_ = other.bits_;
        }
        return *this;
    }

    template<typename T>
    requires is_half_op_enable_v<T>
    constexpr half &operator=(T val) {
        *this = half(val);
        return *this;
    }

#define OC_HALF_BINARY_AND_ASSIGN_OP(op)                                       \
    template<typename T>                                                       \
    requires is_half_op_enable_v<T>                                            \
    constexpr binary_op_half_target_t<T> operator op(T val) const {            \
        return half_to_float(bits_) op val;                                    \
    }                                                                          \
    template<typename T>                                                       \
    requires is_half_op_enable_v<T>                                            \
    friend constexpr binary_op_half_target_t<T> operator op(T lhs, half rhs) { \
        return lhs op half_to_float(rhs.bits_);                                \
    }                                                                          \
    constexpr half operator op(const half &other) const {                      \
        return half(half_to_float(bits_) op half_to_float(other.bits_));       \
    }                                                                          \
    template<typename T>                                                       \
    constexpr half &operator op## = (const T &other) {                         \
        *this = *this op other;                                                \
        return *this;                                                          \
    }

    OC_HALF_BINARY_AND_ASSIGN_OP(+)
    OC_HALF_BINARY_AND_ASSIGN_OP(-)
    OC_HALF_BINARY_AND_ASSIGN_OP(*)
    OC_HALF_BINARY_AND_ASSIGN_OP(/)

#undef OC_HALF_BINARY_AND_ASSIGN_OP

#define OC_HALF_COMPARE_OP(op, bool_nan)                  \
    constexpr bool operator op(const half &other) const { \
        if (is_nan() || other.is_nan()) {                 \
            return bool_nan;                              \
        }                                                 \
        return bits_ op other.bits_;                      \
    }                                                     \
                                                          \
    template<typename T>                                  \
    requires is_half_op_enable_v<T>                       \
    constexpr bool operator op(T val) const {             \
        if constexpr (is_integral_v<T>) {                 \
            return *this op half(val);                    \
        } else {                                          \
            return T(*this) op val;                       \
        }                                                 \
    }                                                     \
                                                          \
    template<typename T>                                  \
    requires is_half_op_enable_v<T>                       \
    friend constexpr bool operator op(T lhs, half rhs) {  \
        if constexpr (is_integral_v<T>) {                 \
            return half(lhs) op rhs;                      \
        } else {                                          \
            return lhs op T(rhs);                         \
        }                                                 \
    }

    OC_HALF_COMPARE_OP(==, false)
    OC_HALF_COMPARE_OP(!=, true)
    OC_HALF_COMPARE_OP(>, false)
    OC_HALF_COMPARE_OP(<, false)
    OC_HALF_COMPARE_OP(>=, false)
    OC_HALF_COMPARE_OP(<=, false)

#undef OC_HALF_COMPARE_OP

    constexpr half &operator++() {
        *this = *this + half(1.0f);
        return *this;
    }
    constexpr half operator++(int) {
        half temp = *this;
        ++(*this);
        return temp;
    }
    constexpr half &operator--() {
        *this = *this - half(1.0f);
        return *this;
    }
    constexpr half operator--(int) {
        half temp = *this;
        --(*this);
        return temp;
    }

    constexpr half operator+() const {
        return *this;
    }
    constexpr half operator-() const {
        half result;
        result.bits_ = bits_ ^ 0x8000;
        return result;
    }

    friend std::ostream &operator<<(std::ostream &os, const half &h) {
        os << static_cast<float>(h);
        return os;
    }
    friend std::istream &operator>>(std::istream &is, half &h) {
        float f;
        is >> f;
        h = f;
        return is;
    }

    [[nodiscard]] constexpr uint16_t bits() const { return bits_; }

    [[nodiscard]] constexpr bool is_nan() const {
        return ((bits_ & 0x7C00) == 0x7C00) && (bits_ & 0x03FF);
    }
    [[nodiscard]] constexpr bool is_inf() const {
        return ((bits_ & 0x7FFF) == 0x7C00);
    }
    [[nodiscard]] constexpr bool is_neg() const {
        return (bits_ & 0x8000) != 0;
    }
};

static constexpr float half2float(half h) {
    return half_to_float(h.bits());
}

static constexpr half float2half(float f) {
    return half(f);
}

}// namespace ocarina