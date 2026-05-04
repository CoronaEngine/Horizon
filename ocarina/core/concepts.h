//
// Created by Zero on 26/04/2022.
//

#pragma once

#include "math/basic_traits.h"
#include "stl.h"

namespace ocarina::concepts {

struct Noncopyable {
    Noncopyable() noexcept = default;
    Noncopyable(const Noncopyable &) noexcept = delete;
    Noncopyable &operator=(const Noncopyable &) noexcept = delete;
    Noncopyable(Noncopyable &&) noexcept = default;
    Noncopyable &operator=(Noncopyable &&) noexcept = default;
};

template<typename T>
concept iterable = requires(T v) {
    v.begin();
    v.end();
};

template<typename T>
concept subscriptable = requires(T v) {
    v[0];
    v.at(0);
};

template<typename T>
concept string_viewable = requires(T v) {
    ocarina::string_view{v};
};

template<typename... Ts>
concept all_string_viewable = (string_viewable<Ts> && ...);

template<typename T>
concept string_convertible = requires(T v) {
    ocarina::string{v};
};

template<typename... Ts>
concept all_string_convertible = (string_convertible<Ts> && ...);

template<typename T>
concept span_convertible = requires(T v) {
    ocarina::span{v};
};

template<typename T, typename... Args>
concept constructible = requires(Args... args) {
    T{args...};
};

template<typename T, typename F>
concept selectable = is_all_scalar_v<T, F> && requires {
    bool{} ? static_cast<decltype(std::declval<T>() + std::declval<F>())>(std::declval<T>()) :
             static_cast<decltype(std::declval<T>() + std::declval<F>())>(std::declval<F>());
};

template<typename Dest, typename Src>
concept static_convertible = requires(Src s) {
    static_cast<Dest>(s);
};

template<typename Dest, typename Src>
concept bitwise_convertible = sizeof(Src) == sizeof(Dest);

template<typename Dest, typename Src>
concept reinterpret_convertible = requires(Src s) {
    reinterpret_cast<Dest *>(&s);
};

template<typename F, typename... Args>
concept invocable = std::is_invocable_v<F, Args...>;

template<typename Ret, typename F, typename... Args>
concept invocable_with_return = std::is_invocable_r_v<Ret, F, Args...>;

template<typename T>
concept pointer = std::is_pointer_v<T>;

template<typename T>
concept non_pointer = !std::is_pointer_v<T>;

template<typename T>
concept container = requires(T a) {
    a.begin();
    a.size();
};

template<typename T>
concept integral = is_integral_v<T>;

template<typename T>
concept scalar = is_scalar_v<T>;

template<typename T>
concept vector = is_vector_v<T>;

template<typename T>
concept vector2 = is_vector2_v<T>;

template<typename T>
concept vector3 = is_vector3_v<T>;

template<typename T>
concept vector4 = is_vector4_v<T>;

template<typename T>
concept bool_vector = is_bool_vector_v<T>;

template<typename T>
concept float_vector = is_float_vector_v<T>;

template<typename T>
concept int_vector = is_int_vector_v<T>;

template<typename T>
concept uint_vector = is_uint_vector_v<T>;

template<typename T>
concept matrix = is_matrix_v<T>;

template<typename T>
concept matrix2 = is_matrix2_v<T>;

template<typename T>
concept matrix3 = is_matrix3_v<T>;

template<typename T>
concept matrix4 = is_matrix4_v<T>;

template<typename T>
concept basic = is_basic_v<T>;

template<typename... Ts>
concept all_basic = is_all_basic_v<Ts...>;

template<typename... T>
concept same = is_same_v<T...>;

template<typename... T>
concept all_integral = (integral<T> && ...);

template<typename A, typename B>
concept different = !same<A, B>;

template<typename T>
concept bool_able = requires(T t) { bool(t); };

template<typename T>
concept switch_able = std::is_enum_v<T> || ocarina::is_integral_v<T>;

#define OC_MAKE_UNARY_CHECK(concept_name, op) \
    template<typename T>                      \
    concept concept_name##_able = requires(T t) { +t; };

OC_MAKE_UNARY_CHECK(positive, +)
OC_MAKE_UNARY_CHECK(negative, -)
OC_MAKE_UNARY_CHECK(not, !)
OC_MAKE_UNARY_CHECK(bit_not, ~)

#undef OC_MAKE_UNARY_CHECK

#define OC_MAKE_BINARY_CHECK(concept_name, op) \
    template<typename A, typename B>           \
    concept concept_name##_able = requires(A a, B b) { a op b; };

OC_MAKE_BINARY_CHECK(plus, +)
OC_MAKE_BINARY_CHECK(minus, -)
OC_MAKE_BINARY_CHECK(multiply, *)
OC_MAKE_BINARY_CHECK(divide, /)
OC_MAKE_BINARY_CHECK(mode, %)
OC_MAKE_BINARY_CHECK(bit_and, &)
OC_MAKE_BINARY_CHECK(bit_or, |)
OC_MAKE_BINARY_CHECK(bit_xor, ^)
OC_MAKE_BINARY_CHECK(shift_left, <<)
OC_MAKE_BINARY_CHECK(shift_right, >>)
OC_MAKE_BINARY_CHECK(and, &&)
OC_MAKE_BINARY_CHECK(or, ||)
OC_MAKE_BINARY_CHECK(equal, ==)
OC_MAKE_BINARY_CHECK(NE, !=)
OC_MAKE_BINARY_CHECK(LT, <)
OC_MAKE_BINARY_CHECK(GT, >)
OC_MAKE_BINARY_CHECK(LE, <=)
OC_MAKE_BINARY_CHECK(GE, >=)
OC_MAKE_BINARY_CHECK(assign, =)

OC_MAKE_BINARY_CHECK(plus_assign, +=)
OC_MAKE_BINARY_CHECK(minus_assgin, -=)
OC_MAKE_BINARY_CHECK(multiply_assign, *=)
OC_MAKE_BINARY_CHECK(divide_assgin, /=)
OC_MAKE_BINARY_CHECK(mode_assgin, %=)
OC_MAKE_BINARY_CHECK(bit_and_assgin, &=)
OC_MAKE_BINARY_CHECK(bit_or_assgin, |=)
OC_MAKE_BINARY_CHECK(bit_xor_assgin, ^=)
OC_MAKE_BINARY_CHECK(shift_left_assgin, <<=)
OC_MAKE_BINARY_CHECK(right_left_assgin, >>=)

#undef OC_MAKE_BINARY_CHECK

}// namespace ocarina::concepts
