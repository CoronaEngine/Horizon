#include "tests/backend_test.h"
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

        CHECK(half(-2.f) < half(-1.f));
        CHECK(half(-0.f) == half(0.f));
        CHECK(half(-0.f) <= half(0.f));
        CHECK(!static_cast<bool>(half(-0.f)));
        // Exhaustive finite decoding and round-trip, with an arithmetic oracle
        // independent of the production bit conversion.
        for (uint bits = 0; bits != 65536u; ++bits) {
                const uint exponent = (bits >> 10u) & 31u;
                const uint mantissa = bits & 1023u;
                const float actual = half_to_float(uint16_t(bits));
                if (exponent == 31u) {
                        CHECK(mantissa ? std::isnan(actual) : std::isinf(actual));
                        continue;
                }
                float expected = exponent ? std::ldexp(1.f + float(mantissa) / 1024.f, int(exponent) - 15)
                                          : std::ldexp(float(mantissa), -24);
                if (bits & 0x8000u) expected = -expected;
                CHECK(bit_cast<uint>(actual) == bit_cast<uint>(expected));
                CHECK(float_to_half(actual) == bits);
        }
        // Test both sides of every positive half midpoint and ties-to-even,
        // including the transition from zero through subnormal to normal.
        for (uint bits = 0; bits < 0x7bffu; ++bits) {
                const float midpoint = (half_to_float(uint16_t(bits)) + half_to_float(uint16_t(bits + 1u))) * 0.5f;
                CHECK(float_to_half(midpoint) == bits + (bits & 1u));
                CHECK(float_to_half(-midpoint) == (0x8000u | (bits + (bits & 1u))));
                CHECK(float_to_half(std::nextafter(midpoint, -std::numeric_limits<float>::infinity())) == bits);
                CHECK(float_to_half(std::nextafter(midpoint, std::numeric_limits<float>::infinity())) == bits + 1u);
        }
        CHECK(float_to_half(65520.f) == 0x7c00u);
        CHECK(float_to_half(-65520.f) == 0xfc00u);

        return true;
}

