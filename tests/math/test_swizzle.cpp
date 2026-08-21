#include "math/basic_types.h"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template<typename T>
void expect_equal(T actual, T expected, const char *message) {
    expect(actual == expected, message);
}

void expect_near(float actual, float expected, const char *message) {
    expect(std::fabs(actual - expected) < 1e-5f, message);
}

void test_vector_arithmetic() {
    using horizon::math::float3;

    const float3 lhs{1.f, 2.f, 3.f};
    const float3 rhs{4.f, 5.f, 6.f};
    const float3 sum = lhs + rhs;
    const float3 difference = rhs - lhs;
    const float3 product = lhs * rhs;
    const float3 quotient = rhs / lhs;
    const float3 scaled = lhs * 2.f;

    expect_equal(sum[0], 5.f, "vector addition x");
    expect_equal(sum[1], 7.f, "vector addition y");
    expect_equal(sum[2], 9.f, "vector addition z");
    expect_equal(difference[0], 3.f, "vector subtraction x");
    expect_equal(difference[1], 3.f, "vector subtraction y");
    expect_equal(difference[2], 3.f, "vector subtraction z");
    expect_equal(product[0], 4.f, "vector multiplication x");
    expect_equal(product[1], 10.f, "vector multiplication y");
    expect_equal(product[2], 18.f, "vector multiplication z");
    expect_equal(quotient[0], 4.f, "vector division x");
    expect_equal(quotient[1], 2.5f, "vector division y");
    expect_equal(quotient[2], 2.f, "vector division z");
    expect_equal(scaled[0], 2.f, "vector scalar multiplication x");
    expect_equal(scaled[1], 4.f, "vector scalar multiplication y");
    expect_equal(scaled[2], 6.f, "vector scalar multiplication z");
}

void test_matrix_arithmetic() {
    using horizon::math::float2x2;

    const float2x2 lhs{1.f, 2.f, 3.f, 4.f};
    const float2x2 rhs{5.f, 6.f, 7.f, 8.f};
    const float2x2 sum = lhs + rhs;
    const float2x2 difference = rhs - lhs;
    const float2x2 scaled = lhs * 2.f;
    const float2x2 divided = rhs / 2.f;
    const float2x2 product = lhs * rhs;

    expect_equal(sum[0][0], 6.f, "matrix addition column 0 row 0");
    expect_equal(sum[0][1], 8.f, "matrix addition column 0 row 1");
    expect_equal(sum[1][0], 10.f, "matrix addition column 1 row 0");
    expect_equal(sum[1][1], 12.f, "matrix addition column 1 row 1");

    expect_equal(difference[0][0], 4.f, "matrix subtraction column 0 row 0");
    expect_equal(difference[0][1], 4.f, "matrix subtraction column 0 row 1");
    expect_equal(difference[1][0], 4.f, "matrix subtraction column 1 row 0");
    expect_equal(difference[1][1], 4.f, "matrix subtraction column 1 row 1");

    expect_equal(scaled[0][0], 2.f, "matrix scalar multiplication column 0 row 0");
    expect_equal(scaled[1][1], 8.f, "matrix scalar multiplication column 1 row 1");
    expect_equal(divided[0][0], 2.5f, "matrix scalar division column 0 row 0");
    expect_equal(divided[1][1], 4.f, "matrix scalar division column 1 row 1");

    expect_equal(product[0][0], 23.f, "matrix multiplication column 0 row 0");
    expect_equal(product[0][1], 34.f, "matrix multiplication column 0 row 1");
    expect_equal(product[1][0], 31.f, "matrix multiplication column 1 row 0");
    expect_equal(product[1][1], 46.f, "matrix multiplication column 1 row 1");
}

void test_matrix_inverse() {
    using horizon::math::float2x2;
    using horizon::math::float3x3;
    using horizon::math::float4x4;

    const float2x2 matrix{4.f, 7.f, 2.f, 6.f};
    const float2x2 inverse = horizon::math::inverse(matrix);
    const float2x2 identity = matrix * inverse;

    expect_near(identity[0][0], 1.f, "2x2 inverse identity 0,0");
    expect_near(identity[0][1], 0.f, "2x2 inverse identity 0,1");
    expect_near(identity[1][0], 0.f, "2x2 inverse identity 1,0");
    expect_near(identity[1][1], 1.f, "2x2 inverse identity 1,1");

    const float3x3 matrix3{1.f, 2.f, 3.f,
                           0.f, 1.f, 4.f,
                           5.f, 6.f, 0.f};
    const float3x3 identity3 = matrix3 * horizon::math::inverse(matrix3);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const float expected = row == column ? 1.f : 0.f;
            expect_near(identity3[column][row], expected, "3x3 inverse identity");
        }
    }

    const float4x4 matrix4{1.f, 2.f, 3.f, 4.f,
                           0.f, 1.f, 4.f, 2.f,
                           5.f, 6.f, 0.f, 1.f,
                           1.f, 0.f, 2.f, 1.f};
    const float4x4 identity4 = matrix4 * horizon::math::inverse(matrix4);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const float expected = row == column ? 1.f : 0.f;
            expect_near(identity4[column][row], expected, "4x4 inverse identity");
        }
    }
}

void test_swizzle_reads() {
    using horizon::math::float4;
    using horizon::math::float3;

    const float4 value{1.f, 2.f, 3.f, 4.f};
    const float3 zyx = value.zyx();
    const float4 wzyx = value.wzyx();

    expect_equal(zyx[0], 3.f, "swizzle read zyx[0]");
    expect_equal(zyx[1], 2.f, "swizzle read zyx[1]");
    expect_equal(zyx[2], 1.f, "swizzle read zyx[2]");
    expect_equal(wzyx[0], 4.f, "swizzle read wzyx[0]");
    expect_equal(wzyx[1], 3.f, "swizzle read wzyx[1]");
    expect_equal(wzyx[2], 2.f, "swizzle read wzyx[2]");
    expect_equal(wzyx[3], 1.f, "swizzle read wzyx[3]");
}

void test_swizzle_writes() {
    using horizon::math::float2;
    using horizon::math::float4;

    float4 value{1.f, 2.f, 3.f, 4.f};
    value.xy() = float2{10.f, 20.f};
    expect_equal(value.x, 10.f, "swizzle write xy x");
    expect_equal(value.y, 20.f, "swizzle write xy y");

    value.wz() = float2{40.f, 30.f};
    expect_equal(value.z, 30.f, "swizzle write wz z");
    expect_equal(value.w, 40.f, "swizzle write wz w");

    value.xx() = float2{7.f, 8.f};
    expect_equal(value.x, 8.f, "swizzle write repeated component");
}

void test_swizzle_aliases_and_operations() {
    using horizon::math::float4;

    float4 value{1.f, 2.f, 3.f, 4.f};
    value.xy() += float4{5.f, 6.f, 0.f, 0.f}.xy();
    expect_equal(value.x, 6.f, "swizzle compound addition x");
    expect_equal(value.y, 8.f, "swizzle compound addition y");

    const auto reversed = value.wzyx();
    const auto length_squared = reversed[0] * reversed[0] +
                                reversed[1] * reversed[1] +
                                reversed[2] * reversed[2] +
                                reversed[3] * reversed[3];
    expect(std::fabs(length_squared - 125.f) < 1e-6f,
           "swizzle values preserve host vector data");
}

}// namespace

int main() {
    test_vector_arithmetic();
    test_matrix_arithmetic();
    test_matrix_inverse();
    test_swizzle_reads();
    test_swizzle_writes();
    test_swizzle_aliases_and_operations();
    return failures == 0 ? 0 : 1;
}
