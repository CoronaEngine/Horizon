//
// Created by Zero on 2024/5/20.
//

#pragma once

#include "vector_types.h"

namespace ocarina {

template<typename T, size_t N, size_t M>
struct Matrix {
public:
    static constexpr auto row_num = N;
    static constexpr auto col_num = M;
    static constexpr auto element_num = M * N;
    using scalar_type = T;
    using this_type = Matrix<T, N, M>;
    using vector_type = Vector<scalar_type, N>;
    using array_t = array<vector_type, M>;

private:
    array_t cols_{};

public:
    template<typename... Args>
    requires(sizeof...(Args) == M)
    explicit constexpr Matrix(Args &&...args) noexcept
        : cols_(array_t{static_cast<vector_type>(OC_FORWARD(args))...}) {}

    template<typename... Args>
    requires(sizeof...(Args) == element_num && is_all_scalar_v<Args...>)
    explicit constexpr Matrix(Args &&...args) noexcept
        : cols_([&]<size_t... i>(std::index_sequence<i...>,
                                 const array<scalar_type, element_num> &arr) {
              return array_t{vector_type{addressof(arr.data()[i * N])}...};
          }(std::make_index_sequence<M>(),
                array<scalar_type, element_num>{static_cast<scalar_type>(OC_FORWARD(args))...})) {
    }

    template<typename TT, size_t NN, size_t MM>
    requires(NN >= N && MM >= M)
    explicit constexpr Matrix(Matrix<TT, NN, MM> mat) noexcept
        : cols_{[&]<size_t... i>(std::index_sequence<i...>) {
              return std::array<Vector<T, N>, M>{Vector<T, N>{mat[i]}...};
          }(std::make_index_sequence<M>())} {}

    constexpr Matrix(scalar_type s = 1) noexcept
        : cols_([&]<size_t... i>(std::index_sequence<i...>) {
              array_t ret{};
              if constexpr (M == N) {
                  ((ret[i][i] = s), ...);
              }
              return ret;
          }(std::make_index_sequence<N>())) {
    }

