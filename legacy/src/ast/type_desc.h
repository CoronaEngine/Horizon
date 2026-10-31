#pragma once

#include "core/type_system/type_desc.h"
#include "math/basic_traits.h"
#include "math/half.h"
#include "math/storage_traits.h"

namespace horizon::core {

#define HORIZON_MATH_VECTOR_DESC(TYPE, NAME, N)                             \
    template<>                                                              \
    struct TypeDesc<horizon::math::Vector<TYPE, N>> {                       \
        static constexpr string_view description() noexcept {              \
            return string_view("vector<" NAME "," #N ">");                \
        }                                                                   \
        static constexpr string_view name() noexcept {                      \
            return string_view(NAME #N);                                     \
        }                                                                   \
    };

#define HORIZON_MATH_SCALAR_DESC(S)                                         \
    template<>                                                              \
    struct TypeDesc<horizon::math::S> {                                     \
        static constexpr string_view description() noexcept {               \
            return string_view(#S);                                         \
        }                                                                   \
        static constexpr string_view name() noexcept {                      \
            return description();                                           \
        }                                                                   \
    };                                                                      \
    HORIZON_MATH_VECTOR_DESC(horizon::math::S, #S, 2)                        \
    HORIZON_MATH_VECTOR_DESC(horizon::math::S, #S, 3)                        \
    HORIZON_MATH_VECTOR_DESC(horizon::math::S, #S, 4)

HORIZON_MATH_SCALAR_DESC(real)
HORIZON_MATH_SCALAR_DESC(half)

#define HORIZON_MATH_VECTOR_SET(TYPE, NAME)                                 \
    HORIZON_MATH_VECTOR_DESC(TYPE, NAME, 2)                                  \
    HORIZON_MATH_VECTOR_DESC(TYPE, NAME, 3)                                  \
    HORIZON_MATH_VECTOR_DESC(TYPE, NAME, 4)

HORIZON_MATH_VECTOR_SET(bool, "bool")
HORIZON_MATH_VECTOR_SET(float, "float")
HORIZON_MATH_VECTOR_SET(int, "int")
HORIZON_MATH_VECTOR_SET(uint, "uint")
HORIZON_MATH_VECTOR_SET(uchar, "uchar")
HORIZON_MATH_VECTOR_SET(char, "char")
HORIZON_MATH_VECTOR_SET(ulong, "ulong")
HORIZON_MATH_VECTOR_SET(ushort, "ushort")
HORIZON_MATH_VECTOR_SET(short, "short")

#undef HORIZON_MATH_VECTOR_SET
#undef HORIZON_MATH_SCALAR_DESC
#undef HORIZON_MATH_VECTOR_DESC

template<typename T, size_t N, size_t M>
struct TypeDesc<horizon::math::Matrix<T, N, M>> {
    static string &description() noexcept {
        static thread_local auto value = horizon::core::format(
            "matrix<{},{},{}>", TypeDesc<T>::description(), N, M);
        return value;
    }

    static string &name() noexcept {
        static thread_local auto value = horizon::core::format(
            "{}{}x{}", TypeDesc<T>::name(), N, M);
        return value;
    }
};

template<typename T>
[[nodiscard]] string to_str(const T &value) noexcept {
    static string type_string = string(TypeDesc<T>::name());
    if constexpr (horizon::math::is_vector2_v<T>) {
        return horizon::core::format(type_string + "({}, {})", to_str(value.x), to_str(value.y));
    } else if constexpr (horizon::math::is_vector3_v<T>) {
        return horizon::core::format(type_string + "({}, {}, {})", to_str(value.x), to_str(value.y), to_str(value.z));
    } else if constexpr (horizon::math::is_vector4_v<T>) {
        return horizon::core::format(type_string + "({}, {}, {}, {})", to_str(value.x), to_str(value.y), to_str(value.z), to_str(value.w));
    } else if constexpr (horizon::math::is_matrix2_v<T>) {
        return horizon::core::format("[{},\n {}]", to_str(value[0]), to_str(value[1]));
    } else if constexpr (horizon::math::is_matrix3_v<T>) {
        return horizon::core::format("[{},\n {},\n {}]", to_str(value[0]), to_str(value[1]), to_str(value[2]));
    } else if constexpr (horizon::math::is_matrix4_v<T>) {
        return horizon::core::format("[{},\n {},\n {},\n {}]", to_str(value[0]), to_str(value[1]), to_str(value[2]), to_str(value[3]));
    } else if constexpr (horizon::math::is_scalar_v<T>) {
        if constexpr (horizon::math::is_half_v<T>) {
            return std::to_string(horizon::math::half2float(value));
        } else {
            return std::to_string(value);
        }
    } else if constexpr (horizon::core::is_struct_v<T>) {
        string result = horizon::core::format("{}[", horizon::core::struct_member_tuple<T>::struct_name);
        horizon::core::for_each_struct_member(value, [&](const auto &element, horizon::core::uint index) {
            if (index == horizon::core::struct_member_tuple<T>::offset_array.size() - 1) {
                result += to_str(element);
            } else {
                result += to_str(element) + ",";
            }
        });
        return result + "]";
    } else {
        static_assert(horizon::core::always_false_v<T>);
        return {};
    }
}

} // namespace horizon::core
