//
// Created by Zero on 26/07/2022.
//

#pragma once

#include "constants.h"
#include "scalar_func.h"
#include "dsl/api/operators.h"
#include "dsl/core/type_trait.h"

#define MAKE_VECTOR_OP(op)                                                           \
    template<typename T>                                                             \
    requires ocarina::concepts::iterable<T>                                          \
    [[nodiscard]] T operator op(const T &lhs, const T &rhs) noexcept {               \
        OC_ASSERT((lhs.size() == rhs.size()) || lhs.size() == 1 || rhs.size() == 1); \
        T ret;                                                                       \
        if (lhs.size() == 1) {                                                       \
            for (int i = 0; i < rhs.size(); ++i) {                                   \
                ret.push_back(lhs[0] op rhs[i]);                                     \
            }                                                                        \
        } else if (rhs.size() == 1) {                                                \
            for (int i = 0; i < lhs.size(); ++i) {                                   \
                ret.push_back(lhs[i] op rhs[0]);                                     \
            }                                                                        \
        } else {                                                                     \
            for (int i = 0; i < lhs.size(); ++i) {                                   \
                ret.push_back(lhs[i] op rhs[i]);                                     \
            }                                                                        \
        }                                                                            \
        return ret;                                                                  \
    }

MAKE_VECTOR_OP(+)
MAKE_VECTOR_OP(-)
MAKE_VECTOR_OP(*)
MAKE_VECTOR_OP(/)
#undef MAKE_VECTOR_OP

