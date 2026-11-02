
#pragma once

#include "cuda_vector_func.h"

namespace ocarina {

template<typename T, size_t N, size_t M>
struct Matrix {
public:
    static constexpr auto RowNum = N;
    static constexpr auto ColNum = M;
    static constexpr auto ElementNum = M * N;
    using scalar_type = T;
    using vector_type = Vector<scalar_type, N>;
    using array_t = oc_array<vector_type, M>;

private:
    array_t cols_{};

public:
    template<size_t... i>
    [[nodiscard]] static constexpr array_t diagonal_helper(index_sequence<i...>, scalar_type s) noexcept {
        array_t ret{};
        if constexpr (M == N) {
            ((ret[i][i] = s), ...);
        }
        return ret;
    }
    constexpr Matrix(scalar_type s = 1) noexcept
        : cols_(diagonal_helper(make_index_sequence<N>(), s)) {
    }

    template<typename... Args, enable_if_t<sizeof...(Args) == M, int> = 0>
    constexpr Matrix(Args... args) noexcept : cols_(array_t{args...}) {}

    template<typename TT, size_t NN, size_t MM, size_t... i>
    [[nodiscard]] static constexpr auto construct_helper(Matrix<TT, NN, MM> mat, index_sequence<i...>) {
        return oc_array<Vector<T, N>, M>{Vector<T, N>{mat[i]}...};
    }

    template<typename TT, size_t NN, size_t MM, enable_if_t<(NN >= N && MM >= M), int> = 0>
    explicit constexpr Matrix(Matrix<TT, NN, MM> mat) noexcept
        : cols_{construct_helper(mat, make_index_sequence<M>())} {}

    template<size_t... i>
    [[nodiscard]] static constexpr array_t construct_helper(ocarina::index_sequence<i...>,
                                                            const oc_array<scalar_type, ElementNum> &arr) {
        return array_t{vector_type{&(arr.data()[i * M])}...};
    }

    template<typename... Args, enable_if_t<sizeof...(Args) == ElementNum, int> = 0>
    explicit constexpr Matrix(Args &&...args) noexcept
        : cols_(construct_helper(ocarina::make_index_sequence<N>(),
                                 oc_array<scalar_type, ElementNum>{oc_static_cast<scalar_type>(args)...})) {}

    [[nodiscard]] constexpr vector_type &operator[](size_t i) noexcept { return cols_[i]; }
    [[nodiscard]] constexpr const vector_type &operator[](size_t i) const noexcept { return cols_[i]; }
};

template<typename T, size_t N, size_t M, typename... Args>
[[nodiscard]] constexpr Matrix<T, N, M> make_matrix(Args &&...args) noexcept {
    return Matrix<T, N, M>(args...);
}

#define OC_MAKE_MATRIX(type, N, M)                                                      \
    using type##N##x##M = Matrix<type, N, M>;                                           \
    template<typename... Args>                                                          \
    [[nodiscard]] constexpr type##N##x##M make_##type##N##x##M(Args &&...args) {        \
        return make_matrix<type, N, M>(args...);                                        \
    }                                                                                   \
    template<typename T, size_t NN, size_t MM>                                          \
    [[nodiscard]] constexpr type##N##x##M make_##type##N##x##M(Matrix<T, NN, MM> mat) { \
        return type##N##x##M(mat);                                                      \
    }

#define OC_MAKE_MATRICES_FOR_TYPE(type) \
    OC_MAKE_MATRIX(type, 2, 2)          \
    OC_MAKE_MATRIX(type, 2, 3)          \
    OC_MAKE_MATRIX(type, 2, 4)          \
    OC_MAKE_MATRIX(type, 3, 2)          \
    OC_MAKE_MATRIX(type, 3, 3)          \
    OC_MAKE_MATRIX(type, 3, 4)          \
    OC_MAKE_MATRIX(type, 4, 2)          \
    OC_MAKE_MATRIX(type, 4, 3)          \
    OC_MAKE_MATRIX(type, 4, 4)

OC_MAKE_MATRICES_FOR_TYPE(float)
OC_MAKE_MATRICES_FOR_TYPE(half)
OC_MAKE_MATRICES_FOR_TYPE(real)

#undef OC_MAKE_MATRICES_FOR_TYPE
#undef OC_MAKE_MATRIX

