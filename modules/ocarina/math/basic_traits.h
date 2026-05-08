//
// Created by Zero on 25/04/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/util/element_trait.h"

namespace ocarina {

template<typename T>
requires std::is_enum_v<T>
[[nodiscard]] constexpr auto
to_underlying(T e) noexcept {
    return static_cast<std::underlying_type_t<T>>(e);
}
class half;
class real;

template<typename T, size_t N>
class Vector;

template<typename T, size_t N, size_t M>
class Matrix;

using uint = uint32_t;
using ulong = uint64_t;
using uchar = unsigned char;
using ushort = unsigned short;

template<typename T>
using is_integral = std::disjunction<
    std::is_same<std::remove_cvref_t<T>, int>,
    std::is_same<std::remove_cvref_t<T>, uint>,
    std::is_same<std::remove_cvref_t<T>, size_t>>;

template<typename T>
constexpr bool is_integral_v = is_integral<T>::value;

template<typename T>
using is_boolean = std::is_same<std::remove_cvref_t<T>, bool>;

template<typename T>
constexpr auto is_boolean_v = is_boolean<T>::value;

template<typename T>
using is_half = std::is_same<std::remove_cvref_t<T>, half>;
OC_DEFINE_TEMPLATE_VALUE(is_half)

template<typename T>
using is_float = std::is_same<std::remove_cvref_t<T>, float>;
OC_DEFINE_TEMPLATE_VALUE(is_float)

template<typename T>
using is_real = std::is_same<std::remove_cvref_t<T>, real>;
OC_DEFINE_TEMPLATE_VALUE(is_real)

template<typename T>
using is_floating_point = std::disjunction<is_half<T>, is_float<T>, is_real<T>>;

template<typename T>
constexpr auto is_floating_point_v = is_floating_point<T>::value;

template<typename T>
using is_char = std::is_same<std::remove_cvref_t<T>, char>;

template<typename T>
using is_uchar = std::is_same<std::remove_cvref_t<T>, uchar>;

template<typename T>
using is_signed = std::disjunction<
    is_floating_point<T>,
    std::is_same<std::remove_cvref_t<T>, int>>;

template<typename T>
constexpr auto is_signed_v = is_signed<T>::value;

template<typename T>
using is_unsigned = std::disjunction<std::is_same<std::remove_cvref_t<T>, uint>,
                                     std::is_same<std::remove_cvref_t<T>, ulong>>;

template<typename T>
constexpr auto is_unsigned_v = is_unsigned<T>::value;

template<typename T>
using is_scalar = std::disjunction<is_integral<T>,
                                   is_boolean<T>,
                                   ocarina::is_floating_point<T>>;

template<typename T>
constexpr auto is_scalar_v = is_scalar<T>::value;

template<typename T>
using is_number = std::disjunction<is_integral<T>,
                                   ocarina::is_floating_point<T>>;

template<typename T>
constexpr auto is_number_v = is_number<T>::value;

template<typename T, size_t N, size_t... Indices>
struct Swizzle;

template<typename T>
struct is_dynamic_size {
    static constexpr bool value = false;
};
template<typename T>
static constexpr auto is_dynamic_size_v = is_dynamic_size<std::remove_cvref_t<T>>::value;

template<>
struct is_dynamic_size<real> {
    static constexpr bool value = true;
};

template<template<typename, size_t> typename Container, size_t N>
struct is_dynamic_size<Container<real, N>> {
    static constexpr bool value = true;
};

template<size_t N, size_t M>
struct is_dynamic_size<Matrix<real, N, M>> {
    static constexpr bool value = true;
};

namespace detail {

template<typename T>
struct swizzle_dimension_impl {
    static constexpr size_t value = 1;
};

template<typename T, size_t N, size_t... Indices>
struct swizzle_dimension_impl<Swizzle<T, N, Indices...>> {
    static constexpr size_t value = sizeof...(Indices);
};
}// namespace detail

template<typename T>
using swizzle_dimension = detail::swizzle_dimension_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(swizzle_dimension)

namespace detail {
//todo bug of msvc
template<typename T>
struct is_swizzle_impl : std::false_type {
};

template<typename T, size_t N, size_t... Indices>
struct is_swizzle_impl<Swizzle<T, N, Indices...>> : std::true_type {
};

/// fuck !!! work around bug of msvc
template<typename T, size_t N = 0u>
struct is_swizzle_dim_impl {
    static constexpr bool value = is_swizzle_impl<T>::value &&
                                  (swizzle_dimension_v<T> == N || N == 0);
};

}// namespace detail

template<typename T, size_t N = 0>
using is_swizzle = detail::is_swizzle_dim_impl<std::remove_cvref_t<T>, N>;
template<typename T, size_t N = 0>
constexpr auto is_swizzle_v = is_swizzle<T, N>::value;

template<typename T>
struct Var;

namespace detail {

template<typename T>
struct is_host_swizzle_impl : std::false_type {};

template<typename T, size_t N, size_t... Indices>
struct is_host_swizzle_impl<Swizzle<T, N, Indices...>> : std::true_type {};

template<typename T, size_t N, size_t... Indices>
struct is_host_swizzle_impl<Swizzle<Var<T>, N, Indices...>> : std::false_type {};

/// fuck !!! work around bug of msvc
template<typename T, size_t N = 0u>
struct is_host_swizzle_dim_impl {
    static constexpr bool value = is_host_swizzle_impl<T>::value &&
                                  (swizzle_dimension_v<T> == N || N == 0);
};

}// namespace detail

template<typename T, size_t N = 0u>
using is_host_swizzle = detail::is_host_swizzle_dim_impl<std::remove_cvref_t<T>, N>;
template<typename T, size_t N = 0u>
constexpr auto is_host_swizzle_v = is_host_swizzle<T, N>::value;

#define MAKE_TYPE_TRAITS(type)                                  \
    template<typename... Ts>                                    \
    using is_all_##type = std::conjunction<is_##type<Ts>...>;   \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_##type)               \
    template<typename... Ts>                                    \
    using is_any_##type = std::disjunction<is_##type<Ts>...>;   \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_any_##type)               \
    template<typename... Ts>                                    \
    using is_none_##type = std::negation<is_any_##type<Ts...>>; \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_none_##type)

MAKE_TYPE_TRAITS(scalar)
MAKE_TYPE_TRAITS(number)
MAKE_TYPE_TRAITS(integral)
MAKE_TYPE_TRAITS(half)
MAKE_TYPE_TRAITS(float)
MAKE_TYPE_TRAITS(real)
MAKE_TYPE_TRAITS(swizzle)
MAKE_TYPE_TRAITS(floating_point)
MAKE_TYPE_TRAITS(boolean)
MAKE_TYPE_TRAITS(unsigned)

#undef MAKE_ALL_TYPE_TRAITS

namespace {
template<typename... T>
struct all_same_impl : std::true_type {};

template<typename First, typename... Other>
struct all_same_impl<First, Other...> : std::conjunction<std::is_same<First, Other>...> {};
}// namespace

template<typename... T>
using is_same = all_same_impl<T...>;

template<typename... T>
constexpr auto is_same_v = is_same<T...>::value;

namespace {

template<typename T, typename... Ts>
struct all_is_impl;

template<typename T, typename First>
struct all_is_impl<T, First> : std::is_same<T, First> {};

template<typename T, typename First, typename... Rest>
struct all_is_impl<T, First, Rest...> : std::conjunction<all_is_impl<T, First>,
                                                         all_is_impl<T, Rest>...> {};

}// namespace

template<typename Target, typename... Ts>
using all_is = all_is_impl<std::remove_cvref_t<Target>, std::remove_cvref_t<Ts>...>;

template<typename Target, typename... Ts>
constexpr auto all_is_v = all_is<Target, Ts...>::value;

namespace detail {
template<typename T>
struct remove_device_impl;
}

template<typename T>
using remove_device = detail::remove_device_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_TYPE(remove_device)

namespace detail {
template<typename T>
struct is_device_swizzle_impl : std::false_type {};

template<typename T, size_t N, size_t... Indices>
struct is_device_swizzle_impl<Swizzle<T, N, Indices...>> : std::false_type {};

template<typename T, size_t N, size_t... Indices>
struct is_device_swizzle_impl<Swizzle<Var<T>, N, Indices...>> : std::true_type {};

/// fuck !!! work around bug of msvc
template<typename T, size_t N = 0u>
struct is_device_swizzle_dim_impl {
    static constexpr bool value = is_device_swizzle_impl<T>::value &&
                                  (swizzle_dimension_v<T> == N || N == 0);
};

}// namespace detail

template<typename T, size_t N = 0u>
using is_device_swizzle = detail::is_device_swizzle_dim_impl<std::remove_cvref_t<T>, N>;
template<typename T, size_t N = 0u>
constexpr auto is_device_swizzle_v = is_device_swizzle<T, N>::value;

namespace detail {
template<typename T>
struct swizzle_decay_impl {
    using type = T;
};

template<typename T, size_t N, size_t... Indices>
struct swizzle_decay_impl<Swizzle<T, N, Indices...>> {
    using type = typename Swizzle<T, N, Indices...>::vec_type;
};

template<typename T, size_t N, size_t... Indices>
struct swizzle_decay_impl<Swizzle<Var<T>, N, Indices...>> {
    using type = typename Swizzle<Var<T>, N, Indices...>::vec_type;
};

}// namespace detail

template<typename T>
using swizzle_decay = detail::swizzle_decay_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_TYPE(swizzle_decay)

template<typename T>
[[nodiscard]] auto decay_swizzle(const T &t) noexcept {
    return static_cast<swizzle_decay_t<T>>(t);
}

template<typename T, size_t N>
class Vector;

namespace detail {

template<typename T>
struct deduce_vec {
    using type = T;
};

template<typename T, size_t N>
struct deduce_vec<Vector<T, N>> {
    using type = typename Vector<T, N>::vec_type;
};

template<typename T, size_t N, size_t... Indices>
struct deduce_vec<Swizzle<T, N, Indices...>> {
    using type = typename Swizzle<T, N, Indices...>::vec_type;
};

template<typename Lhs, typename Rhs>
struct deduce_binary_op_vec_impl {
    static_assert(always_false_v<Lhs, Rhs>);
};

template<typename T, size_t N>
struct deduce_binary_op_vec_impl<Vector<T, N>, Vector<T, N>> {
    using type = Vector<T, N>;
};

template<typename T, size_t N>
struct deduce_binary_op_vec_impl<typename Vector<T, N>::scalar_type, Vector<T, N>> {
    using type = Vector<T, N>;
};

template<typename T, size_t N>
struct deduce_binary_op_vec_impl<Vector<T, N>, typename Vector<T, N>::scalar_type> {
    using type = Vector<T, N>;
};

template<typename Lhs, typename Rhs>
struct deduce_binary_op_vec {
    using type = typename deduce_binary_op_vec_impl<typename deduce_vec<Lhs>::type,
                                                    typename deduce_vec<Rhs>::type>::type;
};

}// namespace detail

template<typename T>
using deduce_vec_t = typename detail::deduce_vec<std::remove_cvref_t<T>>::type;

template<typename Lhs, typename Rhs>
using deduce_binary_op_vec_t = typename detail::deduce_binary_op_vec<std::remove_cvref_t<Lhs>,
                                                                     std::remove_cvref_t<Rhs>>::type;

namespace detail {

template<typename T>
struct vector_dimension_impl {
    static constexpr auto value = static_cast<size_t>(1u);
};

template<typename T, size_t N>
struct vector_dimension_impl<Vector<T, N>> {
    static constexpr auto value = N;
};

template<typename U, typename V>
struct is_vector_same_dimension_impl : std::false_type {};

template<typename U, typename V, size_t N>
struct is_vector_same_dimension_impl<Vector<U, N>, Vector<V, N>> : std::true_type {};

template<typename... T>
struct is_vector_all_same_dimension_impl : std::true_type {};

template<typename First, typename... Other>
struct is_vector_all_same_dimension_impl<First, Other...> : std::conjunction<is_vector_same_dimension_impl<First, Other>...> {};

}// namespace detail

template<typename... T>
using is_vector_same_dimension = detail::is_vector_all_same_dimension_impl<std::remove_cvref_t<T>...>;

template<typename... T>
constexpr auto is_vector_same_dimension_v = is_vector_same_dimension<T...>::value;

template<typename T>
using vector_dimension = detail::vector_dimension_impl<std::remove_cvref_t<T>>;

template<typename T>
constexpr auto vector_dimension_v = vector_dimension<T>::value;

template<typename T, size_t N, size_t M>
class Matrix;

namespace detail {
template<typename T>
struct matrix_dimension_impl {
    static constexpr auto value = static_cast<size_t>(1u);
};

template<typename T, size_t N, size_t M>
struct matrix_dimension_impl<Matrix<T, N, M>> {
    static constexpr auto value = N;
};
}// namespace detail

template<typename T>
using matrix_dimension = detail::matrix_dimension_impl<std::remove_cvref_t<T>>;

template<typename T>
constexpr auto matrix_dimension_v = matrix_dimension<T>::value;

namespace detail {
template<typename T>
struct type_dimension_impl {
    static constexpr size_t value = 1;
};

template<typename T, size_t N>
struct type_dimension_impl<Vector<T, N>> {
    static constexpr size_t value = N;
};

template<typename T, size_t N, size_t... Indices>
struct type_dimension_impl<Swizzle<T, N, Indices...>> {
    static constexpr size_t value = swizzle_dimension_v<Swizzle<T, N, Indices...>>;
};

}// namespace detail

template<typename T>
using type_dimension = detail::type_dimension_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(type_dimension)

namespace detail {
template<typename... Ts>
struct is_same_type_dimension_impl : std::false_type {};

template<typename A, typename B>
requires(type_dimension_v<A> == type_dimension_v<B>)
struct is_same_type_dimension_impl<A, B> : std::true_type {};

template<typename T, typename... Ts>
requires(sizeof...(Ts) > 1)
struct is_same_type_dimension_impl<T, Ts...> : std::conjunction<is_same_type_dimension_impl<T, Ts>...> {};

}// namespace detail

namespace detail {
template<typename T>
struct vector_element_impl {
    using type = T;
};

template<typename T, size_t N>
struct vector_element_impl<Vector<T, N>> {
    using type = T;
};

}// namespace detail

template<typename T>
using vector_element = detail::vector_element_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_TYPE(vector_element)

namespace detail {
template<typename T>
struct type_element_impl {
    using type = T;
};

template<typename T, size_t N>
struct type_element_impl<Vector<T, N>> {
    using type = T;
};

template<typename T, size_t N, size_t... Indices>
struct type_element_impl<Swizzle<T, N, Indices...>> {
    using type = T;
};

}// namespace detail

template<typename T>
using type_element = detail::type_element_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_TYPE(type_element)

template<typename... Ts>
using is_same_type_dimension = detail::is_same_type_dimension_impl<std::remove_cvref_t<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_same_type_dimension)

