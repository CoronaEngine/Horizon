#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>

using namespace ocarina;

namespace {

float half_reference(float value) {
    if (!std::isfinite(value) || value == 0.f) return value;
    const double magnitude = std::abs(double(value));
    if (magnitude >= 65520.0) return std::copysign(std::numeric_limits<float>::infinity(), value);
    const double quantum = std::ldexp(1.0, std::max(std::ilogb(magnitude) - 10, -24));
    const double units = magnitude / quantum;
    uint rounded = uint(std::floor(units));
    const double fraction = units - rounded;
    if (fraction > 0.5 || (fraction == 0.5 && (rounded & 1u))) ++rounded;
    return std::copysign(float(rounded * quantum), value);
}

bool float32_subnormal(Device &device) {
    const array<uint, 10> patterns{0x00000001u, 0x80000001u, 0x007fffffu, 0x807fffffu,
                                  0x00800000u, 0x80800000u, 0u, 0x80000000u, 0x3f800000u, 0xbf800000u};
    vector<float4> input;
    for (uint bits : patterns) input.push_back(make_float4(std::bit_cast<float>(bits), 2.f, 0.5f, 0.f));
    auto source = device.create_buffer<float4>(input.size());
    auto output = device.create_buffer<uint4>(input.size() * 2u);
    Kernel kernel = [](BufferVar<float4> in, BufferVar<uint4> out) {
        Uint i = dispatch_id();
        Float4 v = in.read(i);
        out.write(i * 2u, as<uint4>(make_float4(v.x * v.y, v.x * v.z, v.x + v.w, v.x / v.y)));
        out.write(i * 2u + 1u, make_uint4(cast<uint>(v.x < v.w), cast<uint>(v.x == v.w),
                                             as<uint>(v.x), cast<uint>(ocarina::isinf(v.x))));
    };
    auto shader = device.compile(kernel, "policy-float32-subnormal");
    vector<uint4> actual(input.size() * 2u);
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, output).dispatch(uint(input.size()))
           << output.download(actual.data()) << synchronize() << commit();
    constexpr const char *operations[]{"mul2", "mul_half", "add_zero", "div2"};
    for (uint i = 0u; i < input.size(); ++i) {
        const double x = input[i].x;
        const array<float, 4> gradual_reference{float(x * 2.0), float(x * 0.5), float(x + 0.0), float(x / 2.0)};
        for (uint c = 0u; c < 4u; ++c)
            std::cout << "FLOAT_BITS row=" << i << " op=" << operations[c] << " input=" << patterns[i]
                      << " actual=" << actual[i * 2u][c]
                      << " gradual_reference=" << std::bit_cast<uint>(gradual_reference[c]) << std::endl;
        constexpr const char *comparison_operations[]{"less_zero", "equal_zero", "copy", "isinf"};
        for (uint c = 0u; c < 4u; ++c)
            std::cout << "FLOAT_BITS row=" << i << " op=" << comparison_operations[c] << " input=" << patterns[i]
                      << " actual=" << actual[i * 2u + 1u][c] << std::endl;
    }
    std::cout << "float32-subnormal result=OBSERVED (no equality policy assumed)" << std::endl;
    return true;
}

bool check_double_reference(float actual, double expected, const char *operation, float input) {
    bool passed;
    if (std::isnan(expected)) passed = std::isnan(actual);
    else if (std::isinf(expected)) passed = std::isinf(actual) && std::signbit(actual) == std::signbit(expected);
    else passed = std::isfinite(actual) && std::abs(double(actual) - expected) <= 3e-5 * std::abs(expected) + 1e-12;
    std::cout << "FLOAT_MATH op=" << operation << " input=" << input << " actual=" << actual << " expected=" << expected
              << " result=" << (passed ? "PASS" : "FAIL") << std::endl;
    return passed;
}