#define OC_MAKE_MATRIX_CONVERTERS(type)                                                    \
    [[nodiscard]] constexpr auto make_##type##3x3(type##2x2 m) noexcept {                  \
        return type##3x3 {                                                                 \
            type##3(m[0].x, m[0].y, (type)0),                                              \
            type##3(m[1].x, m[1].y, (type)0),                                              \
            type##3((type)0, (type)0, (type)1)};                                           \
    }                                                                                      \
                                                                                           \
    [[nodiscard]] constexpr auto make_##type##4x4(type##2x2 m) noexcept {                  \
        return type##4x4 {                                                                 \
            type##4(m[0].x, m[0].y, (type)0, (type)0),                                     \
            type##4(m[1].x, m[1].y, (type)0, (type)0),                                     \
            type##4 {(type)0, (type)0, (type)1, (type)0},                                  \
            type##4 {(type)0, (type)0, (type)0, (type)1}};                                 \
    }                                                                                      \
                                                                                           \
    [[nodiscard]] constexpr auto make_##type##4x4(type##4x3 m) noexcept {                  \
        return type##4x4 {m[0], m[1], m[2], type##4 {(type)0, (type)0, (type)0, (type)1}}; \
    }                                                                                      \
                                                                                           \
    [[nodiscard]] constexpr auto make_##type##4x4(type##3x4 m) noexcept {                  \
        return type##4x4 {                                                                 \
            type##4(m[0].x, m[0].y, m[0].z, (type)0),                                      \
            type##4(m[1].x, m[1].y, m[1].z, (type)0),                                      \
            type##4(m[2].x, m[2].y, m[2].z, (type)0),                                      \
            type##4 {m[3].x, m[3].y, m[3].z, (type)1}};                                    \
    }                                                                                      \
                                                                                           \
    [[nodiscard]] constexpr auto make_##type##4x4(type##3x3 m) noexcept {                  \
        return type##4x4 {                                                                 \
            type##4(m[0].x, m[0].y, m[0].z, (type)0),                                      \
            type##4(m[1].x, m[1].y, m[1].z, (type)0),                                      \
            type##4(m[2].x, m[2].y, m[2].z, (type)0),                                      \
            type##4 {(type)0, (type)0, (type)0, (type)1}};                                 \
    }

OC_MAKE_MATRIX_CONVERTERS(float)
OC_MAKE_MATRIX_CONVERTERS(half)
OC_MAKE_MATRIX_CONVERTERS(real)

#undef OC_MAKE_MATRIX_CONVERTERS

template<typename T, size_t N, size_t M, size_t... m>
constexpr Vector<T, N> transpose_helper_m(const Matrix<T, M, N> &mat, size_t i, index_sequence<m...>) {
    return Vector<T, N>((mat[m][i])...);
}

template<typename T, size_t N, size_t M, size_t... n>
constexpr Matrix<T, N, M> transpose_helper_n(const Matrix<T, M, N> &mat, index_sequence<n...>) {
    return Matrix<T, N, M>(transpose_helper_m(mat, n, make_index_sequence<N>())...);
}

template<typename T, size_t N, size_t M>
[[nodiscard]] constexpr Matrix<T, N, M> transpose(const Matrix<T, M, N> &mat) noexcept {
    return transpose_helper_n(mat, make_index_sequence<M>());
}

}// namespace ocarina

namespace ocarina {
template<typename T, size_t N, size_t M, size_t... i>
OC_DEVICE_FLAG constexpr auto negate_matrix_impl(const ocarina::Matrix<T, N, M> &m, ocarina::index_sequence<i...>) {
    return ocarina::Matrix<T, N, M>{(-m[i])...};
}
}// namespace ocarina

template<typename T, size_t N, size_t M>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto operator-(ocarina::Matrix<T, N, M> m) {
    return ocarina::negate_matrix_impl(m, ocarina::make_index_sequence<N>());
}

namespace ocarina {
template<typename T, typename S, size_t N, size_t M, size_t... i>
OC_DEVICE_FLAG constexpr auto multiply_impl(const ocarina::Matrix<T, N, M> &m, S s, ocarina::index_sequence<i...>) {
    using scalar_type = decltype(T{} * S{});
    return ocarina::Matrix<scalar_type, N, M>((m[i] * s)...);
}
}// namespace ocarina

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto operator*(ocarina::Matrix<T, N, M> m, S s) {
    return ocarina::multiply_impl(m, s, ocarina::make_index_sequence<N>());
}

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto operator*(S s, ocarina::Matrix<T, N, M> m) {
    return m * s;
}

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto operator/(ocarina::Matrix<T, N, M> m, S s) {
    return m * S(1.0f / s);
}

