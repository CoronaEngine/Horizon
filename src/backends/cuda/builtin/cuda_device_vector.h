
#pragma once

#include "cuda_device_scalar.h"
#include "cuda_device_std.h"

namespace ocarina {

namespace detail {
template<typename T, size_t N>
struct VectorStorage {
    static_assert(always_false_v<T>, "Invalid vector storage");
};

template<typename T>
struct alignas(sizeof(T) * 2) VectorStorage<T, 2> {
    T x{}, y{};
    OC_DEVICE_FLAG constexpr VectorStorage(T s = T{}) noexcept : x{s}, y{s} {}
    OC_DEVICE_FLAG constexpr VectorStorage(T x, T y) noexcept : x{x}, y{y} {}
    OC_DEVICE_FLAG constexpr VectorStorage(const T *ptr) noexcept : x{ptr[0]}, y{ptr[1]} {}
};

template<typename T>
struct alignas(sizeof(T) * 4) VectorStorage<T, 3> {
    T x{}, y{}, z{};
    OC_DEVICE_FLAG constexpr VectorStorage(T s = T{}) noexcept : x{s}, y{s}, z{s} {}
    OC_DEVICE_FLAG constexpr VectorStorage(T x, T y, T z) noexcept : x{x}, y{y}, z{z} {}
    OC_DEVICE_FLAG constexpr VectorStorage(const T *ptr) noexcept : x{ptr[0]}, y{ptr[1]}, z{ptr[2]} {}
};

template<typename T>
struct alignas(sizeof(T) * 4) VectorStorage<T, 4> {
    T x{}, y{}, z{}, w{};
    OC_DEVICE_FLAG constexpr VectorStorage(T s = T{}) noexcept : x{s}, y{s}, z{s}, w{s} {}
    OC_DEVICE_FLAG constexpr VectorStorage(T x, T y, T z, T w) noexcept : x{x}, y{y}, z{z}, w{w} {}
    OC_DEVICE_FLAG constexpr VectorStorage(const T *ptr) noexcept : x{ptr[0]}, y{ptr[1]}, z{ptr[2]}, w{ptr[3]} {}
};
}// namespace detail

namespace detail {

}// namespace detail

template<typename T, size_t N>
struct Vector : public detail::VectorStorage<T, N> {
    using detail::VectorStorage<T, N>::VectorStorage;

private:
    template<typename U, size_t NN, size_t... i>
    static Vector<T, N> construct_helper(Vector<U, NN> v,
                                         ocarina::index_sequence<i...>) {
        return Vector<T, N>(oc_static_cast<T>(v[i])...);
    }

    template<typename U, size_t... i>
    void assign_helper(const Vector<U, N> &other, ocarina::index_sequence<i...>) noexcept {
        ((this->operator[](i) = static_cast<T>(other[i])), ...);
    }

public:
    template<typename U>
    explicit constexpr Vector(U s) noexcept : Vector(oc_static_cast<T>(s)) {}

    template<typename U, size_t NN, ocarina::enable_if_t<(NN > N), int> = 0>
    explicit constexpr Vector(Vector<U, NN> v)
        : Vector{construct_helper(v, ocarina::make_index_sequence<N>())} {}

    template<typename U>
    constexpr Vector(Vector<U, N> v)
        : Vector{construct_helper(v, ocarina::make_index_sequence<N>())} {}

    template<typename U>
    constexpr Vector &operator=(Vector<U, N> other) noexcept {
        assign_helper<U>(other, ocarina::make_index_sequence<N>());
        return *this;
    }

    OC_DEVICE_FLAG constexpr T &operator[](size_t index) noexcept { return (&(this->x))[index]; }
    OC_DEVICE_FLAG constexpr const T &operator[](size_t index) const noexcept { return (&(this->x))[index]; }
};

using uint = unsigned int;
using ulong = unsigned long long;
using uchar = unsigned char;
using ushort = unsigned short;

#define OC_MAKE_VECTOR_TYPES(T) \
    using T##2 = Vector<T, 2>;  \
    using T##3 = Vector<T, 3>;  \
    using T##4 = Vector<T, 4>;

OC_MAKE_VECTOR_TYPES(bool)
OC_MAKE_VECTOR_TYPES(float)
OC_MAKE_VECTOR_TYPES(half)
OC_MAKE_VECTOR_TYPES(real)
OC_MAKE_VECTOR_TYPES(int)
OC_MAKE_VECTOR_TYPES(char)
OC_MAKE_VECTOR_TYPES(short)
OC_MAKE_VECTOR_TYPES(ushort)
OC_MAKE_VECTOR_TYPES(uchar)
OC_MAKE_VECTOR_TYPES(uint)
OC_MAKE_VECTOR_TYPES(ulong)

#undef OC_MAKE_VECTOR_TYPES

}// namespace ocarina