namespace detail {
template<typename... Ts>
struct is_same_type_element_impl : ocarina::is_same<type_element_t<Ts>...> {};

}// namespace detail

template<typename... Ts>
using is_same_type_element = detail::is_same_type_element_impl<std::remove_cvref_t<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_same_type_element)

namespace detail {

template<typename T, size_t N = 0u>
struct is_vector_impl : std::false_type {};

template<typename T, size_t N>
struct is_vector_impl<Vector<T, N>, N> : std::true_type {};

template<typename T, size_t N>
struct is_vector_impl<Vector<T, N>, 0u> : std::true_type {};

}// namespace detail

//todo clear up the trait
template<typename T, size_t N = 0u>
using is_vector = detail::is_vector_impl<std::remove_cvref_t<T>, N>;

template<typename T>
using is_vector2 = is_vector<T, 2u>;

template<typename T>
using is_vector3 = is_vector<T, 3u>;

template<typename T>
using is_vector4 = is_vector<T, 4u>;

template<typename T, size_t N = 0u>
constexpr auto is_vector_v = is_vector<T, N>::value;

template<typename T>
constexpr auto is_vector2_v = is_vector2<T>::value;

template<typename T>
constexpr auto is_vector3_v = is_vector3<T>::value;

template<typename T>
constexpr auto is_vector4_v = is_vector4<T>::value;

