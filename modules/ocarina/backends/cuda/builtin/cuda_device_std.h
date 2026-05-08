
#pragma once

#include <cuda_fp16.h>

#ifndef OCARINA_CUDA_USE_FLOAT32
#define OCARINA_CUDA_USE_FLOAT32 1
#endif

using oc_int = int;
using oc_uint = unsigned int;
using oc_half = half;
using oc_float = float;
using oc_bool = bool;
using oc_uchar = unsigned char;
using oc_ushort = unsigned short;
using oc_ulong = unsigned long long;

#if OCARINA_CUDA_USE_FLOAT32
using oc_real = oc_float;
#else
using oc_real = oc_half;
#endif

using real = oc_real;

#define OC_DEFINE_TEMPLATE_VALUE(template_name) \
    template<typename T>                        \
    static constexpr auto template_name##_v = template_name<T>::value;

#define OC_DEFINE_TEMPLATE_VALUE_MULTI(template_name) \
    template<typename... Ts>                          \
    static constexpr auto template_name##_v = template_name<Ts...>::value;

#define OC_DEFINE_TEMPLATE_TYPE(template_name) \
    template<typename T>                       \
    using template_name##_t = typename template_name<T>::type;

#define OC_DEFINE_TEMPLATE_TYPE_MULTI(template_name) \
    template<typename... Ts>                         \
    using template_name##_t = typename template_name<Ts...>::type;

namespace ocarina {

using ulong = unsigned long long;
using ushort = unsigned short;
using uchar = unsigned char;
using uint = unsigned int;
using real = oc_real;

template<typename... Ts>
struct always_false {
    static constexpr bool value = false;
};

template<typename... Ts>
static constexpr bool always_false_v = always_false<Ts...>::value;

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    using type = integral_constant;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template<typename T>
struct remove_reference {
    using type = T;
};

template<typename T>
struct remove_reference<T &> {
    using type = T;
};

template<typename T>
struct remove_reference<T &&> {
    using type = T;
};

template<typename T>
struct remove_cv {
    using type = T;
};

template<typename T>
struct remove_cv<const T> {
    using type = T;
};

template<typename T>
struct remove_cv<volatile T> {
    using type = T;
};

template<typename T>
struct remove_cv<const volatile T> {
    using type = T;
};

template<typename T>
struct remove_cvref {
    using type = typename remove_cv<typename remove_reference<T>::type>::type;
};

template<typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

template<bool B, typename T, typename F>
struct conditional {
    using type = F;
};

template<typename T, typename F>
struct conditional<true, T, F> {
    using type = T;
};

template<bool B, typename T, typename F>
using conditional_t = typename conditional<B, T, F>::type;

template<typename... Ts>
struct disjunction {};

template<>
struct disjunction<> : false_type {};

template<typename First, typename... Rest>
struct disjunction<First, Rest...> : conditional_t<bool(First::value), First, disjunction<Rest...>> {};

OC_DEFINE_TEMPLATE_VALUE(disjunction)

template<typename... B>
struct conjunction;

template<>
struct conjunction<> : true_type {};

template<typename First, typename... Rest>
struct conjunction<First, Rest...>
    : conditional_t<(sizeof...(Rest) == 0 || !bool(First::value)), First, conjunction<Rest...>> {};

template<typename B>
struct negation : integral_constant<bool, !bool(B::value)> {};

namespace detail {
template<typename T>
struct is_integral_impl : public false_type {};

template<>
struct is_integral_impl<int> : public true_type {};

template<>
struct is_integral_impl<unsigned int> : public true_type {};

template<>
struct is_integral_impl<unsigned long long> : public true_type {};
}// namespace detail

template<typename T>
using is_integral = detail::is_integral_impl<remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_integral)

#define OC_MAKE_IS_TYPE(type)                                      \
    namespace detail {                                             \
    template<typename T>                                           \
    struct is_##type##_impl : public false_type {};                \
                                                                   \
    template<>                                                     \
    struct is_##type##_impl<type> : public true_type {};           \
    }                                                              \
    template<typename T>                                           \
    using is_##type = detail::is_##type##_impl<remove_cvref_t<T>>; \
    OC_DEFINE_TEMPLATE_VALUE(is_##type)

OC_MAKE_IS_TYPE(int)
OC_MAKE_IS_TYPE(float)
OC_MAKE_IS_TYPE(half)
OC_MAKE_IS_TYPE(bool)
OC_MAKE_IS_TYPE(uint)

#undef OC_MAKE_IS_TYPE

namespace detail {
template<typename T>
struct is_floating_point_impl : public false_type {};

template<>
struct is_floating_point_impl<float> : public true_type {};

template<>
struct is_floating_point_impl<half> : public true_type {};
}// namespace detail
template<typename T>
using is_floating_point = detail::is_floating_point_impl<remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_floating_point)

