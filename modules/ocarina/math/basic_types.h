//
// Created by Zero on 25/04/2022.
//

#pragma once

#include "basic_traits.h"
#include "scalar_func.h"
#include "math/constants.h"
#include "matrix_types.h"
#include "half.h"
#include "real.h"

namespace ocarina {

using basic_types = ocarina::tuple<
    bool, float, real, int, uint, ulong, half,
    bool2, float2, real2, int2, uint2, ulong2, half2,
    bool3, float3, real3, int3, uint3, ulong3, half3,
    bool4, float4, real4, int4, uint4, ulong4, half4,
    float2x2, float2x3, float2x4,
    float3x2, float3x3, float3x4,
    float4x2, float4x3, float4x4,
    real2x2, real2x3, real2x4,
    real3x2, real3x3, real3x4,
    real4x2, real4x3, real4x4>;

namespace detail {
template<typename T>
struct tuple_to_variant_impl {
    static_assert(always_false_v<T>);
};

template<typename... Ts>
struct tuple_to_variant_impl<ocarina::tuple<Ts...>> {
    using type = ocarina::variant<Ts...>;
};
}// namespace detail

template<typename T>
using tuple_to_variant_t = typename detail::tuple_to_variant_impl<T>::type;

using basic_variant_t = tuple_to_variant_t<basic_types>;

namespace detail {
using texture_elements = ocarina::tuple<uchar, uchar2, uchar4, float, float2, float4>;
template<typename T, typename... Ts>
[[nodiscard]] constexpr bool is_contain(const ocarina::tuple<Ts...> *tp) noexcept {
    return std::disjunction_v<std::is_same<T, Ts>...>;
}

template<typename T>
[[nodiscard]] constexpr bool is_valid_texture_element_impl() noexcept {
    return is_contain<T>(static_cast<texture_elements *>(nullptr));
}

}// namespace detail

template<typename T>
[[nodiscard]] constexpr bool is_valid_texture_element() noexcept {
    return detail::is_valid_texture_element_impl<std::remove_cvref_t<T>>();
}

namespace detail {
template<typename T>
requires(is_valid_texture_element<T>())
struct texture_sample_impl {
    using type = float;
};

template<>
struct texture_sample_impl<float2> {
    using type = float2;
};

template<>
struct texture_sample_impl<uchar2> : public texture_sample_impl<float2> {};

template<>
struct texture_sample_impl<float4> {
    using type = float4;
};

template<>
struct texture_sample_impl<uchar4> : public texture_sample_impl<float4> {};

};// namespace detail

template<typename element_type>
using texture_sample_t = typename detail::texture_sample_impl<std::remove_cvref_t<element_type>>::type;

namespace detail {
template<typename T, size_t N>
struct basic_impl {
    using type = Vector<T, N>;
};

template<typename T>
struct basic_impl<T, 1> {
    using type = T;
};
}// namespace detail

template<typename T, uint N>
using basic = detail::basic_impl<std::remove_cvref_t<T>, N>;
template<typename T, uint N>
using basic_t = typename basic<T, N>::type;

namespace detail {
template<typename T>
struct literal_value {
    static_assert(always_false_v<T>);
};

template<typename... T>
struct literal_value<ocarina::tuple<T...>> {
    using type = ocarina::variant<T...>;
};
}// namespace detail

template<typename T>
using literal_value_t = typename detail::literal_value<T>::type;

using basic_literal_t = literal_value_t<basic_types>;

}// namespace ocarina