template<typename T, size_t N = 0u>
using is_general_vector = std::disjunction<is_vector<swizzle_decay_t<T>, N>>;

template<typename T>
using is_general_vector2 = is_general_vector<T, 2u>;

template<typename T>
using is_general_vector3 = is_general_vector<T, 3u>;

template<typename T>
using is_general_vector4 = is_general_vector<T, 4u>;

template<typename T, size_t N = 0u>
constexpr auto is_general_vector_v = is_general_vector<T, N>::value;

template<typename T>
constexpr auto is_general_vector2_v = is_general_vector2<T>::value;

template<typename T>
constexpr auto is_general_vector3_v = is_general_vector3<T>::value;

template<typename T>
constexpr auto is_general_vector4_v = is_general_vector4<T>::value;

template<typename... Ts>
using is_any_general_vector = std::disjunction<is_general_vector<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_any_general_vector)

template<typename... Ts>
using is_all_general_vector = std::conjunction<is_general_vector<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_general_vector)

#define OC_MAKE_IS_ALL_CLS(cls, dim)                                  \
    template<typename... Ts>                                          \
    using is_all_##cls##dim = std::conjunction<is_##cls##dim<Ts>...>; \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_##cls##dim)

OC_MAKE_IS_ALL_CLS(vector, )
OC_MAKE_IS_ALL_CLS(vector, 2)
OC_MAKE_IS_ALL_CLS(vector, 3)
OC_MAKE_IS_ALL_CLS(vector, 4)