#define OC_MAKE_VECTOR_N(type, dim) using type##dim = ocarina::Vector<type, dim>;

template<template<typename, size_t> typename Container, typename T, size_t N>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto
operator+(const Container<T, N> v) noexcept {
    return v;
}

#define OC_MAKE_UNARY_VECTOR_OPERATOR(op)                 \
    template<typename T, size_t N>                        \
    [[nodiscard]] OC_DEVICE_FLAG constexpr auto           \
    operator op(const ocarina::Vector<T, N> v) noexcept { \
        using R = ocarina::Vector<decltype(op T{}), N>;   \
        if constexpr (N == 2) {                           \
            return R{op v.x, op v.y};                     \
        } else if constexpr (N == 3) {                    \
            return R{op v.x, op v.y, op v.z};             \
        } else {                                          \
            return R{op v.x, op v.y, op v.z, op v.w};     \
        }                                                 \
    }

OC_MAKE_UNARY_VECTOR_OPERATOR(-)
OC_MAKE_UNARY_VECTOR_OPERATOR(!)
OC_MAKE_UNARY_VECTOR_OPERATOR(~)

#define OC_MAKE_VECTOR(type)  \
    OC_MAKE_VECTOR_N(type, 2) \
    OC_MAKE_VECTOR_N(type, 3) \
    OC_MAKE_VECTOR_N(type, 4)

OC_MAKE_VECTOR(oc_int)
OC_MAKE_VECTOR(oc_uint)
OC_MAKE_VECTOR(oc_float)
OC_MAKE_VECTOR(oc_half)
OC_MAKE_VECTOR(oc_real)
OC_MAKE_VECTOR(oc_bool)
OC_MAKE_VECTOR(oc_uchar)
OC_MAKE_VECTOR(oc_ushort)
OC_MAKE_VECTOR(oc_ulong)

#undef OC_MAKE_VECTOR

#define OC_MAKE_VECTOR_BINARY_OPERATOR(op, ...)                          \
    template<typename T, typename U, size_t N>                           \
    [[nodiscard]] OC_DEVICE_FLAG constexpr auto                          \
    operator op(                                                         \
        ocarina::Vector<T, N> lhs, ocarina::Vector<U, N> rhs) noexcept { \
        using ret_type = decltype(T{} + U{});                            \
        if constexpr (N == 2) {                                          \
            return ocarina::Vector<ret_type, 2>{                         \
                lhs.x op rhs.x,                                          \
                lhs.y op rhs.y};                                         \
        } else if constexpr (N == 3) {                                   \
            return ocarina::Vector<ret_type, 3>{                         \
                lhs.x op rhs.x,                                          \
                lhs.y op rhs.y,                                          \
                lhs.z op rhs.z};                                         \
        } else {                                                         \
            return ocarina::Vector<ret_type, 4>{                         \
                lhs.x op rhs.x,                                          \
                lhs.y op rhs.y,                                          \
                lhs.z op rhs.z,                                          \
                lhs.w op rhs.w};                                         \
        }                                                                \
    }                                                                    \
    template<typename T, typename U, size_t N>                           \
    [[nodiscard]] OC_DEVICE_FLAG constexpr auto                          \
    operator op(ocarina::Vector<T, N> lhs, U rhs) noexcept {             \
        return lhs op ocarina::Vector<U, N>{rhs};                        \
    }                                                                    \
    template<typename T, typename U, size_t N>                           \
    [[nodiscard]] OC_DEVICE_FLAG constexpr auto                          \
    operator op(T lhs, ocarina::Vector<U, N> rhs) noexcept {             \
        return ocarina::Vector<T, N>{lhs} op rhs;                        \
    }                                                                    \
    template<typename T, typename U, size_t N>                           \
    OC_DEVICE_FLAG constexpr decltype(auto)                              \
    operator op## = (ocarina::Vector<T, N> & lhs,                        \
                     ocarina::Vector<U, N> rhs) noexcept {               \
        lhs = lhs op rhs;                                                \
        return lhs;                                                      \
    }                                                                    \
    template<typename T, typename U, size_t N>                           \
    OC_DEVICE_FLAG constexpr decltype(auto)                              \
    operator op## = (ocarina::Vector<T, N> & lhs, U rhs) noexcept {      \
        return (lhs op ocarina::Vector<U, N>{rhs});                      \
    }