bool hyperbolic_stability(Device &device) {
    vector<float2> input{
        make_float2(1e-8f, 1e-8f), make_float2(-1e-8f, -1e-8f),
        make_float2(1e-4f, 1e-4f), make_float2(-1e-4f, -1e-4f),
        make_float2(100.f, 0.9999f), make_float2(-100.f, -0.9999f),
        make_float2(1e20f, 0.5f), make_float2(-1e20f, -0.5f),
        make_float2(0.f, 0.f), make_float2(-0.f, -0.f)};
    auto source = device.create_buffer<float2>(input.size());
    auto output = device.create_buffer<float4>(input.size());
    Kernel kernel = [](BufferVar<float2> in, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Float2 v = in.read(i);
        out.write(i, make_float4(ocarina::asinh(v.x), ocarina::acosh(abs(v.x) + 1.f),
                                ocarina::tanh(v.x), ocarina::atanh(v.y)));
    };
    auto shader = device.compile(kernel, "policy-hyperbolic-stability");
    vector<float4> actual(input.size());
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, output).dispatch(uint(input.size()))
           << output.download(actual.data()) << synchronize() << commit();
    bool passed = true;
    for (uint i = 0u; i < input.size(); ++i) {
        // The argument expression is evaluated in float on the GPU too; round
        // abs(x)+1 before evaluating the independent double acosh reference.
        const float acosh_argument = std::abs(input[i].x) + 1.f;
        passed &= check_double_reference(actual[i].x, std::asinh(double(input[i].x)), "asinh", input[i].x);
        passed &= check_double_reference(actual[i].y, std::acosh(double(acosh_argument)), "acosh", acosh_argument);
        passed &= check_double_reference(actual[i].z, std::tanh(double(input[i].x)), "tanh", input[i].x);
        passed &= check_double_reference(actual[i].w, std::atanh(double(input[i].y)), "atanh", input[i].y);
    }
    return passed;
}

bool half_intermediate(Device &device) {
    vector<float4> input{
        make_float4(-6.6796875f, -0.024627685546875f, 207.375f, 0.f),
        make_float4(6.6796875f, 0.024627685546875f, -207.375f, 0.f),
        make_float4(3.f, 1.f, 5.f, 0.f)};
    auto source = device.create_buffer<float4>(input.size());
    auto output = device.create_buffer<float4>(input.size());
    Kernel kernel = [](BufferVar<float4> in, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Float4 v = in.read(i);
        Half x = cast<half>(v.x), a = cast<half>(v.y), b = cast<half>(v.z);
        Half builtin_result = inverse_lerp(x, a, b);
        Half explicit_result = (x - a) / (b - a);
        Half final_rounding = cast<half>((cast<float>(x) - cast<float>(a)) / (cast<float>(b) - cast<float>(a)));
        out.write(i, make_float4(cast<float>(builtin_result), cast<float>(explicit_result), cast<float>(final_rounding), 0.f));
    };
    auto shader = device.compile(kernel, "policy-half-intermediate");
    vector<float4> actual(input.size());
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, output).dispatch(uint(input.size()))
           << output.download(actual.data()) << synchronize() << commit();
    bool passed = true;
    constexpr const char *operations[]{"inverse_lerp_half", "explicit_half_steps", "float_steps_final_half"};
    for (uint i = 0u; i < input.size(); ++i) {
        const float x = half_reference(input[i].x), a = half_reference(input[i].y), b = half_reference(input[i].z);
        const float stepped = half_reference(half_reference(x - a) / half_reference(b - a));
        const float final_only = half_reference((x - a) / (b - a));
        for (uint c = 0u; c < 3u; ++c) {
            const float expected = c == 2u ? final_only : stepped;
            const bool equal = std::bit_cast<uint>(actual[i][c]) == std::bit_cast<uint>(expected);
            passed &= equal;
            std::cout << "HALF_MATH op=" << operations[c] << " row=" << i << " actual=" << actual[i][c] << " expected=" << expected
                      << " actual_bits=" << std::bit_cast<uint>(actual[i][c]) << " expected_bits=" << std::bit_cast<uint>(expected)
                      << " half_step_reference=" << stepped << " final_only_reference=" << final_only
                      << " result=" << (equal ? "PASS" : "FAIL") << std::endl;
        }
    }
    return passed;
}

