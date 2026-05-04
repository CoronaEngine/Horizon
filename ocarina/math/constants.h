//
// Created by Zero on 03/07/2022.
//

#pragma once

#include "core/header.h"
#include "basic_traits.h"

namespace ocarina {
inline namespace constants {
/// pi
constexpr float Pi = 3.14159265358979323846264338327950288f;
/// 2 * pi
constexpr float _2Pi = 2 * Pi;
/// pi/2
constexpr float PiOver2 = 1.57079632679489661923132169163975144f;
/// pi/4
constexpr float PiOver4 = 0.785398163397448309615660845819875721f;
/// 1/pi
constexpr float InvPi = 1.f / Pi;
/// 2/pi
constexpr float _2OverPi = 2.f / Pi;
/// 1 / (4 * pi)
constexpr float Inv4Pi = 1 / (4 * Pi);
/// 1 / (2 * pi)
constexpr float Inv2Pi = 1 / (2 * Pi);
/// sqrt(2)
constexpr float Sqrt2 = 1.41421356237309504880168872420969808f;
/// 1/sqrt(2)
constexpr float InvSqrt2 = 0.707106781186547524400844362104849039f;
/// 1-epsilon
constexpr float OneMinusEpsilon = 0x1.fffffep-1f;
/// tmax for ray
constexpr float RayTMax = 1e16f;

constexpr float ShadowEpsilon = 0.0001f;

constexpr uint32_t InvalidUI32 = uint32_t(-1);

constexpr uint64_t InvalidUI64 = uint64_t(-1);

constexpr uchar InvalidUI8 = uchar(-1);
}// namespace constants

inline namespace constants {

constexpr float shadow_epsilon = 0.0001f;

struct NegInfTy {
    constexpr operator double() const { return -std::numeric_limits<double>::infinity(); }
    constexpr operator float() const { return -std::numeric_limits<float>::infinity(); }
    constexpr operator long long() const { return std::numeric_limits<long long>::min(); }
    constexpr operator unsigned long long() const { return std::numeric_limits<unsigned long long>::min(); }
    constexpr operator long() const { return std::numeric_limits<long>::min(); }
    constexpr operator unsigned long() const { return std::numeric_limits<unsigned long>::min(); }
    constexpr operator int() const { return std::numeric_limits<int>::min(); }
    constexpr operator unsigned int() const { return std::numeric_limits<unsigned int>::min(); }
    constexpr operator short() const { return std::numeric_limits<short>::min(); }
    constexpr operator unsigned short() const { return std::numeric_limits<unsigned short>::min(); }
    constexpr operator char() const { return std::numeric_limits<char>::min(); }
    constexpr operator unsigned char() const { return std::numeric_limits<unsigned char>::min(); }
};

template<typename T>
struct neg_infinity {
    static constexpr auto value = std::remove_cvref_t<T>(NegInfTy{});
};
OC_DEFINE_TEMPLATE_VALUE(neg_infinity)

struct PosInfTy {
    constexpr operator double() const { return std::numeric_limits<double>::infinity(); }
    constexpr operator float() const { return std::numeric_limits<float>::infinity(); }
    constexpr operator long long() const { return std::numeric_limits<long long>::max(); }
    constexpr operator unsigned long long() const { return std::numeric_limits<unsigned long long>::max(); }
    constexpr operator long() const { return std::numeric_limits<long>::max(); }
    constexpr operator unsigned long() const { return std::numeric_limits<unsigned long>::max(); }
    constexpr operator int() const { return std::numeric_limits<int>::max(); }
    constexpr operator unsigned int() const { return std::numeric_limits<unsigned int>::max(); }
    constexpr operator short() const { return std::numeric_limits<short>::max(); }
    constexpr operator unsigned short() const { return std::numeric_limits<unsigned short>::max(); }
    constexpr operator char() const { return std::numeric_limits<char>::max(); }
    constexpr operator unsigned char() const { return std::numeric_limits<unsigned char>::max(); }
};

template<typename T>
struct pos_infinity {
    static constexpr auto value = std::remove_cvref_t<T>(PosInfTy{});
};
OC_DEFINE_TEMPLATE_VALUE(pos_infinity)

struct NaNTy {
    constexpr operator double() const { return std::numeric_limits<double>::quiet_NaN(); }
    constexpr operator float() const { return std::numeric_limits<float>::quiet_NaN(); }
};

struct UlpTy {
    constexpr operator double() const { return std::numeric_limits<double>::epsilon(); }
    constexpr operator float() const { return std::numeric_limits<float>::epsilon(); }
};

}// namespace constants

}// namespace ocarina