namespace detail {
template<typename T>
struct is_boolean_impl : public false_type {};

template<>
struct is_boolean_impl<bool> : public true_type {};
}// namespace detail
template<typename T>
using is_boolean = detail::is_boolean_impl<remove_cvref_t<T>>;
OC_DEFINE_TEMPLATE_VALUE(is_boolean)

template<typename T>
struct is_number : disjunction<is_integral<T>, is_floating_point<T>> {};
OC_DEFINE_TEMPLATE_VALUE(is_number)

template<typename T>
struct is_scalar : disjunction<is_integral<T>, is_floating_point<T>, is_boolean<T>> {};
OC_DEFINE_TEMPLATE_VALUE(is_scalar)

#define MAKE_TYPE_TRAITS(type)                             \
    template<typename... Ts>                               \
    using is_all_##type = conjunction<is_##type<Ts>...>;   \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_all_##type)          \
    template<typename... Ts>                               \
    using is_any_##type = disjunction<is_##type<Ts>...>;   \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_any_##type)          \
    template<typename... Ts>                               \
    using is_none_##type = negation<is_any_##type<Ts...>>; \
    OC_DEFINE_TEMPLATE_VALUE_MULTI(is_none_##type)

MAKE_TYPE_TRAITS(scalar)
MAKE_TYPE_TRAITS(number)
MAKE_TYPE_TRAITS(integral)
MAKE_TYPE_TRAITS(floating_point)
MAKE_TYPE_TRAITS(boolean)

#undef MAKE_TYPE_TRAITS

namespace detail {
template<typename... Ts>
struct make_void {
    using type = void;
};
}// namespace detail

template<typename... Ts>
using void_t = typename detail::make_void<Ts...>::type;

template<size_t... Ints>
struct index_sequence {};

template<size_t N, size_t... Ints>
struct make_index_sequence_helper : make_index_sequence_helper<N - 1, N - 1, Ints...> {
};

template<size_t... Ints>
struct make_index_sequence_helper<0, Ints...> {
    using type = index_sequence<Ints...>;
};

template<size_t N>
using make_index_sequence = typename make_index_sequence_helper<N>::type;

template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<typename, typename, typename = void>
struct is_addable : false_type {};

template<typename T, typename F>
struct is_addable<T, F, void_t<decltype(T{} + F{})>> : true_type {};

template<typename T, typename F>
struct is_selectable : conjunction<is_all_scalar<T, F>, is_addable<T, F>> {};
OC_DEFINE_TEMPLATE_VALUE_MULTI(is_selectable)

#define OC_DEVICE_FLAG

template<typename T, size_t N>
class array {
private:
    T _data[N];

public:
    OC_DEVICE_FLAG constexpr array() noexcept : _data{} {}
    template<typename... Elem>
    OC_DEVICE_FLAG constexpr array(Elem... elem) noexcept : _data{elem...} {}
    OC_DEVICE_FLAG constexpr array(array &&) noexcept = default;
    OC_DEVICE_FLAG constexpr array(const array &) noexcept = default;
    OC_DEVICE_FLAG constexpr array &operator=(array &&) noexcept = default;
    OC_DEVICE_FLAG constexpr array &operator=(const array &) noexcept = default;
    OC_DEVICE_FLAG constexpr T *data() noexcept { return &_data[0]; }
    OC_DEVICE_FLAG constexpr const T *data() const noexcept { return &_data[0]; }
    [[nodiscard]] OC_DEVICE_FLAG T &operator[](size_t i) noexcept { return _data[i]; }
    [[nodiscard]] OC_DEVICE_FLAG T operator[](size_t i) const noexcept { return _data[i]; }
};

}// namespace ocarina

template<typename T, size_t N>
using oc_array = ocarina::array<T, N>;

#undef OC_DEFINE_TEMPLATE_VALUE_MULTI
#undef OC_DEFINE_TEMPLATE_VALUE
#undef OC_DEFINE_TEMPLATE_TYPE
#undef OC_DEFINE_TEMPLATE_TYPE_MULTI