template<uint Dimension>
bool half_inverse_dimension(Device &device) {
    // Diagonally dominant matrices, uploaded as columns. CPU reference
    // starts from the same rounded binary16 inputs, then uses double elimination.
    vector<float4> input;
    if constexpr (Dimension == 2u) input = {
        // The determinant is an HFMA rounding midpoint plus discarded low
        // bits: float FMA then half incorrectly gives 0.626953125, while one
        // binary16 rounding gives 0.62646484375. This also tests the complete
        // inverse's reciprocal and signed cofactor paths.
        make_float4(0.97998046875f, 1.f, 0.f, 0.f),
        make_float4(-0.6298828125f, -0.003238677978515625f, 0.f, 0.f)};
    if constexpr (Dimension == 3u) input = {
        make_float4(2.25f, 0.125f, -0.25f, 0.f), make_float4(0.375f, 1.75f, 0.0625f, 0.f), make_float4(-0.125f, 0.25f, 2.5f, 0.f),
        make_float4(1.3f, 0.21f, -0.1f, 0.f), make_float4(-0.15f, 2.1f, 0.35f, 0.f), make_float4(0.17f, -0.2f, 1.6f, 0.f),
        make_float4(0.625f, -0.125f, 0.0625f, 0.f), make_float4(0.25f, 1.125f, -0.1875f, 0.f), make_float4(-0.0625f, 0.125f, 0.875f, 0.f)};
    // Keep the original regressions, then vary each signed off-diagonal and
    // minor independently. Row-wise strict diagonal dominance keeps the new
    // matrices nonsingular and well-conditioned after binary16 input rounding.
    uint state = 0x7193a5cdu;
    auto random_uint = [&] {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    };
    for (uint index = 0u; index < 24u; ++index) {
        float columns[Dimension][Dimension]{};
        for (uint c = 0u; c < Dimension; ++c)
            for (uint r = 0u; r < Dimension; ++r)
                if (c != r) columns[c][r] = float(int(random_uint() % 2001u) - 1000) / 1537.f;
        for (uint r = 0u; r < Dimension; ++r) {
            float row_sum = 0.f;
            for (uint c = 0u; c < Dimension; ++c) row_sum += std::abs(columns[c][r]);
            columns[r][r] = row_sum + 0.75f + float(random_uint() % 1001u) / 997.f;
        }
        for (uint c = 0u; c < Dimension; ++c) {
            float4 column = make_float4(0.f);
            for (uint r = 0u; r < Dimension; ++r) column[r] = columns[c][r];
            input.push_back(column);
        }
    }
    constexpr uint matrix_count = Dimension == 3u ? 27u : Dimension == 2u ? 25u : 24u;
    auto source = device.create_buffer<float4>(input.size());
    auto output = device.create_buffer<float4>(input.size());
    Kernel kernel = [](BufferVar<float4> in, BufferVar<float4> out) {
        Uint base = dispatch_id() * Dimension;
        Float4 c0 = in.read(base), c1 = in.read(base + 1u);
        if constexpr (Dimension == 2u) {
            auto matrix_value = make_half2x2(make_half2(c0.xy()), make_half2(c1.xy()));
            auto result = inverse(matrix_value);
            for (uint c = 0u; c < Dimension; ++c) out.write(base + c, make_float4(make_float2(result[c]), 0.f, 0.f));
        } else if constexpr (Dimension == 3u) {
            Float4 c2 = in.read(base + 2u);
            auto matrix_value = make_half3x3(make_half3(c0.xyz()), make_half3(c1.xyz()), make_half3(c2.xyz()));
            auto result = inverse(matrix_value);
            for (uint c = 0u; c < Dimension; ++c) out.write(base + c, make_float4(make_float3(result[c]), 0.f));
        } else {
            Float4 c2 = in.read(base + 2u), c3 = in.read(base + 3u);
            auto matrix_value = make_half4x4(make_half4(c0), make_half4(c1), make_half4(c2), make_half4(c3));
            auto result = inverse(matrix_value);
            for (uint c = 0u; c < Dimension; ++c) out.write(base + c, make_float4(result[c]));
        }
    };
    auto shader = device.compile(kernel, "policy-half-inverse");
    vector<float4> actual(input.size());
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, output).dispatch(matrix_count)
           << output.download(actual.data()) << synchronize() << commit();
    bool passed = true;
    for (uint matrix_index = 0u; matrix_index < matrix_count; ++matrix_index) {
        double augmented[Dimension][Dimension * 2u]{};
        for (uint r = 0u; r < Dimension; ++r) {
            for (uint c = 0u; c < Dimension; ++c) augmented[r][c] = half_reference(input[matrix_index * Dimension + c][r]);
            augmented[r][Dimension + r] = 1.0;
        }
        for (uint pivot = 0u; pivot < Dimension; ++pivot) {
            uint best = pivot;
            for (uint r = pivot + 1u; r < Dimension; ++r)
                if (std::abs(augmented[r][pivot]) > std::abs(augmented[best][pivot])) best = r;
            for (uint c = 0u; c < Dimension * 2u; ++c) std::swap(augmented[pivot][c], augmented[best][c]);
            const double divisor = augmented[pivot][pivot];
            for (uint c = 0u; c < Dimension * 2u; ++c) augmented[pivot][c] /= divisor;
            for (uint r = 0u; r < Dimension; ++r) {
                if (r == pivot) continue;
                const double factor = augmented[r][pivot];
                for (uint c = 0u; c < Dimension * 2u; ++c) augmented[r][c] -= factor * augmented[pivot][c];
            }
        }
        for (uint c = 0u; c < Dimension; ++c) {
            for (uint r = 0u; r < Dimension; ++r) {
                const float value = actual[matrix_index * Dimension + c][r];
                const double expected = augmented[r][Dimension + c];
                // Allow accumulated half arithmetic error while exposing every
                // output bit separately for cross-backend comparison.
                const double tolerance = 4e-3 * std::abs(expected) + 2e-4;
                const bool equal = std::isfinite(value) && std::abs(double(value) - expected) <= tolerance;
                passed &= equal;
                std::cout << "HALF_INVERSE dimension=" << Dimension << " row=" << matrix_index << " column=" << c << " component=" << r
                          << " actual=" << value << " actual_bits=" << std::bit_cast<uint>(value)
                          << " expected=" << expected << " tolerance=" << tolerance
                          << " comparison=cross_backend_bits result=" << (equal ? "PASS" : "FAIL") << std::endl;
            }
        }
    }
    return passed;
}