#define OC_MAKE_IS_TYPE_VECTOR_DIM(type, dim)                                                            \
    template<typename T>                                                                                 \
    using is_##type##_vector##dim = std::conjunction<is_vector##dim<T>,                                  \
                                                     std::is_same<vector_element_t<T>, type>>;           \
    OC_DEFINE_TEMPLATE_VALUE(is_##type##_vector##dim)                                                    \
    template<typename... T>                                                                              \
    using is_all_##type##_vector##dim = std::conjunction<is_##type##_vector##dim<T>...>;                 \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_##type##_vector##dim)                                          \
    template<typename T>                                                                                 \
    using is_general_##type##_vector##dim = std::conjunction<is_general_vector##dim<T>,                  \
                                                             std::is_same<type_element_t<T>, type>>;     \
    OC_DEFINE_TEMPLATE_VALUE(is_general_##type##_vector##dim)                                            \
    template<typename... T>                                                                              \
    using is_all_general_##type##_vector##dim = std::conjunction<is_general_##type##_vector##dim<T>...>; \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_general_##type##_vector##dim)

#define OC_MAKE_IS_TYPE_VECTOR(type)    \
    OC_MAKE_IS_TYPE_VECTOR_DIM(type, )  \
    OC_MAKE_IS_TYPE_VECTOR_DIM(type, 2) \
    OC_MAKE_IS_TYPE_VECTOR_DIM(type, 3) \
    OC_MAKE_IS_TYPE_VECTOR_DIM(type, 4)

OC_MAKE_IS_TYPE_VECTOR(bool)
OC_MAKE_IS_TYPE_VECTOR(int)
OC_MAKE_IS_TYPE_VECTOR(uint)
OC_MAKE_IS_TYPE_VECTOR(half)
OC_MAKE_IS_TYPE_VECTOR(float)
OC_MAKE_IS_TYPE_VECTOR(uchar)
OC_MAKE_IS_TYPE_VECTOR(char)

#define OC_MAKE_GENERAL_INTEGER_VECTOR_TRAIT(placeholder, dim)                                                              \
    template<typename T>                                                                                                    \
    using is_general_integer_vector##dim = std::disjunction<is_general_uint_vector##dim<T>, is_general_int_vector##dim<T>>; \
    OC_DEFINE_TEMPLATE_VALUE(is_general_integer_vector##dim)

// placeholder is used to avoid compile warning
OC_MAKE_GENERAL_INTEGER_VECTOR_TRAIT(placeholder, )
OC_MAKE_GENERAL_INTEGER_VECTOR_TRAIT(placeholder, 2)
OC_MAKE_GENERAL_INTEGER_VECTOR_TRAIT(placeholder, 3)
OC_MAKE_GENERAL_INTEGER_VECTOR_TRAIT(placeholder, 4)

#undef OC_MAKE_IS_TYPE_VECTOR

#undef OC_MAKE_IS_TYPE_VECTOR_DIM

namespace detail {

template<typename T, size_t N = 0u>
struct is_matrix_impl : std::false_type {};

template<typename T, size_t N, size_t M>
struct is_matrix_impl<Matrix<T, N, M>, M> : std::true_type {};

template<typename T, size_t N, size_t M>
struct is_matrix_impl<Matrix<T, N, M>, 0u> : std::true_type {};
}// namespace detail

template<typename T, size_t N = 0u>
using is_matrix = detail::is_matrix_impl<std::remove_cvref_t<T>, N>;

template<typename T>
using is_matrix2 = is_matrix<T, 2u>;

template<typename T>
using is_matrix3 = is_matrix<T, 3u>;

template<typename T>
using is_matrix4 = is_matrix<T, 4u>;

OC_MAKE_IS_ALL_CLS(matrix, )
OC_MAKE_IS_ALL_CLS(matrix, 2)
OC_MAKE_IS_ALL_CLS(matrix, 3)
OC_MAKE_IS_ALL_CLS(matrix, 4)

#undef OC_MAKE_IS_ALL_CLS

template<typename T, size_t N = 0u>
constexpr auto is_matrix_v = is_matrix<T, N>::value;

template<typename T>
constexpr auto is_matrix2_v = is_matrix2<T>::value;

template<typename T>
constexpr auto is_matrix3_v = is_matrix3<T>::value;

template<typename T>
constexpr auto is_matrix4_v = is_matrix4<T>::value;

template<typename T>
using is_basic = std::disjunction<is_scalar<T>, is_vector<T>, is_matrix<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_basic)

template<typename... Ts>
using is_all_basic = std::conjunction<is_basic<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_basic)

template<typename T>
using is_general_basic = std::disjunction<is_basic<T>, is_host_swizzle<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_general_basic)