OC_MAKE_VECTOR_BINARY_OPERATOR(+, ocarina::is_all_number_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(-, ocarina::is_all_number_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(*, ocarina::is_all_number_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(/, ocarina::is_all_number_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(%, ocarina::is_all_number_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(>>, ocarina::is_all_integral_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(<<, ocarina::is_all_integral_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(|, ocarina::is_all_integral_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(&, ocarina::is_all_integral_v<T, U>)
OC_MAKE_VECTOR_BINARY_OPERATOR(^, ocarina::is_all_integral_v<T, U>)

#undef OC_MAKE_VECTOR_BINARY_OPERATOR

#define OC_MAKE_VECTOR_LOGIC_OPERATOR(op, ...)                           \
    template<typename T, size_t N>                                       \
    [[nodiscard]] OC_DEVICE_FLAG constexpr auto                          \
    operator op(                                                         \
        ocarina::Vector<T, N> lhs, ocarina::Vector<T, N> rhs) noexcept { \
        if constexpr (N == 2) {                                          \
            return ocarina::Vector<bool, 2>{                             \
                lhs.x op rhs.x,                                          \
                lhs.y op rhs.y};                                         \
        } else if constexpr (N == 3) {                                   \
            return ocarina::Vector<bool, 3>{                             \
                lhs.x op rhs.x,                                          \
                lhs.y op rhs.y,                                          \
                lhs.z op rhs.z};                                         \
        } else {                                                         \
            return ocarina::Vector<bool, 4>{                             \
                lhs.x op rhs.x,                                          \
                lhs.y op rhs.y,                                          \
                lhs.z op rhs.z,                                          \
                lhs.w op rhs.w};                                         \
        }                                                                \
    }                                                                    \
    template<typename T, size_t N>                                       \
    [[nodiscard]] OC_DEVICE_FLAG constexpr auto                          \
    operator op(ocarina::Vector<T, N> lhs, T rhs) noexcept {             \
        return lhs op ocarina::Vector<T, N>{rhs};                        \
    }                                                                    \
    template<typename T, size_t N>                                       \
    [[nodiscard]] OC_DEVICE_FLAG constexpr auto                          \
    operator op(T lhs, ocarina::Vector<T, N> rhs) noexcept {             \
        return ocarina::Vector<T, N>{lhs} op rhs;                        \
    }
OC_MAKE_VECTOR_LOGIC_OPERATOR(||, ocarina::is_all_boolean_v<T>)
OC_MAKE_VECTOR_LOGIC_OPERATOR(&&, ocarina::is_all_boolean_v<T>)
OC_MAKE_VECTOR_LOGIC_OPERATOR(==, ocarina::is_all_number_v<T>)
OC_MAKE_VECTOR_LOGIC_OPERATOR(!=, ocarina::is_all_number_v<T>)
OC_MAKE_VECTOR_LOGIC_OPERATOR(<, ocarina::is_all_number_v<T>)
OC_MAKE_VECTOR_LOGIC_OPERATOR(>, ocarina::is_all_number_v<T>)
OC_MAKE_VECTOR_LOGIC_OPERATOR(<=, ocarina::is_all_number_v<T>)
OC_MAKE_VECTOR_LOGIC_OPERATOR(>=, ocarina::is_all_number_v<T>)

#undef OC_MAKE_VECTOR_LOGIC_OPERATOR

#define OC_MAKE_TYPE_N(type)                                                                                                 \
    [[nodiscard]] constexpr auto make_##type##2(type s = {}) noexcept { return type##2(s); }                                 \
    [[nodiscard]] constexpr auto make_##type##2(type x, type y) noexcept { return type##2(x, y); }                           \
    template<typename T, size_t N, ocarina::enable_if_t<N >= 2, int> = 0>                                                    \
    [[nodiscard]] constexpr auto make_##type##2(Vector<T, N> v) noexcept {                                                   \
        return type##2(                                                                                                      \
            static_cast<type>(v.x),                                                                                          \
            static_cast<type>(v.y));                                                                                         \
    }                                                                                                                        \
    template<typename T, size_t N, ocarina::enable_if_t<N >= 2, int> = 0>                                                    \
    [[nodiscard]] constexpr auto make_##type##2(ocarina::array<T, N> v) noexcept {                                           \
        return type##2(                                                                                                      \
            static_cast<type>(v[0]),                                                                                         \
            static_cast<type>(v[1]));                                                                                        \
    }                                                                                                                        \
                                                                                                                             \
    [[nodiscard]] constexpr auto make_##type##3(type s = {}) noexcept { return type##3(s); }                                 \
    [[nodiscard]] constexpr auto make_##type##3(type x, type y, type z) noexcept { return type##3(x, y, z); }                \
    template<typename T, size_t N, ocarina::enable_if_t<N >= 3, int> = 0>                                                    \
    [[nodiscard]] constexpr auto make_##type##3(Vector<T, N> v) noexcept {                                                   \
        return type##3(                                                                                                      \
            static_cast<type>(v.x),                                                                                          \
            static_cast<type>(v.y),                                                                                          \
            static_cast<type>(v.z));                                                                                         \
    }                                                                                                                        \
    template<typename T, size_t N, ocarina::enable_if_t<N >= 3, int> = 0>                                                    \
    [[nodiscard]] constexpr auto make_##type##3(ocarina::array<T, N> v) noexcept {                                           \
        return type##3(                                                                                                      \
            static_cast<type>(v[0]),                                                                                         \
            static_cast<type>(v[1]),                                                                                         \
            static_cast<type>(v[2]));                                                                                        \
    }                                                                                                                        \
    [[nodiscard]] constexpr auto make_##type##3(type##2 v, type z) noexcept { return type##3(v.x, v.y, z); }                 \
    [[nodiscard]] constexpr auto make_##type##3(type x, type##2 v) noexcept { return type##3(x, v.x, v.y); }                 \
    [[nodiscard]] constexpr auto make_##type##3(type##4 v) noexcept { return type##3(v.x, v.y, v.z); }                       \
                                                                                                                             \
    [[nodiscard]] constexpr auto make_##type##4(type s = {}) noexcept { return type##4(s); }                                 \
    [[nodiscard]] constexpr auto make_##type##4(type x, type y, type z, type w) noexcept { return type##4(x, y, z, w); }     \
    template<typename T, size_t N, ocarina::enable_if_t<N == 4, int> = 0>                                                    \
    [[nodiscard]] constexpr auto make_##type##4(Vector<T, N> v) noexcept {                                                   \
        return type##4(                                                                                                      \
            static_cast<type>(v.x),                                                                                          \
            static_cast<type>(v.y),                                                                                          \
            static_cast<type>(v.z),                                                                                          \
            static_cast<type>(v.w));                                                                                         \
    }                                                                                                                        \
    template<typename T, size_t N, ocarina::enable_if_t<N == 4, int> = 0>                                                    \
    [[nodiscard]] constexpr auto make_##type##4(ocarina::array<T, N> v) noexcept {                                           \
        return type##4(                                                                                                      \
            static_cast<type>(v[0]),                                                                                         \
            static_cast<type>(v[1]),                                                                                         \
            static_cast<type>(v[2]),                                                                                         \
            static_cast<type>(v[3]));                                                                                        \
    }                                                                                                                        \
    [[nodiscard]] constexpr auto make_##type##4(type##2 v, type z, type w) noexcept { return type##4(v.x, v.y, z, w); }      \
    [[nodiscard]] constexpr auto make_##type##4(type x, type##2 v, type w) noexcept { return type##4(x, v.x, v.y, w); }      \
    [[nodiscard]] constexpr auto make_##type##4(type x, type y, type##2 v) noexcept { return type##4(x, y, v.x, v.y); }      \
    [[nodiscard]] constexpr auto make_##type##4(type##2 xy, type##2 zw) noexcept { return type##4(xy.x, xy.y, zw.x, zw.y); } \
    [[nodiscard]] constexpr auto make_##type##4(type##3 v, type w) noexcept { return type##4(v.x, v.y, v.z, w); }            \
    [[nodiscard]] constexpr auto make_##type##4(type x, type##3 v) noexcept { return type##4(x, v.x, v.y, v.z); }

