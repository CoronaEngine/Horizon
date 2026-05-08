#include "math/base.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ocarina;

namespace {

[[noreturn]] void fail(const std::string &message) {
    std::cerr << "[test-matrix-inverse] " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const std::string &message) {
    if (!condition) {
        fail(message);
    }
}

template<typename T>
bool nearly_equal(T lhs, T rhs, float eps = 1e-4f) {
    return std::abs(static_cast<float>(lhs) - static_cast<float>(rhs)) <= eps;
}

template<typename T, size_t N>
Matrix<T, N, N> make_matrix_from_rows(const std::array<T, N * N> &values) {
    Matrix<T, N, N> matrix{static_cast<T>(0)};
    for (size_t row = 0; row < N; ++row) {
        for (size_t col = 0; col < N; ++col) {
            matrix[col][row] = values[row * N + col];
        }
    }
    return matrix;
}

template<typename T, size_t N>
void require_matrix_near(Matrix<T, N, N> actual,
                         Matrix<T, N, N> expected,
                         const std::string &message,
                         float eps = 1e-4f) {
    for (size_t row = 0; row < N; ++row) {
        for (size_t col = 0; col < N; ++col) {
            if (!nearly_equal(actual[col][row], expected[col][row], eps)) {
                fail(message + " at row=" + std::to_string(row) +
                     " col=" + std::to_string(col) +
                     " expected=" + std::to_string(static_cast<float>(expected[col][row])) +
                     " actual=" + std::to_string(static_cast<float>(actual[col][row])));
            }
        }
    }
}

template<typename T, size_t N>
void require_identity(Matrix<T, N, N> matrix,
                      const std::string &message,
                      float eps = 1e-4f) {
    Matrix<T, N, N> identity{static_cast<T>(1)};
    require_matrix_near(matrix, identity, message, eps);
}

void test_generic_inverse_requires_row_pivoting() {
    auto matrix = make_matrix_from_rows<float, 5>({
        0.f, 2.f, 0.f, 0.f, 1.f,
        1.f, 3.f, 1.f, 0.f, 0.f,
        0.f, 1.f, 4.f, 1.f, 0.f,
        0.f, 0.f, 1.f, 5.f, 1.f,
        2.f, 0.f, 0.f, 1.f, 6.f,
    });

    auto inv = inverse<float, 5>(matrix);

    require_identity(matrix * inv, "5x5 generic inverse left product mismatch", 2e-4f);
    require_identity(inv * matrix, "5x5 generic inverse right product mismatch", 2e-4f);
}

void test_generic_inverse_for_six_by_six() {
    auto matrix = make_matrix_from_rows<float, 6>({
        0.f, 2.f, 0.f, 0.f, 1.f, 0.f,
        3.f, 9.f, 1.f, 0.f, 0.f, 1.f,
        0.f, 1.f, 8.f, 2.f, 0.f, 0.f,
        0.f, 0.f, 2.f, 7.f, 1.f, 0.f,
        1.f, 0.f, 0.f, 1.f, 6.f, 2.f,
        0.f, 1.f, 0.f, 0.f, 2.f, 5.f,
    });

    auto inv = inverse<float, 6>(matrix);

    require_identity(matrix * inv, "6x6 generic inverse left product mismatch", 4e-4f);
    require_identity(inv * matrix, "6x6 generic inverse right product mismatch", 4e-4f);
}

void test_generic_inverse_matches_specialized_four_by_four() {
    float4x4 matrix = make_matrix_from_rows<float, 4>({
        4.f, 1.f, 0.f, 2.f,
        1.f, 5.f, 1.f, 0.f,
        0.f, 1.f, 6.f, 1.f,
        2.f, 0.f, 1.f, 7.f,
    });

    auto generic_inv = inverse<float, 4>(matrix);
    auto specialized_inv = inverse(matrix);

    require_matrix_near(generic_inv, specialized_inv,
                        "generic 4x4 inverse does not match specialized implementation", 1e-4f);
    require_identity(matrix * generic_inv, "4x4 generic inverse left product mismatch", 1e-4f);
}

}// namespace

int main() {
    test_generic_inverse_requires_row_pivoting();
    test_generic_inverse_for_six_by_six();
    test_generic_inverse_matches_specialized_four_by_four();
    std::cout << "[test-matrix-inverse] all checks passed" << std::endl;
    return 0;
}