namespace ocarina {

template<int n, typename T, typename ret_type = condition_t<expr_value_t<T>, T>>
requires requires(T a, T b) { a *b; }
OC_NODISCARD constexpr ret_type Pow(const T &v) {
    if constexpr (n < 0) {
        return 1.f / Pow<-n>(v);
    } else if constexpr (n == 1) {
        return v;
    } else if constexpr (n == 0) {
        return 1;
    }
    auto n2 = Pow<n / 2>(v);
    return n2 * n2 * Pow<n & 1>(v);
}

template<typename F, typename A, typename B>
requires none_dsl_v<F, A, B>
OC_NODISCARD constexpr auto
lerp(F t, A a, B b) noexcept {
    return a + t * (b - a);
}

template<typename T, typename U>
requires is_all_floating_point_expr_v<T, U>
[[nodiscard]] condition_t<bool, T, U> is_close(T t, U u, float epsilon = 0.00001f) {
    return abs(t - u) < epsilon;
}

template<typename T, typename U>
requires ocarina::is_all_scalar_expr_v<T, U>
auto divide(T &&t, U &&u) noexcept {
    return OC_FORWARD(t) * rcp(OC_FORWARD(u));
}

template<typename U, typename V>
[[nodiscard]] constexpr auto abs_dot(const U &u, const V &v) noexcept {
    return abs(dot(u, v));
}

template<typename T>
[[nodiscard]] auto abs(const T &t) noexcept {
    if constexpr (is_half_v<T>) {
        return select(t < 0, -t, t);
    } else {
        return std::abs(t);
    }
}

template<typename T>
void Swap(T &a, T &b) noexcept {
    T tmp = a;
    a = b;
    b = tmp;
}

template<typename T>
[[nodiscard]] constexpr auto safe_sqrt(const T &t) noexcept {
    return sqrt(max(0.f, t));
}

template<typename T>
[[nodiscard]] constexpr auto safe_acos(const T &t) noexcept {
    return acos(clamp(t, -1.f, 1.f));
}

template<typename T>
[[nodiscard]] constexpr T safe_asin(const T &t) noexcept {
    return asin(clamp(t, -1.f, 1.f));
}

template<typename T>
auto safe_div(const T &a, const T &b) noexcept {
    return select(b == 0.0f, 0.0f, a / b);
};

template<typename T, size_t N>
[[nodiscard]] auto triangle_area(Vector<T, N> p0, Vector<T, N> p1, Vector<T, N> p2) noexcept {
    static_assert(N == 3 || N == 2, "N must be greater than 1!");
    if constexpr (N == 2) {
        Vector<T, 3> pp0 = Vector<T, 3>{p0.x, p0.y, 0};
        Vector<T, 3> pp1 = Vector<T, 3>{p1.x, p1.y, 0};
        Vector<T, 3> pp2 = Vector<T, 3>{p2.x, p2.y, 0};
        return 0.5f * length(cross(pp1 - pp0, pp2 - pp0));
    } else {
        return 0.5f * length(cross(p1 - p0, p2 - p0));
    }
}

template<typename T, typename F2>
[[nodiscard]] T triangle_lerp(const F2 &barycentric, const T &v0, const T &v1, const T &v2) noexcept {
    auto u = barycentric.x;
    auto v = barycentric.y;
    auto w = 1 - barycentric.x - barycentric.y;
    return u * v0 + v * v1 + w * v2;
}

template<EPort P = D>
[[nodiscard]] oc_float2<P> barycentric(const oc_float2<P> &p, const oc_float2<P> &v0,
                                       const oc_float2<P> &v1, const oc_float2<P> &v2) {
    oc_float<P> a1 = v0.x - v2.x;
    oc_float<P> b1 = v1.x - v2.x;
    oc_float<P> c1 = p.x - v2.x;

    oc_float<P> a2 = v0.y - v2.y;
    oc_float<P> b2 = v1.y - v2.y;
    oc_float<P> c2 = p.y - v2.y;

    oc_float<P> u = (c1 * b2 - c2 * b1) / (a1 * b2 - a2 * b1);
    oc_float<P> v = (a1 * c2 - a2 * c1) / (a1 * b2 - a2 * b1);
    return make_float2(u, v);
}

template<EPort p = D>
[[nodiscard]] oc_bool<p> in_triangle(const oc_float2<p> &barycentric, const oc_float2<p> &v0,
                                     const oc_float2<p> &v1, const oc_float2<p> &v2) noexcept {
    oc_float3<p> a = make_float3(v0, 0.f);
    oc_float3<p> b = make_float3(v1, 0.f);
    oc_float3<p> c = make_float3(v2, 0.f);
    oc_float3<p> n = make_float3(barycentric, 0.f);

    oc_float3<p> ba = a - b;
    oc_float3<p> cb = b - c;
    oc_float3<p> ac = c - a;

    oc_float3<p> an = n - a;
    oc_float3<p> bn = n - b;
    oc_float3<p> cn = n - c;

    oc_float<p> r0 = cross(ba, bn).z;
    oc_float<p> r1 = cross(cb, cn).z;
    oc_float<p> r2 = cross(ac, an).z;

    oc_bool<p> cond = length_squared(cross(ac, ba)) > 0;

    return ((r0 >= 0 && r1 >= 0 && r2 >= 0) || (r0 <= 0 && r1 <= 0 && r2 <= 0)) && cond;
}

template<typename T>
[[nodiscard]] T srgb_to_linear(T S) {
    using raw_ty = expr_value_t<T>;
    return select((S < raw_ty(0.04045f)),
                  (S / raw_ty(12.92f)),
                  (pow((S + 0.055f) * 1.f / 1.055f, 2.4f)));
}

template<typename T>
[[nodiscard]] T linear_to_srgb(T L) {
    using raw_ty = expr_value_t<T>;
    return select((L < raw_ty(0.0031308f)),
                  (L * raw_ty(12.92f)),
                  (raw_ty(1.055f) * pow(L, raw_ty(1.0f / 2.4f)) - raw_ty(0.055f)));
}

template<typename T>
requires(ocarina::is_general_vector3_v<ocarina::remove_device_t<T>> && !ocarina::is_half_v<type_element_t<ocarina::remove_device_t<T>>>)
[[nodiscard]] auto luminance(const T &v) {
    return dot(make_float3(0.212671f, 0.715160f, 0.072169f), v);
}

template<typename T>
requires(ocarina::is_general_vector3_v<ocarina::remove_device_t<T>> && ocarina::is_half_v<type_element_t<ocarina::remove_device_t<T>>>)
[[nodiscard]] auto luminance(const T &v) {
    return dot(make_half3(0.212671f, 0.715160f, 0.072169f), v);
}


template<typename T>
OC_NODISCARD auto is_zero(const T &v) noexcept {
    return all(v == expr_value_t<T>(0));
}

template<typename T>
OC_NODISCARD auto nonzero(const T &v) noexcept {
    return any(v != expr_value_t<T>(0));
}

template<typename T>
requires is_vector_expr_v<T>
OC_NODISCARD auto has_nan(const T &v) noexcept {
    return any(isnan(v));
}

template<typename T>
requires is_vector_expr_v<T>
OC_NODISCARD auto has_inf(const T &v) noexcept {
    return any(isinf(v));
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] constexpr bool isnan(T val) noexcept {
    if constexpr (is_half_v<T> || is_real_v<T>) {
        return val.is_nan();
    } else {
        return std::isnan(val);
    }
}

template<typename T>
requires is_scalar_v<T>
[[nodiscard]] constexpr bool isinf(T val) noexcept {
    if constexpr (is_half_v<T> || is_real_v<T>) {
        return val.is_inf();
    } else {
        return std::isinf(val);
    }
}

template<typename T>
[[nodiscard]] constexpr auto invalid(const T &t) noexcept {
    return isinf(t) || isnan(t);
}

template<typename T>
[[nodiscard]] constexpr auto has_invalid(const T &t) noexcept {
    return has_nan(t) || has_inf(t);
}

template<typename T>
OC_NODISCARD auto max_comp(const T &v) noexcept {
    static constexpr uint dim = vector_expr_dimension_v<T>;
    auto ret = v[0];
    for (int i = 1; i < dim; ++i) {
        ret = max(v[i], ret);
    }
    return ret;
}

[[nodiscard]] inline uint32_t make_8bit(const float f) {
    return fmin(255, fmax(0, int(f * 256.f)));
}

template<typename V>
requires ocarina::is_vector3_v<expr_value_t<V>>
[[nodiscard]] inline uint32_t make_rgba(const V &color) {
    return (make_8bit(color.x) << 0) +
           (make_8bit(color.y) << 8) +
           (make_8bit(color.z) << 16) +
           (0xffU << 24);
}

template<typename V>
requires ocarina::is_vector4_v<expr_value_t<V>>
[[nodiscard]] inline uint32_t make_rgba(const V &color) {
    return (make_8bit(color.x) << 0) +
           (make_8bit(color.y) << 8) +
           (make_8bit(color.z) << 16) +
           (make_8bit(color.w) << 24);
}
#include "common_lib.inl.h"
}// namespace ocarina