[[nodiscard]] bool test_device_half() {
        RHIContext &context = RHIContext::instance();
        Device device = context.create_device(test_backend_name());
        device.init_rtx();
        Stream stream = device.create_stream();

        Buffer<float4> output = device.create_buffer<float4>(3u, "test_half_results");
        vector<float4> host(3u, make_float4(0.f));

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
                result.write(2u, make_float4(half2float(float2half(Float(1.0006f))),
                                             half2float(float2half(Float(0x1p-24f))),
                                             half2float(float2half(Float(-0.f))),
                                             half2float(float2half(Float(65504.f)))));
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
        CHECK(host[2].x == 1.0009765625f);
        CHECK(host[2].y == 0x1p-24f);
        CHECK(host[2].z == 0.f && std::signbit(host[2].z));
        CHECK(host[2].w == 65504.f);

        // Use memory inputs so neither compiler can replace conversion and
        // arithmetic with constant-folded values. The host converter used by
        // this oracle was exhaustively checked above, including every midpoint.
        const vector<float> boundary_values{
                0.f, -0.f, 0x1p-24f, -0x1p-24f, 0x1.ff8p-15f, -0x1.ff8p-15f,
                0x1p-14f, -0x1p-14f, 0x1p-25f, -0x1p-25f, 0x1.8p-24f, -0x1.8p-24f,
                1.00048828125f, -1.00048828125f, 1.00146484375f, -1.00146484375f,
                4097.f, -4097.f, 4099.f, -4099.f, 65504.f, -65504.f, 65520.f, -65520.f,
                std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()};
        constexpr uint rows = 5u;
        vector<float4> inputs;
        for (float x : boundary_values) inputs.push_back(make_float4(x, 0x1p-24f, .5f, 0.f));
        auto input = device.create_buffer<float4>(inputs.size(), "half_dynamic_input");
        auto bits = device.create_buffer<uint4>(inputs.size() * rows, "half_dynamic_bits");
        auto half_storage = device.create_byte_buffer(inputs.size() * sizeof(half), "half_dynamic_storage");
        auto storage_bits = device.create_buffer<uint>(inputs.size(), "half_storage_bits");
        Kernel convert = [](BufferVar<float4> in, BufferVar<uint4> out, ByteBufferVar stored) {
                Uint i = dispatch_id();
                Float4 values = in.read(i);
                Half cast_value = cast<half>(values.x);
                Half implicit_value = values.x;
                Float implicit_float = implicit_value;
                out.write(i * rows, make_uint4(as<uint>(half2float(float2half(values.x))),
                                               as<uint>(cast<float>(cast_value)),
                                               as<uint>(implicit_float),
                                               as<uint>(half2float(cast_value))));
                Half2 pair = make_half2(values.x, values.x);
                Half3 triple = make_half3(values.x);
                Half4 quad = values;
                out.write(i * rows + 1u, make_uint4(as<uint>(make_float2(pair).x), as<uint>(make_float3(triple).z),
                                                    as<uint>(make_float4(quad).x), as<uint>(cast<float4>(quad).x)));
                auto matrix = make_half2x2(values.x, values.y, values.z, values.w);
                auto widened = make_float2x2(matrix);
                auto diagonal = make_half2x2(values.x);
                out.write(i * rows + 2u, make_uint4(as<uint>(cast<float>(matrix[0u][0u])),
                                                    as<uint>(widened[0u][0u]),
                                                    as<uint>(cast<float>(diagonal[1u][1u])),
                                                    as<uint>(make_float2(matrix[0u]).x)));
                Half step = values.y;
                Half scale = values.z;
                out.write(i * rows + 3u, make_uint4(as<uint>(cast<float>(cast_value + step)),
                                                    as<uint>(cast<float>(cast_value * scale)),
                                                    as<uint>(cast<float>(cast_value - step)),
                                                    as<uint>(cast<float>(cast_value / scale))));
                implicit_value = values.x;
                stored.store(i * uint(sizeof(half)), implicit_value);
                out.write(i * rows + 4u, make_uint4(as<uint>(cast<float>(implicit_value)),
                                                    as<uint>(cast<float>(pair.y)),
                                                    as<uint>(cast<float>(triple.x)),
                                                    as<uint>(cast<float>(quad.x))));
        };
        Kernel decode = [](ByteBufferVar stored, BufferVar<uint> out) {
                Uint i = dispatch_id();
                out.write(i, as<uint>(cast<float>(stored.load_as<half>(i * uint(sizeof(half))))));
        };
        auto convert_shader = device.compile(convert, "test_half_dynamic_convert");
        auto decode_shader = device.compile(decode, "test_half_dynamic_decode");
        vector<uint4> actual(inputs.size() * rows);
        vector<uint> stored_actual(inputs.size());
        stream << input.upload(inputs.data())
               << convert_shader(input, bits, half_storage).dispatch(uint(inputs.size()))
               << decode_shader(half_storage, storage_bits).dispatch(uint(inputs.size()))
               << bits.download(actual.data()) << storage_bits.download(stored_actual.data())
               << synchronize() << commit();
        auto rounded = [](float value) { return half_to_float(float_to_half(value)); };
        auto check_bits = [](uint actual_bits, float expected, uint input_index, uint row, uint component) {
                const bool passed = std::isnan(expected) ? std::isnan(bit_cast<float>(actual_bits))
                                                        : actual_bits == bit_cast<uint>(expected);
                if (!passed) std::cerr << "half-dynamic input=" << input_index << " row=" << row
                                      << " component=" << component << " actual_bits=" << actual_bits
                                      << " expected_bits=" << bit_cast<uint>(expected) << std::endl;
                return passed;
        };
        bool dynamic_passed = true;
        for (uint i = 0; i < inputs.size(); ++i) {
                const float value = rounded(inputs[i].x);
                const float step = rounded(inputs[i].y), scale = rounded(inputs[i].z);
                const float4 arithmetic = make_float4(rounded(value + step), rounded(value * scale),
                                                      rounded(value - step), rounded(value / scale));
                for (uint row = 0; row < rows; ++row)
                        for (uint c = 0; c < 4u; ++c)
                                dynamic_passed &= check_bits(actual[i * rows + row][c], row == 3u ? arithmetic[c] : value, i, row, c);
                dynamic_passed &= check_bits(stored_actual[i], value, i, rows, 0u);
        }
        CHECK(dynamic_passed);

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