    [[nodiscard]] constexpr vector_type &operator[](size_t i) noexcept { return cols_[i]; }
    [[nodiscard]] constexpr const vector_type &operator[](size_t i) const noexcept { return cols_[i]; }
    [[nodiscard]] constexpr vector_type &at(size_t i) noexcept { return cols_[i]; }
    [[nodiscard]] constexpr const vector_type &at(size_t i) const noexcept { return cols_[i]; }
};

#define OC_MATRIX_UNARY_FUNC(func)                                   \
    template<typename T, size_t N, size_t M>                         \
    [[nodiscard]] Matrix<T, N, M> func(Matrix<T, N, M> m) noexcept { \
        return [&]<size_t... i>(std::index_sequence<i...>) {         \
            return ocarina::Matrix<T, N, M>(func(m[i])...);          \
        }(std::make_index_sequence<M>());                            \
    }

OC_MATRIX_UNARY_FUNC(rcp)
OC_MATRIX_UNARY_FUNC(abs)
OC_MATRIX_UNARY_FUNC(sqrt)
OC_MATRIX_UNARY_FUNC(sqr)
OC_MATRIX_UNARY_FUNC(sign)
OC_MATRIX_UNARY_FUNC(cos)
OC_MATRIX_UNARY_FUNC(sin)
OC_MATRIX_UNARY_FUNC(tan)
OC_MATRIX_UNARY_FUNC(cosh)
OC_MATRIX_UNARY_FUNC(sinh)
OC_MATRIX_UNARY_FUNC(tanh)
OC_MATRIX_UNARY_FUNC(log)
OC_MATRIX_UNARY_FUNC(log2)
OC_MATRIX_UNARY_FUNC(log10)
OC_MATRIX_UNARY_FUNC(exp)
OC_MATRIX_UNARY_FUNC(exp2)
OC_MATRIX_UNARY_FUNC(asin)
OC_MATRIX_UNARY_FUNC(acos)
OC_MATRIX_UNARY_FUNC(atan)
OC_MATRIX_UNARY_FUNC(asinh)
OC_MATRIX_UNARY_FUNC(acosh)
OC_MATRIX_UNARY_FUNC(atanh)
OC_MATRIX_UNARY_FUNC(floor)
OC_MATRIX_UNARY_FUNC(ceil)
OC_MATRIX_UNARY_FUNC(degrees)
OC_MATRIX_UNARY_FUNC(radians)
OC_MATRIX_UNARY_FUNC(round)
OC_MATRIX_UNARY_FUNC(isnan)
OC_MATRIX_UNARY_FUNC(isinf)
OC_MATRIX_UNARY_FUNC(fract)
OC_MATRIX_UNARY_FUNC(copysign)

#undef OC_MATRIX_UNARY_FUNC

#define OC_MATRIX_BINARY_FUNC(func)                                    \
    template<typename T, size_t N, size_t M>                           \
    [[nodiscard]] Matrix<T, N, M> func(Matrix<T, N, M> lhs,            \
                                       Matrix<T, N, M> rhs) noexcept { \
        return [&]<size_t... i>(std::index_sequence<i...>) {           \
            return ocarina::Matrix<T, N, M>(func(lhs[i], rhs[i])...);  \
        }(std::make_index_sequence<N>());                              \
    }

OC_MATRIX_BINARY_FUNC(max)
OC_MATRIX_BINARY_FUNC(min)
OC_MATRIX_BINARY_FUNC(pow)
OC_MATRIX_BINARY_FUNC(atan2)

#undef OC_MATRIX_BINARY_FUNC

#define OC_MATRIX_TRIPLE_FUNC(func)                                          \
    template<typename T, size_t N, size_t M>                                 \
    [[nodiscard]] Matrix<T, N, M> func(Matrix<T, N, M> t, Matrix<T, N, M> u, \
                                       Matrix<T, N, M> v) noexcept {         \
        return [&]<size_t... i>(std::index_sequence<i...>) {                 \
            return ocarina::Matrix<T, N, M>(func(t[i], u[i], v[i])...);      \
        }(std::make_index_sequence<N>());                                    \
    }

OC_MATRIX_TRIPLE_FUNC(fma)
OC_MATRIX_TRIPLE_FUNC(clamp)
OC_MATRIX_TRIPLE_FUNC(lerp)

#undef OC_MATRIX_TRIPLE_FUNC

}// namespace ocarina

template<typename T, size_t N, size_t M>
[[nodiscard]] constexpr auto operator-(ocarina::Matrix<T, N, M> m) {
    return [&]<size_t... i>(std::index_sequence<i...>) {
        return ocarina::Matrix<T, N, M>((-m[i])...);
    }(std::make_index_sequence<M>());
}

template<typename T, typename S, size_t N, size_t M>
requires ocarina::is_scalar_v<S>
[[nodiscard]] constexpr auto operator*(ocarina::Matrix<T, N, M> m, S s) {
    using scalar_type = decltype(T{} * s);
    return [&]<size_t... i>(std::index_sequence<i...>) {
        return ocarina::Matrix<scalar_type, N, M>((m[i] * s)...);
    }(std::make_index_sequence<M>());
}

template<typename S, typename T, size_t N, size_t M>
requires ocarina::is_scalar_v<S>
[[nodiscard]] constexpr auto operator*(S s, ocarina::Matrix<T, N, M> m) {
    return m * s;
}

template<typename T, typename S, size_t N, size_t M>
requires ocarina::is_scalar_v<S>
[[nodiscard]] constexpr auto operator/(ocarina::Matrix<T, N, M> m, S s) {
    return m * S(1.0f / s);
}

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] constexpr auto operator*(ocarina::Matrix<T, N, M> m,
                                       ocarina::Vector<S, M> v) noexcept {
    return [&]<size_t... i>(std::index_sequence<i...>) {
        return ((v[i] * m[i]) + ...);
    }(std::make_index_sequence<M>());
}