bool half_inverse(Device &device) {
    bool passed = half_inverse_dimension<2u>(device);
    passed &= half_inverse_dimension<3u>(device);
    passed &= half_inverse_dimension<4u>(device);
    return passed;
}

}// namespace

int main(int argc, char **argv) {
    std::cout << std::setprecision(17);
    if (argc != 3 || std::string_view(argv[1]) != "--case") {
        std::cerr << "usage: --case float32-subnormal|hyperbolic-stability|half-intermediate|half-inverse" << std::endl;
        return EXIT_FAILURE;
    }
    const std::string_view name = argv[2];
    bool (*probe)(Device &) = name == "float32-subnormal" ? float32_subnormal
        : name == "hyperbolic-stability" ? hyperbolic_stability : name == "half-intermediate" ? half_intermediate
        : name == "half-inverse" ? half_inverse : nullptr;
    if (!probe) {
        std::cerr << "unknown case: " << name << std::endl;
        return EXIT_FAILURE;
    }
    const char *backend = test_backend_name();
    std::cout << "case=" << name << " backend=" << backend << std::endl;
    auto device = RHIContext::instance().create_device();
    const bool passed = probe(device);
    if (name != "float32-subnormal") std::cout << "case=" << name << " result=" << (passed ? "PASS" : "FAIL") << std::endl;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
