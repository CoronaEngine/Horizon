//
// Created by z on 21/01/2026.
//

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/basic_traits.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

template<typename T>
[[nodiscard]] bool close_enough(T lhs, T rhs, float eps = 1e-3f) {
        return std::abs(static_cast<float>(lhs) - static_cast<float>(rhs)) <= eps;
}

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

static_assert(is_half_v<half>);
static_assert(is_floating_point_v<half>);
static_assert(is_scalar_v<half>);
static_assert(is_half_vector_v<half2>);
static_assert(is_half_vector2_v<half2>);
static_assert(is_half_vector3_v<half3>);
static_assert(is_half_vector4_v<half4>);
static_assert(is_general_half_vector_v<half3>);
static_assert(std::is_same_v<vector_element_t<half3>, half>);
static_assert(std::is_same_v<type_element_t<half4>, half>);
static_assert(std::is_same_v<binary_op_half_target_t<int>, half>);
static_assert(std::is_same_v<binary_op_half_target_t<float>, float>);
static_assert(std::is_same_v<decltype(half{} + int{}), half>);
static_assert(std::is_same_v<decltype(float{} + half{}), float>);

[[nodiscard]] bool test_host_half() {
        half value = 1.5f;
        CHECK(close_enough(static_cast<float>(value), 1.5f));

        half sum = half(1.25f) + half(2.5f);
        CHECK(close_enough(static_cast<float>(sum), 3.75f));

        half scaled = half(3.f) * 2;
        CHECK(close_enough(static_cast<float>(scaled), 6.f));

        float mixed = 1.0f + half(2.5f);
        CHECK(close_enough(mixed, 3.5f));

        CHECK(half(3.f) > half(2.f));
        CHECK(half(3.f) >= 3);
        CHECK(half(1.f) != half(2.f));
        CHECK(static_cast<bool>(half(1.f)));
        CHECK(!static_cast<bool>(half(0.f)));

        half inf = std::numeric_limits<float>::infinity();
        CHECK(inf.is_inf());
        CHECK(!inf.is_nan());

        half nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(nan.is_nan());
        CHECK(nan != nan);

        return true;
}

[[nodiscard]] bool test_device_half() {
        RHIContext &context = RHIContext::instance();
        Device device = context.create_device("cuda");
        device.init_rtx();
        Stream stream = device.create_stream();

        Buffer<float4> output = device.create_buffer<float4>(2u, "test_half_results");
        vector<float4> host(2u, make_float4(0.f));

        Kernel kernel = [&](BufferVar<float4> result) {
                Half scalar = 4.f;
                Half3 vec = make_float3(1.5f, 2.5f, 3.5f);
                Half3 offset = make_float3(0.5f, 0.5f, 0.5f);

                result.write(0u, make_float4(cast<float>(sqrt(scalar)),
                                                                         cast<float>(scalar + Half(2.f)),
                                                                         cast<float>(vec.x),
                                                                         cast<float>(select(true, Half(5.f), Half(1.f)))));
                result.write(1u, make_float4(cast<float>((Half(3.f) * Half(2.f))),
                                                                         cast<float>((Half(7.f) / 2)),
                                             cast<float>(Half(8.f) * Half(0.25f)),
                                                                         cast<float>((vec + offset).z)));
        };

        auto shader = device.compile(kernel, "test_half_device");
        stream << shader(output).dispatch(1u)
                   << output.download(host.data())
                   << synchronize()
                   << commit();

        CHECK(close_enough(host[0].x, 2.f));
        CHECK(close_enough(host[0].y, 6.f));
        CHECK(close_enough(host[0].z, 1.5f));
        CHECK(close_enough(host[0].w, 5.f));
        CHECK(close_enough(host[1].x, 6.f));
        CHECK(close_enough(host[1].y, 3.5f));
        CHECK(close_enough(host[1].z, 2.f));
        CHECK(close_enough(host[1].w, 4.f));

        return true;
}

}// namespace

int main() {
        if (!test_host_half()) {
                return EXIT_FAILURE;
        }
        if (!test_device_half()) {
                return EXIT_FAILURE;
        }
        std::cout << "[test-half] all checks passed" << std::endl;
        return EXIT_SUCCESS;
}