template<typename T, typename S, size_t N, size_t M, size_t Dim>
[[nodiscard]] constexpr auto operator*(ocarina::Matrix<T, N, Dim> lhs,
                                       ocarina::Matrix<S, Dim, M> rhs) noexcept {
    using scalar_type = decltype(T{} * S{});
    return [&]<size_t... i>(std::index_sequence<i...>) {
        return ocarina::Matrix<scalar_type, N, M>((lhs * rhs[i])...);
    }(std::make_index_sequence<M>());
}

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] constexpr auto operator+(ocarina::Matrix<T, N, M> lhs,
                                       ocarina::Matrix<S, N, M> rhs) noexcept {
    using scalar_type = decltype(T{} + S{});
    return [&]<size_t... i>(std::index_sequence<i...>) {
        return ocarina::Matrix<scalar_type, N, M>(lhs[i] + rhs[i]...);
    }(std::make_index_sequence<M>());
}

template<typename T, typename S, size_t N, size_t M>
[[nodiscard]] constexpr auto operator-(ocarina::Matrix<T, N, M> lhs,
                                       ocarina::Matrix<S, N, M> rhs) noexcept {
    return lhs + (-rhs);
}

namespace ocarina {

template<typename T, size_t N, size_t M, typename... Args>
requires is_all_basic_v<Args...>
[[nodiscard]] constexpr Matrix<T, N, M> make_matrix(Args &&...args) noexcept {
    return Matrix<T, N, M>(OC_FORWARD(args)...);
}

#define OC_MAKE_MATRIX_(type, N, M)                                                     \
    using type##N##x##M = Matrix<type, N, M>;                                           \
    template<typename... Args>                                                          \
    requires is_all_basic_v<Args...>                                                    \
    [[nodiscard]] constexpr type##N##x##M make_##type##N##x##M(Args &&...args) {        \
        return make_matrix<type, N, M>(OC_FORWARD(args)...);                            \
    }                                                                                   \
    template<typename T, size_t NN, size_t MM>                                          \
    [[nodiscard]] constexpr type##N##x##M make_##type##N##x##M(Matrix<T, NN, MM> mat) { \
        return type##N##x##M(mat);                                                      \
    }

#define OC_MAKE_MATRICES_FOR_TYPE(type) \
    OC_MAKE_MATRIX_(type, 2, 2)         \
    OC_MAKE_MATRIX_(type, 2, 3)         \
    OC_MAKE_MATRIX_(type, 2, 4)         \
    OC_MAKE_MATRIX_(type, 3, 2)         \
    OC_MAKE_MATRIX_(type, 3, 3)         \
    OC_MAKE_MATRIX_(type, 3, 4)         \
    OC_MAKE_MATRIX_(type, 4, 2)         \
    OC_MAKE_MATRIX_(type, 4, 3)         \
    OC_MAKE_MATRIX_(type, 4, 4)

OC_MAKE_MATRICES_FOR_TYPE(float)
OC_MAKE_MATRICES_FOR_TYPE(real)
OC_MAKE_MATRICES_FOR_TYPE(half)

#undef OC_MAKE_MATRICES_FOR_TYPE
#undef OC_MAKE_MATRIX_

template<typename T, size_t M, size_t N>
[[nodiscard]] constexpr Matrix<T, N, M> transpose(const Matrix<T, M, N> &mat) noexcept {
    Matrix<T, N, M> ret = make_matrix<T, N, M>();
    auto func_m = [&]<size_t... m>(size_t i, std::index_sequence<m...>) {
        return Vector<T, N>((mat[m][i])...);
    };
    auto func_n = [&]<size_t... n>(std::index_sequence<n...>) {
        return Matrix<T, N, M>(func_m(n, std::make_index_sequence<N>())...);
    };
    return func_n(std::make_index_sequence<M>());
}