namespace ocarina {
OC_MAKE_TYPE_N(bool)
OC_MAKE_TYPE_N(float)
OC_MAKE_TYPE_N(half)
OC_MAKE_TYPE_N(real)
OC_MAKE_TYPE_N(int)
OC_MAKE_TYPE_N(uint)
OC_MAKE_TYPE_N(uchar)
OC_MAKE_TYPE_N(short)
OC_MAKE_TYPE_N(ushort)
OC_MAKE_TYPE_N(ulong)
OC_MAKE_TYPE_N(char)
}// namespace ocarina
#undef OC_MAKE_TYPE_N

#define OC_MAKE_VECTOR_MAKER(type, N) \
    template<typename... Args>        \
    [[nodiscard]] constexpr auto oc_make_##type##N(Args... args) noexcept { return ocarina::make_##type##N(args...); }

#define OC_MAKE_VECTOR_MAKER_FOR_TYPE(type) \
    OC_MAKE_VECTOR_MAKER(type, 2)           \
    OC_MAKE_VECTOR_MAKER(type, 3)           \
    OC_MAKE_VECTOR_MAKER(type, 4)

OC_MAKE_VECTOR_MAKER_FOR_TYPE(bool)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(float)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(half)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(real)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(int)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(uint)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(uchar)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(short)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(ushort)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(ulong)
OC_MAKE_VECTOR_MAKER_FOR_TYPE(char)

#undef OC_MAKE_VECTOR_MAKER
#undef OC_MAKE_VECTOR_MAKER_FOR_TYPE