namespace ocarina {
template<typename T, typename S, size_t N, size_t M, size_t... i>
OC_DEVICE_FLAG constexpr auto multiply_impl(const ocarina::Matrix<T, N, M> &m, const ocarina::Vector<S, M> &v,
                                        ocarina::index_sequence<i...>) noexcept {
    return ((v[i] * m[i]) + ...);
}
}// namespace ocarina

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto operator*(ocarina::Matrix<T, N, M> m, ocarina::Vector<S, M> v) noexcept {
    return ocarina::multiply_impl(m, v, ocarina::make_index_sequence<M>());
}

namespace ocarina {
template<typename T, typename S, size_t N, size_t M, size_t Dim, size_t... i>
OC_DEVICE_FLAG constexpr auto multiply_matrices_impl(const ocarina::Matrix<T, N, Dim> &lhs, const ocarina::Matrix<S, Dim, M> &rhs,
                                                 ocarina::index_sequence<i...>) noexcept {
    using scalar_type = decltype(T{} * S{});
    return ocarina::Matrix<scalar_type, N, M>{(lhs * rhs[i])...};
}
}// namespace ocarina

template<typename T, typename S, size_t N, size_t M, size_t Dim>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto operator*(ocarina::Matrix<T, N, Dim> lhs, ocarina::Matrix<S, Dim, M> rhs) noexcept {
    return ocarina::multiply_matrices_impl(lhs, rhs, ocarina::make_index_sequence<M>());
}

namespace ocarina {
template<typename T, typename S, size_t N, size_t M, size_t... i>
OC_DEVICE_FLAG constexpr auto add_matrices_impl(const ocarina::Matrix<T, N, M> &lhs, const ocarina::Matrix<S, N, M> &rhs,
                                            ocarina::index_sequence<i...>) noexcept {
    using scalar_type = decltype(T{} + S{});
    return ocarina::Matrix<scalar_type, N, M>{(lhs[i] + rhs[i])...};
}
}// namespace ocarina

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto operator+(ocarina::Matrix<T, N, M> lhs, ocarina::Matrix<S, N, M> rhs) noexcept {
    return ocarina::add_matrices_impl(lhs, rhs, ocarina::make_index_sequence<N>());
}

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] OC_DEVICE_FLAG constexpr auto operator-(ocarina::Matrix<T, N, M> lhs, ocarina::Matrix<S, N, M> rhs) noexcept {
    return lhs + (-rhs);
}

#define OC_MAKE_MATRIX(type, N, M)                                                    \
    using oc_##type##N##x##M = ocarina::Matrix<type, N, M>;                           \
    template<typename... Args>                                                        \
    [[nodiscard]] OC_DEVICE_FLAG constexpr auto oc_make_##type##N##x##M(Args &&...args) { \
        return ocarina::make_##type##N##x##M(args...);                                \
    }
#define OC_MAKE_MATRICES_FOR_TYPE(type) \
    OC_MAKE_MATRIX(type, 2, 2)          \
    OC_MAKE_MATRIX(type, 2, 3)          \
    OC_MAKE_MATRIX(type, 2, 4)          \
    OC_MAKE_MATRIX(type, 3, 2)          \
    OC_MAKE_MATRIX(type, 3, 3)          \
    OC_MAKE_MATRIX(type, 3, 4)          \
    OC_MAKE_MATRIX(type, 4, 2)          \
    OC_MAKE_MATRIX(type, 4, 3)          \
    OC_MAKE_MATRIX(type, 4, 4)

OC_MAKE_MATRICES_FOR_TYPE(float)
OC_MAKE_MATRICES_FOR_TYPE(half)
OC_MAKE_MATRICES_FOR_TYPE(real)

template<typename T, size_t N, size_t M>
[[nodiscard]] constexpr ocarina::Matrix<T, M, N> oc_transpose(const ocarina::Matrix<T, N, M> &mat) noexcept {
    return ocarina::transpose(mat);
}

#undef OC_MAKE_MATRICES_FOR_TYPE
#undef OC_MAKE_MATRIX
