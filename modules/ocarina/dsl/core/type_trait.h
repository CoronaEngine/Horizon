//
// Created by Zero on 11/05/2022.
//

#pragma once

#include "core/stl.h"
#include <type_traits>
#include "core/concepts.h"
#include "core/type.h"
#include "math/basic_traits.h"

namespace ocarina {

template<typename T>
struct Var;

template<typename T>
struct Expr;

template<typename T>
class DynamicArray;

template<typename T>
class Buffer;

class ByteBuffer;

namespace detail {

template<typename T>
struct Ref;

/// var
template<typename T>
struct var_to_prototype {
    static_assert(always_false_v<T>, "Invalid type in function definition.");
};

template<typename T>
struct var_to_prototype<Var<T>> {
    using type = T;
};

template<typename T>
struct var_to_prototype<const Var<T> &> {
    using type = T;
};

template<typename T>
struct var_to_prototype<Var<T> &> {
    using type = T &;
};

template<typename T>
struct prototype_to_var {
    using type = Var<T>;
};

template<>
struct prototype_to_var<void> {
    using type = void;
};

template<typename T>
struct prototype_to_var<T &> {
    using type = Var<T> &;
};

template<typename T>
struct prototype_to_var<const T &> {
    using type = const Var<T> &;
};

template<typename T>
using prototype_to_var_t = typename prototype_to_var<T>::type;

template<typename T>
struct expr_value_impl {
    using type = T;
};

template<>
struct expr_value_impl<void> {
    using type = void;
};

template<typename T>
struct expr_value_impl<Expr<T>> {
    using type = T;
};

template<typename T>
struct expr_value_impl<Ref<T>> {
    using type = T;
};

template<typename T>
struct expr_value_impl<Var<T>> {
    using type = T;
};

template<typename T>
struct expr_value_impl<DynamicArray<T>> {
    using type = vector<T>;
};

}// namespace detail

template<typename T>
using expr_value = detail::expr_value_impl<std::remove_cvref_t<T>>;

template<typename T>
using expr_value_t = typename expr_value<T>::type;

namespace detail {
template<typename T>
using is_valid_dsl_type_impl = std::disjunction<is_basic<T>,
                                                is_struct<T>,
                                                is_std_vector<T>,
                                                is_array<T>>;
}

template<typename T>
using is_valid_dsl_type = detail::is_valid_dsl_type_impl<std::remove_cvref_t<T>>;

template<typename T>
constexpr bool is_valid_dsl_type_v = is_valid_dsl_type<T>::value;

namespace detail {
template<typename T>
struct remove_device_impl {
    using type = expr_value_t<T>;
};

template<typename T, size_t N, size_t... Indices>
struct remove_device_impl<Swizzle<Var<T>, N, Indices...>> {
    using type = Swizzle<T, N, Indices...>;
};
}// namespace detail

namespace detail {
template<typename T>
struct dsl_impl {
    using type = Var<T>;
};

template<>
struct dsl_impl<void> {
    using type = void;
};

template<typename T>
struct dsl_impl<vector<T>> {
    using type = DynamicArray<T>;
};

template<typename T>
struct dsl_impl<BufferDesc<T>> {
    using type = Var<Buffer<T>>;
};

template<>
struct dsl_impl<BufferDesc<>> {
    using type = Var<ByteBuffer>;
};

}// namespace detail

template<typename T>
using dsl_t = typename detail::dsl_impl<std::remove_cvref_t<T>>::type;

template<typename T>
using vector_expr_element = vector_element<expr_value_t<T>>;

template<typename T>
using vector_expr_element_t = typename vector_expr_element<T>::type;

template<typename T>
using vector_expr_dimension = vector_dimension<expr_value_t<T>>;

template<typename T>
constexpr auto vector_expr_dimension_v = vector_expr_dimension<T>::value;

template<typename... T>
using is_vector_expr_same_dimension = is_vector_same_dimension<expr_value_t<T>...>;

template<typename... T>
constexpr auto is_vector_expr_same_dimension_v = is_vector_expr_same_dimension<T...>::value;

template<typename... T>
using is_vector_expr_same_element = ocarina::is_same<vector_expr_element_t<T>...>;

template<typename... T>
constexpr auto is_vector_expr_same_element_v = is_vector_expr_same_element<T...>::value;

namespace detail {

template<typename T>
struct is_dsl_impl : std::false_type {};

template<typename T>
struct is_dsl_impl<Expr<T>> : std::true_type {};

template<typename T>
struct is_dsl_impl<Ref<T>> : std::true_type {};

template<typename T>
struct is_dsl_impl<Var<T>> : std::true_type {};

template<typename T>
struct is_dsl_impl<DynamicArray<T>> : std::true_type {};

}// namespace detail

template<typename T>
using is_dsl = typename detail::is_dsl_impl<std::remove_cvref_t<T>>::type;

template<typename T>
constexpr auto is_dsl_v = is_dsl<T>::value;

template<typename... Ts>
using any_dsl = std::disjunction<is_dsl<Ts>...>;

template<typename... Ts>
constexpr auto any_dsl_v = any_dsl<Ts...>::value;

template<typename... Ts>
constexpr auto none_dsl_v = !any_dsl_v<Ts...>;

template<typename... Ts>
using all_dsl = std::conjunction<is_dsl<Ts>...>;

template<typename... Ts>
constexpr auto all_dsl_v = all_dsl<Ts...>::value;

namespace detail {
template<typename T>
struct is_device_type_impl : is_dsl_impl<T> {};

template<typename T, size_t N, size_t... Indices>
struct is_device_type_impl<Swizzle<Var<T>, N, Indices...>> : std::true_type {};
}// namespace detail

template<typename T>
using is_device_type = detail::is_device_type_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_device_type)

