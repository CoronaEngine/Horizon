//
// Created by YGXXD on 2026/10/26.
//

#pragma once

#include <type_traits>

#include <ktm/ktm.h>

#include "basic_traits.h"

#ifndef OCARINA_MATH_USE_KTM
#define OCARINA_MATH_USE_KTM 1
#endif

namespace ocarina::bridge {

#if OCARINA_MATH_USE_KTM

template<typename T>
inline constexpr bool use_ktm_arith_v =
    std::is_same_v<T, float> ||
    std::is_same_v<T, int> ||
    std::is_same_v<T, uint>;

template<typename T>
inline constexpr bool use_ktm_float_v = std::is_same_v<T, float>;

#else

template<typename T>
inline constexpr bool use_ktm_arith_v = false;

template<typename T>
inline constexpr bool use_ktm_float_v = false;

#endif

template<typename T, size_t N>
using ktm_vec_t = ktm::vec<N, T>;

template<typename T, size_t N, size_t M>
using ktm_mat_t = ktm::mat<M, N, T>;

template<typename T, size_t N>
[[nodiscard]] inline ktm_vec_t<T, N> as_ktm(const Vector<T, N> &v) noexcept {
    static_assert(sizeof(Vector<T, N>) == sizeof(ktm_vec_t<T, N>),
                  "ocarina vector and ktm vector layouts must match");
    static_assert(alignof(Vector<T, N>) == alignof(ktm_vec_t<T, N>),
                  "ocarina vector and ktm vector alignments must match");
    static_assert(std::is_standard_layout_v<Vector<T, N>>,
                  "ocarina vector must be standard layout");
    static_assert(std::is_trivially_copyable_v<Vector<T, N>>,
                  "ocarina vector must be trivially copyable");
    return bit_cast<ktm_vec_t<T, N>>(v);
}

template<typename T, size_t N>
[[nodiscard]] inline Vector<T, N> from_ktm(const ktm_vec_t<T, N> &v) noexcept {
    static_assert(sizeof(Vector<T, N>) == sizeof(ktm_vec_t<T, N>),
                  "ocarina vector and ktm vector layouts must match");
    return bit_cast<Vector<T, N>>(v);
}

template<typename T, size_t N, size_t M>
[[nodiscard]] inline ktm_mat_t<T, N, M> as_ktm(const Matrix<T, N, M> &mat) noexcept {
    static_assert(sizeof(Matrix<T, N, M>) == sizeof(ktm_mat_t<T, N, M>),
                  "ocarina matrix and ktm matrix layouts must match");
    static_assert(alignof(Matrix<T, N, M>) == alignof(ktm_mat_t<T, N, M>),
                  "ocarina matrix and ktm matrix alignments must match");
    static_assert(std::is_standard_layout_v<Matrix<T, N, M>>,
                  "ocarina matrix must be standard layout");
    static_assert(std::is_trivially_copyable_v<Matrix<T, N, M>>,
                  "ocarina matrix must be trivially copyable");
    return bit_cast<ktm_mat_t<T, N, M>>(mat);
}

template<typename T, size_t N, size_t M>
[[nodiscard]] inline Matrix<T, N, M> from_ktm(const ktm_mat_t<T, N, M> &mat) noexcept {
    static_assert(sizeof(Matrix<T, N, M>) == sizeof(ktm_mat_t<T, N, M>),
                  "ocarina matrix and ktm matrix layouts must match");
    return bit_cast<Matrix<T, N, M>>(mat);
}

namespace ktm_fn {

using ktm::abs;
using ktm::acos;
using ktm::acosh;
using ktm::asin;
using ktm::asinh;
using ktm::atan;
using ktm::atan2;
using ktm::atanh;
using ktm::ceil;
using ktm::clamp;
using ktm::cos;
using ktm::cosh;
using ktm::degrees;
using ktm::exp;
using ktm::exp2;
using ktm::floor;
using ktm::fract;
using ktm::log;
using ktm::log10;
using ktm::log2;
using ktm::max;
using ktm::min;
using ktm::pow;
using ktm::radians;
using ktm::round;
using ktm::sin;
using ktm::sinh;
using ktm::sqrt;
using ktm::tan;
using ktm::tanh;

template<typename V>
[[nodiscard]] inline auto rcp(const V &v) noexcept {
    return ktm::recip(v);
}

template<typename V>
[[nodiscard]] inline V lerp(const V &t, const V &a, const V &b) noexcept {
    return ktm::mix(a, b, t);
}

template<typename V>
auto sqr(const V &) = delete;

template<typename V>
auto sign(const V &) = delete;

template<typename V>
auto isnan(const V &) = delete;

template<typename V>
auto isinf(const V &) = delete;

template<typename V>
auto copysign(const V &) = delete;

template<typename V>
[[nodiscard]] inline V fma(const V &t, const V &u, const V &v) noexcept {
    return ktm_op_madd(v, t, u);
}

template<typename V>
auto inverse_lerp(const V &) = delete;

}// namespace ktm_fn

}// namespace ocarina::detail
