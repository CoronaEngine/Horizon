
#pragma once

#include "cuda_device_matrix.h"

#define OC_MATRIX_UNARY_FUNC(func)                                                                                        \
    template<typename T, size_t N, size_t M, size_t... i>                                                                 \
    [[nodiscard]] ocarina::Matrix<T, N, M> func##_impl(ocarina::Matrix<T, N, M> m, ocarina::index_sequence<i...>) noexcept { \
        return ocarina::Matrix<T, N, M>(oc_##func(m[i])...);                                                              \
    }                                                                                                                     \
    template<typename T, size_t N, size_t M>                                                                              \
    [[nodiscard]] ocarina::Matrix<T, N, M> oc_##func(ocarina::Matrix<T, N, M> m) noexcept {                               \
        return func##_impl(m, ocarina::make_index_sequence<N>());                                                         \
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

#define OC_MATRIX_BINARY_FUNC(func)                                                              \
    template<typename T, size_t N, size_t M, size_t... i>                                        \
    [[nodiscard]] ocarina::Matrix<T, N, M> func##_impl(ocarina::Matrix<T, N, M> lhs,             \
                                                       ocarina::Matrix<T, N, M> rhs,             \
                                                       ocarina::index_sequence<i...>) noexcept { \
        return ocarina::Matrix<T, N, M>(oc_##func(lhs[i], rhs[i])...);                           \
    }                                                                                            \
    template<typename T, size_t N, size_t M>                                                     \
    [[nodiscard]] ocarina::Matrix<T, N, M> oc_##func(ocarina::Matrix<T, N, M> lhs,               \
                                                     ocarina::Matrix<T, N, M> rhs) noexcept {    \
        return func##_impl(lhs, rhs, ocarina::make_index_sequence<N>());                         \
    }

OC_MATRIX_BINARY_FUNC(max)
OC_MATRIX_BINARY_FUNC(min)
OC_MATRIX_BINARY_FUNC(pow)
OC_MATRIX_BINARY_FUNC(atan2)

#undef OC_MATRIX_BINARY_FUNC