template<typename... Ts>
using any_device_type = std::disjunction<is_device_type<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(any_device_type)

template<typename... Ts>
using all_device_type = std::conjunction<is_device_type<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(all_device_type)

template<typename... Ts>
static constexpr auto none_device_type_v = !any_device_type_v<Ts...>;

namespace detail {
template<typename T>
struct is_var_impl : std::false_type {};

template<typename T>
struct is_var_impl<Var<T>> : std::true_type {};
}// namespace detail

template<typename T>
static constexpr bool is_var_v = detail::is_var_impl<std::remove_cvref_t<T>>::value;

namespace detail {
template<typename T>
struct is_expr_impl : std::false_type {};

template<typename T>
struct is_expr_impl<Expr<T>> : std::true_type {};
}// namespace detail

template<typename T>
static constexpr bool is_expr_v = detail::is_expr_impl<std::remove_cvref_t<T>>::value;

namespace detail {
template<typename T>
struct is_dynamic_array_impl : std::false_type {};

template<typename T>
struct is_dynamic_array_impl<DynamicArray<T>> : std::true_type {};
}// namespace detail

template<typename T>
using is_dynamic_array = typename detail::is_dynamic_array_impl<std::remove_cvref_t<T>>;

template<typename T>
static constexpr bool is_dynamic_array_v = is_dynamic_array<T>::value;

template<typename... T>
using any_dynamic_array = std::disjunction<is_dynamic_array<T>...>;

template<typename... T>
using all_dynamic_array = std::conjunction<is_dynamic_array<T>...>;

OC_DEFINE_TEMPLATE_VALUE_MULTI(any_dynamic_array)
OC_DEFINE_TEMPLATE_VALUE_MULTI(all_dynamic_array)

template<typename... T>
static constexpr auto none_dynamic_array_v = !any_dynamic_array_v<T...>;

namespace detail {

template<typename T>
struct dynamic_array_element_impl {
    using type = T;
};

template<typename T>
struct dynamic_array_element_impl<DynamicArray<T>> {
    using type = T;
};

}// namespace detail

template<typename T>
using dynamic_array_element_t = typename detail::dynamic_array_element_impl<std::remove_cvref_t<T>>::type;

template<typename... T>
using is_same_expr = ocarina::is_same<expr_value_t<T>...>;

template<typename... T>
constexpr auto is_same_expr_v = is_same_expr<T...>::value;

template<typename... Ts>
using is_all_basic_expr = is_all_basic<expr_value_t<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_basic_expr)

template<typename... Ts>
using is_all_general_basic_expr = is_all_general_basic<remove_device_t<Ts>...>;
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_general_basic_expr)

