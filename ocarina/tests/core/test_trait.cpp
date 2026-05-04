//
// Created by Zero on 21/09/2022.
//

#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/basic_traits.h"

using namespace ocarina;

struct Test {
    int a;
    real3 b;
};


OC_STRUCT(,Test, a, b){};

namespace {

enum class TestEnum : uint {
    Value = 7u
};

using HostSwizzle2 = decltype(std::declval<float4 &>().xy());
using HostSwizzle3 = decltype(std::declval<float4 &>().xyz());
using DeviceSwizzle2 = decltype(std::declval<Var<float4> &>().xy());

[[nodiscard]] bool check_impl(bool condition, string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        return false;
    }
    return true;
}

#define CHECK(...)                                \
    do {                                          \
        if (!check_impl((__VA_ARGS__), #__VA_ARGS__)) { \
            return false;                         \
        }                                         \
    } while (false)

static_assert(to_underlying(TestEnum::Value) == 7u);

static_assert(is_integral_v<int>);
static_assert(is_integral_v<const uint &>);
static_assert(is_integral_v<size_t>);
static_assert(!is_integral_v<float>);

static_assert(is_boolean_v<bool>);
static_assert(!is_boolean_v<int>);

static_assert(is_half_v<half>);
static_assert(is_float_v<float>);
static_assert(is_real_v<real>);
static_assert(is_floating_point_v<half>);
static_assert(is_floating_point_v<float>);
static_assert(is_floating_point_v<real>);
static_assert(!is_floating_point_v<int>);

static_assert(is_signed_v<int>);
static_assert(is_signed_v<float>);
static_assert(is_signed_v<real>);
static_assert(is_signed_v<half>);
static_assert(!is_signed_v<uint>);

static_assert(is_unsigned_v<uint>);
static_assert(is_unsigned_v<ulong>);
static_assert(!is_unsigned_v<int>);

static_assert(is_scalar_v<int>);
static_assert(is_scalar_v<bool>);
static_assert(is_scalar_v<float>);
static_assert(is_scalar_v<real>);
static_assert(!is_scalar_v<float2>);

static_assert(is_dynamic_size_v<Test>);

static_assert(is_number_v<int>);
static_assert(is_number_v<float>);
static_assert(is_number_v<real>);
static_assert(!is_number_v<bool>);

static_assert(is_all_scalar_v<int, bool, float>);
static_assert(is_any_float_v<int, float, bool>);
static_assert(is_none_half_v<int, float, uint>);
static_assert(is_all_number_v<int, uint, float>);
static_assert(is_any_boolean_v<int, bool, float>);
static_assert(is_none_unsigned_v<int, float, bool>);

static_assert(is_same_v<int, int, int>);
static_assert(!is_same_v<int, uint>);
static_assert(all_is_v<int, const int &, int>);
static_assert(!all_is_v<int, int, uint>);

static_assert(swizzle_dimension_v<HostSwizzle2> == 2u);
static_assert(swizzle_dimension_v<HostSwizzle3> == 3u);
static_assert(is_swizzle_v<HostSwizzle2>);
static_assert(is_swizzle_v<HostSwizzle2, 2u>);
static_assert(is_host_swizzle_v<HostSwizzle2>);
static_assert(is_host_swizzle_v<HostSwizzle2, 2u>);
static_assert(!is_device_swizzle_v<HostSwizzle2>);
static_assert(is_device_swizzle_v<DeviceSwizzle2>);
static_assert(is_device_swizzle_v<DeviceSwizzle2, 2u>);
static_assert(!is_host_swizzle_v<DeviceSwizzle2>);
static_assert(std::is_same_v<swizzle_decay_t<HostSwizzle2>, float2>);

static_assert(vector_dimension_v<float> == 1u);
static_assert(vector_dimension_v<float3> == 3u);
static_assert(matrix_dimension_v<float2x2> == 2u);
static_assert(matrix_dimension_v<float3x3> == 3u);
static_assert(type_dimension_v<float> == 1u);
static_assert(type_dimension_v<float3> == 3u);
static_assert(type_dimension_v<HostSwizzle3> == 3u);

static_assert(is_same_type_dimension_v<float3, HostSwizzle3, int3>);
static_assert(!is_same_type_dimension_v<float3, float4>);
static_assert(std::is_same_v<vector_element_t<float3>, float>);
static_assert(std::is_same_v<type_element_t<float3>, float>);
static_assert(std::is_same_v<type_element_t<HostSwizzle3>, float>);
static_assert(is_same_type_element_v<float3, HostSwizzle3>);
static_assert(!is_same_type_element_v<float3, int3>);

static_assert(is_vector_v<float2>);
static_assert(is_vector_v<float3>);
static_assert(is_vector2_v<float2>);
static_assert(is_vector3_v<float3>);
static_assert(is_vector4_v<float4>);
static_assert(!is_vector4_v<float3>);
static_assert(is_general_vector_v<HostSwizzle2>);
static_assert(is_general_vector2_v<HostSwizzle2>);
static_assert(is_general_vector3_v<HostSwizzle3>);
static_assert(is_vector_same_dimension_v<float3, int3, uint3>);
static_assert(!is_vector_same_dimension_v<float2, float3>);

static_assert(is_float_vector_v<float2>);
static_assert(is_float_vector3_v<float3>);
static_assert(is_int_vector3_v<int3>);
static_assert(is_uint_vector4_v<uint4>);
static_assert(is_general_float_vector2_v<HostSwizzle2>);
static_assert(is_general_integer_vector3_v<int3>);

static_assert(is_matrix_v<float2x2>);
static_assert(is_matrix_v<float3x3>);
static_assert(is_matrix2_v<float2x2>);
static_assert(is_matrix3_v<float3x3>);
static_assert(is_all_matrix_v<float2x2, float3x3, float4x4>);
static_assert(!is_matrix_v<float4>);

static_assert(is_basic_v<float>);
static_assert(is_basic_v<real>);
static_assert(is_basic_v<float3>);
static_assert(is_basic_v<real3>);
static_assert(is_basic_v<float3x3>);
static_assert(is_basic_v<real3x3>);
static_assert(is_all_basic_v<float, float3, float3x3>);
static_assert(is_all_basic_v<real, real3, real3x3>);
static_assert(is_general_basic_v<HostSwizzle2>);
static_assert(is_all_general_basic_v<float, float2, HostSwizzle2>);
static_assert(is_real_op_enable_v<int>);
static_assert(is_real_op_enable_v<float>);
static_assert(is_real_op_enable_v<double>);
static_assert(std::is_same_v<binary_op_real_target_t<int>, real>);
static_assert(std::is_same_v<binary_op_real_target_t<float>, float>);
static_assert(std::is_same_v<binary_op_real_target_t<double>, double>);
static_assert(std::is_same_v<decltype(real{} + int{}), real>);
static_assert(std::is_same_v<decltype(real{} + float{}), float>);
static_assert(std::is_same_v<decltype(double{} + real{}), double>);

static_assert(is_simple_type<int>::value);
static_assert(is_simple_type<float4>::value);
static_assert(!is_simple_type<const int &>::value);

static_assert(is_std_vector_v<ocarina::vector<int>>);
static_assert(!is_std_vector_v<int>);

static_assert(std::is_same_v<general_vector_t<float, 1u>, float>);
static_assert(std::is_same_v<general_vector_t<float, 3u>, float3>);
static_assert(std::is_same_v<general_vector_t<real, 3u>, real3>);
static_assert(std::is_same_v<general_vector_t<int, 4u>, int4>);

static_assert(match_basic_func_v<float3, HostSwizzle3>);
static_assert(match_basic_func_v<float3, float3, HostSwizzle3>);
static_assert(!match_basic_func_v<float3, int3>);
static_assert(!match_basic_func_v<float3, float4>);

static_assert(sizeof(real) == sizeof(float));
static_assert(alignof(real) == alignof(float));

[[nodiscard]] bool test_to_underlying_runtime() {
    CHECK(to_underlying(TestEnum::Value) == 7u);
    return true;
}

[[nodiscard]] bool test_decay_swizzle_runtime() {
    float4 value = make_float4(1.f, 2.f, 3.f, 4.f);
    auto yz = decay_swizzle(value.yz());
    auto wzyx = decay_swizzle(value.wzyx());

    CHECK(yz.x == 2.f);
    CHECK(yz.y == 3.f);
    CHECK(wzyx.x == 4.f);
    CHECK(wzyx.y == 3.f);
    CHECK(wzyx.z == 2.f);
    CHECK(wzyx.w == 1.f);
    return true;
}

[[nodiscard]] bool test_general_vector_runtime() {
    general_vector_t<float, 1u> scalar = 5.f;
    general_vector_t<int, 3u> vec = make_int3(4, 5, 6);
    real3 rvec = make_real3(1.f, 2.f, 3.f);

    CHECK(scalar == 5.f);
    CHECK(vec.x == 4);
    CHECK(vec.y == 5);
    CHECK(vec.z == 6);
    CHECK(static_cast<float>(rvec.x) == 1.f);
    CHECK(static_cast<float>(rvec.y) == 2.f);
    CHECK(static_cast<float>(rvec.z) == 3.f);
    return true;
}

[[nodiscard]] bool test_real_runtime() {
    real value = 1.5f;
    real sum = value + 2;
    float mixed_float = value + 2.5f;
    double mixed_double = 2.0 + value;
    real accum = value;
    accum += 2;
    accum *= 2;
    real inc = value;
    ++inc;
    ++inc;
    real neg_zero = -real{0.f};
    real inf = std::numeric_limits<float>::infinity();
    real nan = std::numeric_limits<float>::quiet_NaN();
    std::stringstream ss;
    ss << real{3.25f};
    real parsed{};
    ss >> parsed;

    CHECK(static_cast<float>(sum) == 3.5f);
    CHECK(mixed_float == 4.f);
    CHECK(mixed_double == 3.5);
    CHECK(static_cast<float>(accum) == 7.f);
    CHECK(inc == 3.5f);
    CHECK(real{3.f} > 2);
    CHECK(2 < real{3.f});
    CHECK(static_cast<bool>(real{1.f}));
    CHECK(!static_cast<bool>(real{0.f}));
    CHECK(neg_zero.is_neg());
    CHECK(inf.is_inf());
    CHECK(!inf.is_nan());
    CHECK(nan.is_nan());
    CHECK(nan != nan);
    CHECK(parsed == real{3.25f});
    CHECK(real2float(real{4.5f}) == 4.5f);
    CHECK(float2real(2.25f) == real{2.25f});
    return true;
}

}// namespace

int main() {
    if (!test_to_underlying_runtime()) {
        return EXIT_FAILURE;
    }
    if (!test_decay_swizzle_runtime()) {
        return EXIT_FAILURE;
    }
    if (!test_general_vector_runtime()) {
        return EXIT_FAILURE;
    }
    if (!test_real_runtime()) {
        return EXIT_FAILURE;
    }
    std::cout << "[test-trait] all checks passed" << std::endl;
    return EXIT_SUCCESS;
}