template<typename... Ts>
using is_all_general_basic = std::conjunction<is_general_basic<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_general_basic)

template<typename T>
using is_simple_type = std::conjunction<
    std::is_same<T, std::remove_cvref_t<T>>,
    std::is_trivially_copyable<T>,
    std::is_trivially_destructible<T>>;

namespace detail {
template<typename T>
struct is_std_vector_impl : std::false_type {};

template<typename T>
struct is_std_vector_impl<ocarina::vector<T>> : std::true_type {};
}// namespace detail

template<typename T>
using is_std_vector = detail::is_std_vector_impl<std::remove_cvref_t<T>>;

template<typename T>
static constexpr bool is_std_vector_v = detail::is_std_vector_impl<std::remove_cvref_t<T>>::value;

template<typename T, size_t N>
struct general_vector {
private:
    [[nodiscard]] constexpr static auto func() noexcept {
        using raw_type = std::remove_cvref_t<T>;
        if constexpr (N == 1) {
            return raw_type{};
        } else {
            return Vector<raw_type, N>{};
        }
    }

public:
    using type = decltype(func());
};

template<typename T, size_t N>
using general_vector_t = typename general_vector<T, N>::type;

namespace detail {
template<typename... Ts>
struct match_basic_func_impl : std::conjunction<is_same_type_dimension<Ts...>,
                                                is_all_general_basic<Ts...>,
                                                is_same_type_element<Ts...>> {};
}// namespace detail

template<typename... Ts>
using match_basic_func = detail::match_basic_func_impl<std::remove_cvref_t<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(match_basic_func)

}// namespace ocarina