#define EXPR_TYPE_TRAITS(type)                                                    \
    template<typename T>                                                          \
    using is_##type##_expr = is_##type<expr_value_t<T>>;                          \
    OC_DEFINE_TEMPLATE_VALUE(is_##type##_expr)                                    \
    template<typename... T>                                                       \
    using is_all_##type##_expr = std::disjunction<is_##type<expr_value_t<T>>...>; \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_##type##_expr)

EXPR_TYPE_TRAITS(integral)
EXPR_TYPE_TRAITS(boolean)
EXPR_TYPE_TRAITS(floating_point)
EXPR_TYPE_TRAITS(scalar)

#undef EXPR_TYPE_TRAITS

#define EXPR_DIMENSION_TRAITS(cls, dim)                                      \
    template<typename T>                                                     \
    using is_##cls##dim##_expr = is_##cls##dim<expr_value_t<T>>;             \
    OC_DEFINE_TEMPLATE_VALUE(is_##cls##dim##_expr)                           \
    template<typename... Ts>                                                 \
    using is_all_##cls##dim##_expr = is_all_##cls##dim<expr_value_t<Ts>...>; \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_##cls##dim##_expr)

EXPR_DIMENSION_TRAITS(vector, )
EXPR_DIMENSION_TRAITS(vector, 2)
EXPR_DIMENSION_TRAITS(vector, 3)
EXPR_DIMENSION_TRAITS(vector, 4)

EXPR_DIMENSION_TRAITS(matrix, )
EXPR_DIMENSION_TRAITS(matrix, 2)
EXPR_DIMENSION_TRAITS(matrix, 3)
EXPR_DIMENSION_TRAITS(matrix, 4)

#undef EXPR_DIMENSION_TRAITS

#define EXPR_VECTOR_TYPE_TRAITS(type)                                                 \
    template<typename T>                                                              \
    using is_##type##_vector_expr = is_##type##_vector<expr_value_t<T>>;              \
    OC_DEFINE_TEMPLATE_VALUE(is_##type##_vector_expr)                                 \
    template<typename T>                                                              \
    using is_##type##_element = std::is_same<type, vector_element_t<T>>;              \
    OC_DEFINE_TEMPLATE_VALUE(is_##type##_element)                                     \
    template<typename T>                                                              \
    using is_##type##_element_expr = is_##type##_element<expr_value_t<T>>;            \
    OC_DEFINE_TEMPLATE_VALUE(is_##type##_element_expr)                                \
    template<typename... T>                                                           \
    using is_all_##type##_element = std::conjunction<is_##type##_element<T>...>;      \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_##type##_element)                           \
    template<typename... T>                                                           \
    using is_all_##type##_element_expr = is_all_##type##_element<expr_value_t<T>...>; \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_##type##_element_expr)

EXPR_VECTOR_TYPE_TRAITS(bool)
EXPR_VECTOR_TYPE_TRAITS(float)
EXPR_VECTOR_TYPE_TRAITS(int)
EXPR_VECTOR_TYPE_TRAITS(uint)
EXPR_VECTOR_TYPE_TRAITS(uchar)
EXPR_VECTOR_TYPE_TRAITS(char)

template<typename T>
using is_signed_element = std::disjunction<is_float_element<T>, is_int_element<T>>;
template<typename T>
constexpr auto is_signed_element_v = is_signed_element<T>::value;

#undef EXPR_VECTOR_TYPE_TRAITS

template<typename T>
using is_dsl_integral = std::disjunction<
    std::is_same<std::remove_cvref_t<T>, int>,
    std::is_same<std::remove_cvref_t<T>, uint>,
    std::is_same<std::remove_cvref_t<T>, uchar>,
    std::is_same<std::remove_cvref_t<T>, char>,
    std::is_same<std::remove_cvref_t<T>, short>,
    std::is_same<std::remove_cvref_t<T>, ushort>>;
OC_DEFINE_TEMPLATE_VALUE(is_dsl_integral)

template<typename T>
using is_dsl_scalar = std::disjunction<is_dsl_integral<T>,
                                       is_boolean<T>,
                                       is_char<T>,
                                       is_uchar<T>,
                                       ocarina::is_floating_point<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_dsl_scalar)

template<typename T>
using is_dsl_basic = std::disjunction<is_dsl_scalar<T>, is_vector<T>, is_matrix<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_dsl_basic)

template<typename T>
class Buffer;

template<typename T>
class BufferView;

class ByteBuffer;

namespace detail {
class RawDynamicBuffer;
class RawDynamicBufferView;
}

template<typename T>
class DynamicBuffer;

class ByteBufferView;

template<typename T>
class DynamicBufferView;

class Texture3D;
class Texture2D;
class Accel;
class BindlessArray;

template<typename T>
using is_valid_buffer_element = std::conjunction<std::negation<is_param_struct<T>>,
                                                 std::disjunction<is_simple_type<T>, is_valid_dsl_type<T>>>;

template<typename T>
constexpr auto is_valid_buffer_element_v = is_valid_buffer_element<T>::value;

namespace detail {

template<typename T>
struct is_buffer_impl : std::false_type {};

template<typename T>
struct is_buffer_impl<Buffer<T>> : std::true_type {};

template<>
struct is_buffer_impl<ByteBuffer> : std::true_type {};

template<>
struct is_buffer_impl<detail::RawDynamicBuffer> : std::true_type {};

template<typename T>
struct is_buffer_impl<DynamicBuffer<T>> : std::true_type {};

template<typename T>
struct is_buffer_view_impl : std::false_type {};

template<typename T>
struct is_buffer_view_impl<BufferView<T>> : std::true_type {};

template<>
struct is_buffer_view_impl<ByteBufferView> : std::true_type {};

template<>
struct is_buffer_view_impl<detail::RawDynamicBufferView> : std::true_type {};

template<typename T>
struct is_buffer_view_impl<DynamicBufferView<T>> : std::true_type {};

template<typename T>
struct buffer_element_impl {
    using type = T;
};

template<typename T>
struct buffer_element_impl<Buffer<T>> {
    using type = T;
};

template<typename T>
struct buffer_element_impl<BufferView<T>> {
    using type = T;
};

template<>
struct buffer_element_impl<detail::RawDynamicBuffer> {
    using type = std::byte;
};

template<>
struct buffer_element_impl<detail::RawDynamicBufferView> {
    using type = std::byte;
};

template<typename T>
struct buffer_element_impl<DynamicBuffer<T>> {
    using type = std::byte;
};

template<typename T>
struct buffer_element_impl<DynamicBufferView<T>> {
    using type = std::byte;
};

template<typename T>
struct is_byte_buffer_view : public std::false_type {};

template<>
struct is_byte_buffer_view<ByteBufferView> : public std::true_type {};

template<>
struct is_byte_buffer_view<detail::RawDynamicBufferView> : public std::true_type {};

template<typename T>
struct is_byte_buffer_view<DynamicBufferView<T>> : public std::true_type {};

};// namespace detail

template<typename T>
using buffer_element = detail::buffer_element_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_TYPE(buffer_element)

template<typename T>
using is_buffer = detail::is_buffer_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_buffer)

template<typename T>
using is_buffer_view = detail::is_buffer_view_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_buffer_view)

template<typename T>
using is_buffer_or_view = std::disjunction<is_buffer<T>, is_buffer_view<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_buffer_or_view)