namespace detail {

template<typename T, size_t N>
constexpr void swap_matrix_rows(Matrix<T, N, N> &mat, size_t lhs, size_t rhs) noexcept {
    if (lhs == rhs) {
        return;
    }
    for (size_t col = 0; col < N; ++col) {
        auto temp = mat[col][lhs];
        mat[col][lhs] = mat[col][rhs];
        mat[col][rhs] = temp;
    }
}

}// namespace detail

template<typename T, size_t N>
requires is_floating_point_v<T>
[[nodiscard]] constexpr Matrix<T, N, N> inverse(Matrix<T, N, N> mat) noexcept {
    Matrix<T, N, N> inv{static_cast<T>(1)};
    for (size_t pivot_col = 0; pivot_col < N; ++pivot_col) {
        size_t pivot_row = pivot_col;
        auto pivot_abs = abs(mat[pivot_col][pivot_row]);
        for (size_t row = pivot_col + 1; row < N; ++row) {
            auto candidate_abs = abs(mat[pivot_col][row]);
            if (candidate_abs > pivot_abs) {
                pivot_abs = candidate_abs;
                pivot_row = row;
            }
        }

        detail::swap_matrix_rows(mat, pivot_col, pivot_row);
        detail::swap_matrix_rows(inv, pivot_col, pivot_row);

        auto pivot = mat[pivot_col][pivot_col];
        auto inv_pivot = static_cast<T>(1) / pivot;
        for (size_t col = 0; col < N; ++col) {
            mat[col][pivot_col] *= inv_pivot;
            inv[col][pivot_col] *= inv_pivot;
        }

        for (size_t row = 0; row < N; ++row) {
            if (row == pivot_col) {
                continue;
            }
            auto factor = mat[pivot_col][row];
            if (factor == static_cast<T>(0)) {
                continue;
            }
            for (size_t col = 0; col < N; ++col) {
                mat[col][row] -= factor * mat[col][pivot_col];
                inv[col][row] -= factor * inv[col][pivot_col];
            }
        }
    }
    return inv;
}

#define OC_MAKE_MATRIX_CONVERTERS(type)                                                    \
    [[nodiscard]] constexpr auto make_##type##3x3(type##2x2 m) noexcept {                  \
        return type##3x3 {                                                                 \
            make_##type##3(m[0], (type)0),                                                 \
            make_##type##3(m[1], (type)0),                                                 \
            make_##type##3((type)0, (type)0, (type)1)};                                    \
    }                                                                                      \
    [[nodiscard]] constexpr auto make_##type##4x4(type##2x2 m) noexcept {                  \
        return type##4x4 {                                                                 \
            make_##type##4(m[0], (type)0, (type)0),                                        \
            make_##type##4(m[1], (type)0, (type)0),                                        \
            type##4 {(type)0, (type)0, (type)1, (type)0},                                  \
            type##4 {(type)0, (type)0, (type)0, (type)1}};                                 \
    }                                                                                      \
    [[nodiscard]] constexpr auto make_##type##4x4(type##4x3 m) noexcept {                  \
        return type##4x4 {m[0], m[1], m[2], type##4 {(type)0, (type)0, (type)0, (type)1}}; \
    }                                                                                      \
    [[nodiscard]] constexpr auto make_##type##4x4(type##3x4 m) noexcept {                  \
        return type##4x4 {                                                                 \
            make_##type##4(m[0], (type)0),                                                 \
            make_##type##4(m[1], (type)0),                                                 \
            make_##type##4(m[2], (type)0),                                                 \
            make_##type##4(m[3], (type)1)};                                                \
    }                                                                                      \
    [[nodiscard]] constexpr auto make_##type##4x4(type##3x3 m) noexcept {                  \
        return type##4x4 {                                                                 \
            make_##type##4(m[0], (type)0),                                                 \
            make_##type##4(m[1], (type)0),                                                 \
            make_##type##4(m[2], (type)0),                                                 \
            type##4 {(type)0, (type)0, (type)0, (type)1}};                                 \
    }

OC_MAKE_MATRIX_CONVERTERS(float)
OC_MAKE_MATRIX_CONVERTERS(half)

}// namespace ocarina