template<typename T>
using is_byte_buffer_view = detail::is_byte_buffer_view<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_byte_buffer_view)

namespace detail {

template<typename T>
struct is_texture_impl : std::false_type {};

template<>
struct is_texture_impl<Texture3D> : std::true_type {};

template<>
struct is_texture_impl<Texture2D> : std::true_type {};

}// namespace detail

template<typename T>
using is_texture = detail::is_texture_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_texture)

namespace detail {

template<typename T>
struct is_texture2d_impl : std::false_type {};

template<>
struct is_texture2d_impl<Texture2D> : std::true_type {};

}// namespace detail

template<typename T>
using is_texture2d = detail::is_texture2d_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_texture2d)

namespace detail {

template<typename T>
struct is_texture3d_impl : std::false_type {};

template<>
struct is_texture3d_impl<Texture3D> : std::true_type {};

}// namespace detail

template<typename T>
using is_texture3d = detail::is_texture3d_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_texture3d)

namespace detail {
template<typename T>
struct is_accel_impl : std::false_type {};

template<>
struct is_accel_impl<Accel> : std::true_type {};

}// namespace detail

template<typename T>
using is_accel = detail::is_accel_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_accel)

namespace detail {
template<typename T>
struct is_bindless_array_impl : std::false_type {};

template<>
struct is_bindless_array_impl<BindlessArray> : std::true_type {};
}// namespace detail

template<typename T>
using is_bindless_array = detail::is_bindless_array_impl<std::remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_bindless_array)

template<typename T>
using is_array_expr = ocarina::is_array<expr_value_t<T>>;

OC_DEFINE_TEMPLATE_VALUE(is_array_expr)

namespace detail {
template<typename T>
struct variant_var_impl {
    static_assert(always_false_v<T>);
};

template<typename... Ts>
struct variant_var_impl<ocarina::variant<Ts...>> {
    using type = ocarina::variant<Var<Ts>...>;
};

}// namespace detail

using basic_variant_var_t = typename detail::variant_var_impl<basic_variant_t>::type;

}// namespace ocarina

namespace ocarina {

class Ray;

namespace detail {

template<typename T>
requires is_dsl_v<T>
Var<Ray> ray_deduce();

template<typename T>
requires(!is_dsl_v<T>)
Ray ray_deduce();

template<typename T>
struct ray_ {
    using type = decltype(ray_deduce<T>());
};

}// namespace detail

template<typename T>
using ray_t = typename detail::ray_<std::remove_cvref_t<T>>::type;

namespace detail {

template<typename T>
requires is_dsl_v<T>
Var<bool> boolean_deduce();

template<typename T>
requires(!is_dsl_v<T>)
bool boolean_deduce();

template<typename T>
struct boolean {
    using type = decltype(boolean_deduce<T>());
};

}// namespace detail

template<typename T>
using boolean_t = typename detail::boolean<std::remove_cvref_t<T>>::type;

namespace detail {

template<typename T>
requires is_scalar_v<expr_value_t<T>>
T scalar_deduce();

template<typename T>
requires is_vector_v<expr_value_t<T>>
decltype(T::x) scalar_deduce();

template<typename T>
requires is_matrix_v<expr_value_t<T>>
decltype(std::declval<T>()[0][0]) scalar_deduce();

template<typename T>
struct scalar {
    using type = decltype(scalar_deduce<std::remove_cvref_t<T>>());
};

}// namespace detail

template<typename T>
using scalar_t = typename detail::scalar<T>::type;

}// namespace ocarina

namespace ocarina {

// Used to determine where the function runs (host or device)
enum EPort {
    H,
    D
};

namespace detail {

template<typename T, EPort>
struct var_impl {};

template<typename T>
struct var_impl<T, D> {
    using type = Var<expr_value_t<T>>;
};

template<typename T>
struct var_impl<T, H> {
    using type = expr_value_t<T>;
};

}// namespace detail

template<typename T, EPort port>
using var_t = typename detail::var_impl<T, port>::type;

namespace detail {
template<typename... Ts>
struct port_impl {
    static constexpr EPort value = any_dsl_v<Ts...> ? D : H;
};
}// namespace detail

template<typename... Ts>
static constexpr auto port_v = detail::port_impl<Ts...>::value;

namespace detail {
template<typename T, typename... Args>
struct condition_impl {
    using type = var_t<T, port_v<Args...>>;
};
}// namespace detail

template<typename T, typename... Args>
using condition_t = typename detail::condition_impl<T, Args...>::type;

#define OC_MAKE_VAR_TYPE_IMPL(type, dim) \
    template<EPort port>                 \
    using oc_##type##dim = var_t<type##dim, port>;

#define OC_MAKE_VAR_TYPE(type)     \
    OC_MAKE_VAR_TYPE_IMPL(type, )  \
    OC_MAKE_VAR_TYPE_IMPL(type, 2) \
    OC_MAKE_VAR_TYPE_IMPL(type, 3) \
    OC_MAKE_VAR_TYPE_IMPL(type, 4)

struct TriangleHit;

OC_MAKE_VAR_TYPE(int)
OC_MAKE_VAR_TYPE(uint)
OC_MAKE_VAR_TYPE(ulong)
OC_MAKE_VAR_TYPE(float)
OC_MAKE_VAR_TYPE(char)
OC_MAKE_VAR_TYPE(uchar)
OC_MAKE_VAR_TYPE(bool)
OC_MAKE_VAR_TYPE(half)
OC_MAKE_VAR_TYPE_IMPL(Ray, )
OC_MAKE_VAR_TYPE_IMPL(TriangleHit, )

#define OC_MAKE_VAR_MAT(dim) \
    template<EPort port>     \
    using oc_float##dim##x##dim = var_t<float##dim##x##dim, port>;
OC_MAKE_VAR_MAT(2)
OC_MAKE_VAR_MAT(3)
OC_MAKE_VAR_MAT(4)

#undef OC_MAKE_VAR_MAT
#undef OC_MAKE_VAR_TYPE
#undef OC_MAKE_VAR_TYPE_IMPL

}// namespace ocarina
