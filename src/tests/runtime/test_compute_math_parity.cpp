#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace ocarina;

namespace {

uint failures = 0u;

// Independent binary16 round-to-nearest-even oracle. The project's host half
// conversion currently mishandles subnormals, so it cannot validate the GPU.
float half_roundtrip_reference(float value) {
    if (!std::isfinite(value) || value == 0.f) return value;
    const double magnitude = std::abs(double(value));
    if (magnitude >= 65520.0)
        return std::copysign(std::numeric_limits<float>::infinity(), value);
    const int exponent = std::ilogb(magnitude);
    const double quantum = std::ldexp(1.0, std::max(exponent - 10, -24));
    const double units = magnitude / quantum;
    uint rounded = uint(std::floor(units));
    const double fractional = units - rounded;
    if (fractional > 0.5 || (fractional == 0.5 && (rounded & 1u) != 0u)) ++rounded;
    return std::copysign(float(rounded * quantum), value);
}

void check_uint(uint actual, uint expected, const char *operation, uint row, uint component) {
    if (actual == expected) return;
    if (failures++ < 24u)
        std::cerr << operation << " row=" << row << " component=" << component
                  << " actual=" << actual << " expected=" << expected << std::endl;
}

void check_float(float actual, float expected, const char *operation, uint row, uint component,
                 bool signed_zero = false) {
    bool equal = std::isnan(expected) ? std::isnan(actual)
                 : std::isinf(expected) ? actual == expected
                 : std::isfinite(actual) && std::abs(actual - expected) <= 3e-5f * std::max(1.f, std::abs(expected));
    if (equal && signed_zero && expected == 0.f)
        equal = actual == 0.f && std::signbit(actual) == std::signbit(expected);
    if (equal) return;
    if (failures++ < 24u)
        std::cerr << operation << " row=" << row << " component=" << component
                  << " actual=" << actual << " expected=" << expected
                  << " actual_bits=" << std::bit_cast<uint>(actual)
                  << " expected_bits=" << std::bit_cast<uint>(expected) << std::endl;
}

void test_float_special_values(Device &device) {
    const float nan = std::bit_cast<float>(0x7fc12345u);
    const float infinity = std::numeric_limits<float>::infinity();
    vector<float4> input{
        make_float4(nan, 0.25f, -infinity, infinity),
        make_float4(0.25f, nan, infinity, -infinity),
        make_float4(nan, nan, -0.f, 0.f),
        make_float4(-0.f, 0.f, -1.5f, 1.5f),
        make_float4(0.f, -0.f, 0.5f, -0.5f),
        make_float4(infinity, -2.f, 2.f, nan),
        make_float4(-infinity, 2.f, -2.f, nan),
        make_float4(-1.75f, 0.625f, 1.75f, -0.625f)};
    constexpr uint rows = 8u;
    auto source = device.create_buffer<float4>(input.size());
    auto destination = device.create_buffer<float4>(input.size() * rows);
    Kernel kernel = [](BufferVar<float4> in, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Float4 v = in.read(i);
        Float x = v.x, y = v.y;
        out.write(i * rows, make_float4(min(x, y), min(y, x), max(x, y), max(y, x)));
        out.write(i * rows + 1u, make_float4(clamp(x, -0.5f, 0.75f), saturate(x), round(x), sign(x)));
        out.write(i * rows + 2u, make_float4(copysign(x, y), abs(x), floor(x), ceil(x)));
        out.write(i * rows + 3u, make_float4(select(ocarina::isnan(x), y, x), select(ocarina::isinf(x), y, x), rcp(x), sqrt(abs(x))));
        out.write(i * rows + 4u, make_float4(cast<float>(ocarina::isnan(x)), cast<float>(ocarina::isinf(x)),
                                             cast<float>(ocarina::isnan(y)), cast<float>(ocarina::isinf(y))));
        out.write(i * rows + 5u, round(v));
        out.write(i * rows + 6u, select(ocarina::isnan(v), make_float4(42.f), v));
        out.write(i * rows + 7u, min(v, make_float4(0.25f)));
    };
    auto shader = device.compile(kernel, "compute-math-special-values");
    vector<float4> actual(input.size() * rows);
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, destination).dispatch(uint(input.size()))
           << destination.download(actual.data()) << synchronize() << commit();
    for (uint i = 0u; i < input.size(); ++i) {
        const float4 v = input[i];
        float x = v.x, y = v.y;
        // DSL fminf/fmaxf select the numeric operand if exactly one is NaN.
        // Either sign is accepted for a min/max tie between signed zero values.
        array<float4, rows> expected{
            make_float4(std::fmin(x, y), std::fmin(y, x), std::fmax(x, y), std::fmax(y, x)),
            make_float4(std::fmin(0.75f, std::fmax(-0.5f, x)), std::fmin(1.f, std::fmax(0.f, x)), std::round(x), x >= 0.f ? 1.f : -1.f),
            make_float4(std::copysign(x, y), std::abs(x), std::floor(x), std::ceil(x)),
            make_float4(std::isnan(x) ? y : x, std::isinf(x) ? y : x, 1.f / x, std::sqrt(std::abs(x))),
            make_float4(float(std::isnan(x)), float(std::isinf(x)), float(std::isnan(y)), float(std::isinf(y))),
            make_float4(0.f), make_float4(0.f), make_float4(0.f)};
        for (uint c = 0u; c < 4u; ++c) {
            expected[5u][c] = std::round(input[i][c]);
            expected[6u][c] = std::isnan(input[i][c]) ? 42.f : input[i][c];
            expected[7u][c] = std::fmin(input[i][c], 0.25f);
        }
        for (uint row = 0u; row < rows; ++row)
            for (uint c = 0u; c < 4u; ++c)
                check_float(actual[i * rows + row][c], expected[row][c], "special float", i * rows + row, c,
                            row == 2u || row == 3u || row == 5u || row == 6u || (row == 1u && c == 2u));
    }
}

void test_finite_math_and_conversions(Device &device) {
    vector<float4> input{
        make_float4(-7.875f, 0.125f, 4097.f, 4097.f),
        make_float4(7.875f, 0.75f, 1.0009765625f, 1.0009765625f),
        make_float4(-0.5f, 1.5f, -4097.f, 4097.f),
        make_float4(0.5f, 2.5f, 0.00006103515625f, 0.5f),
        make_float4(-0.f, 4.f, 0.000000059604644775390625f, 1.f),
        make_float4(0.f, 8.f, 65504.f, 0.5f)};
    constexpr uint rows = 5u;
    auto source = device.create_buffer<float4>(input.size());
    auto destination = device.create_buffer<float4>(input.size() * rows);
    auto integers = device.create_buffer<uint4>(input.size());
    Kernel kernel = [](BufferVar<float4> in, BufferVar<float4> out, BufferVar<uint4> bits) {
        Uint i = dispatch_id();
        Float4 v = in.read(i);
        Float x = v.x, positive = v.y;
        out.write(i * rows, make_float4(fract(x), sin(x), cos(x), atan2(x, positive)));
        out.write(i * rows + 1u, make_float4(exp2(x), log2(positive), sqrt(positive), rsqrt(positive)));
        out.write(i * rows + 2u, make_float4(lerp(0.375f, x, positive), inverse_lerp(0.25f, x, positive),
                                             fma(v.z, v.w, -16785408.f), pow(positive, 1.5f)));
        auto half_values = make_half4(v);
        out.write(i * rows + 3u, make_float4(half_values));
        Float3 a = make_float3(x, positive, 0.75f);
        Float3 b = make_float3(positive, -0.5f, x);
        out.write(i * rows + 4u, make_float4(dot(a, b), length_squared(a), distance_squared(a, b), length(a)));
        bits.write(i, make_uint4(as<uint>(cast<int>(x)), cast<uint>(positive * 1024.f),
                                 as<uint>(cast<float>(cast<int>(x))), as<uint>(v.x)));
    };
    auto shader = device.compile(kernel, "compute-math-finite-conversions");
    vector<float4> actual(input.size() * rows);
    vector<uint4> converted(input.size());
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, destination, integers).dispatch(uint(input.size()))
           << destination.download(actual.data()) << integers.download(converted.data()) << synchronize() << commit();
    for (uint i = 0u; i < input.size(); ++i) {
        const auto v = input[i];
        const float x = v.x, p = v.y;
        const float3 a = make_float3(x, p, 0.75f), b = make_float3(p, -0.5f, x);
        const float3 d = a - b;
        const float aa = a.x * a.x + a.y * a.y + a.z * a.z;
        const array<float4, rows> expected{
            make_float4(x - std::floor(x), std::sin(x), std::cos(x), std::atan2(x, p)),
            make_float4(std::exp2(x), std::log2(p), std::sqrt(p), 1.f / std::sqrt(p)),
            make_float4(0.625f * x + 0.375f * p, (0.25f - x) / (p - x), std::fma(v.z, v.w, -16785408.f), std::pow(p, 1.5f)),
            make_float4(half_roundtrip_reference(v.x), half_roundtrip_reference(v.y),
                        half_roundtrip_reference(v.z), half_roundtrip_reference(v.w)),
            make_float4(a.x * b.x + a.y * b.y + a.z * b.z, aa, d.x * d.x + d.y * d.y + d.z * d.z, std::sqrt(aa))};
        for (uint row = 0u; row < rows; ++row)
            for (uint c = 0u; c < 4u; ++c)
                check_float(actual[i * rows + row][c], expected[row][c], "finite math", i * rows + row, c, row == 3u);
        for (uint c = 0u; c < 4u; ++c)
            check_uint(std::bit_cast<uint>(actual[i * rows + 3u][c]), std::bit_cast<uint>(expected[3u][c]),
                       "half conversion bits", i, c);
        const uint4 expected_bits = make_uint4(std::bit_cast<uint>(int(x)), uint(p * 1024.f),
                                                std::bit_cast<uint>(float(int(x))), std::bit_cast<uint>(x));
        for (uint c = 0u; c < 4u; ++c) check_uint(converted[i][c], expected_bits[c], "conversion bits", i, c);
    }
}

void test_integer_bits(Device &device) {
    vector<uint4> input{
        make_uint4(0xffffffffu, 0x80000001u, 0u, std::bit_cast<uint>(-17)),
        make_uint4(0x80000000u, 0x7fffffffu, 1u, std::bit_cast<uint>(17)),
        make_uint4(0x12345678u, 0x9abcdef0u, 7u, std::bit_cast<uint>(-12345)),
        make_uint4(1u, 0xffffffffu, 31u, std::bit_cast<uint>(12345))};
    constexpr uint rows = 10u;
    auto source = device.create_buffer<uint4>(input.size());
    auto destination = device.create_buffer<uint4>(input.size() * rows);
    Kernel kernel = [](BufferVar<uint4> in, BufferVar<uint4> out) {
        Uint i = dispatch_id();
        Uint4 v = in.read(i);
        Uint a = v.x, b = v.y, shift = v.z;
        Int signed_value = as<int>(v.w);
        out.write(i * rows, make_uint4(a + b, a - b, a * b, ~a));
        out.write(i * rows + 1u, make_uint4(a & b, a | b, a ^ b, a >> shift));
        out.write(i * rows + 2u, make_uint4(a << shift, as<uint>(signed_value / -7),
                                             as<uint>(signed_value % -7), as<uint>(signed_value >> shift)));
        Ulong wide = (cast<ulong>(a) << 32u) | cast<ulong>(b);
        Ulong moved = (wide >> 17u) ^ (wide << 9u);
        out.write(i * rows + 3u, make_uint4(cast<uint>(wide), cast<uint>(wide >> 32u),
                                             cast<uint>(moved), cast<uint>(moved >> 32u)));
        Uint3 left = make_uint3(a, b, 17u), right = make_uint3(b, 3u, a);
        auto mask = left < right;
        Uint3 chosen = select(mask, left, right);
        out.write(i * rows + 4u, make_uint4(chosen, cast<uint>(all(mask)) + 2u * cast<uint>(any(mask)) + 4u * cast<uint>(none(mask))));
        auto remainder4 = make_int4(signed_value, -signed_value, signed_value, -signed_value) % make_int4(-7, -7, 7, 7);
        out.write(i * rows + 5u, as<uint4>(remainder4));
        DynamicArray<int> numerators(3u), divisors(3u);
        numerators[0u] = signed_value; numerators[1u] = -signed_value; numerators[2u] = signed_value;
        divisors[0u] = -7; divisors[1u] = -7; divisors[2u] = 7;
        auto array_remainders = numerators % divisors;
        out.write(i * rows + 6u, make_uint4(as<uint>(array_remainders[0u]), as<uint>(array_remainders[1u]),
                                             as<uint>(array_remainders[2u]), 0u));
        Int4 signed_vector = make_int4(signed_value, -signed_value, signed_value + 1, -signed_value - 1);
        out.write(i * rows + 7u, as<uint4>(signed_vector % -7));
        out.write(i * rows + 8u, as<uint4>(signed_value % make_int4(-7, 7, -11, 11)));
        DynamicArray<int4> vector_numerators(2u);
        vector_numerators[0u] = signed_vector;
        vector_numerators[1u] = -signed_vector;
        auto vector_remainders = vector_numerators % -7;
        out.write(i * rows + 9u, as<uint4>(vector_remainders[1u]));
    };
    auto shader = device.compile(kernel, "compute-math-integer-bits");
    vector<uint4> actual(input.size() * rows);
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, destination).dispatch(uint(input.size()))
           << destination.download(actual.data()) << synchronize() << commit();
    for (uint i = 0u; i < input.size(); ++i) {
        auto v = input[i];
        const uint a = v.x, b = v.y, shift = v.z;
        const int s = std::bit_cast<int>(v.w);
        // Construct arithmetic right shift explicitly, without relying on the
        // host compiler's behavior for a negative signed operand.
        uint shifted = v.w >> shift;
        if (s < 0 && shift != 0u) shifted |= 0xffffffffu << (32u - shift);
        const uint64_t wide = (uint64_t(a) << 32u) | b;
        const uint64_t moved = (wide >> 17u) ^ (wide << 9u);
        const bool p0 = a < b, p1 = b < 3u, p2 = 17u < a;
        array<uint4, rows> expected{
            make_uint4(a + b, a - b, a * b, ~a), make_uint4(a & b, a | b, a ^ b, a >> shift),
            make_uint4(a << shift, std::bit_cast<uint>(s / -7), std::bit_cast<uint>(s % -7), shifted),
            make_uint4(uint(wide), uint(wide >> 32u), uint(moved), uint(moved >> 32u)),
            make_uint4(p0 ? a : b, p1 ? b : 3u, p2 ? 17u : a,
                       uint(p0 && p1 && p2) + 2u * uint(p0 || p1 || p2) + 4u * uint(!(p0 || p1 || p2))),
            make_uint4(uint(s % -7), uint(-s % -7), uint(s % 7), uint(-s % 7)),
            make_uint4(uint(s % -7), uint(-s % -7), uint(s % 7), 0u),
            make_uint4(uint(s % -7), uint(-s % -7), uint((s + 1) % -7), uint((-s - 1) % -7)),
            make_uint4(uint(s % -7), uint(s % 7), uint(s % -11), uint(s % 11)),
            make_uint4(uint(-s % -7), uint(s % -7), uint((-s - 1) % -7), uint((s + 1) % -7))};
        for (uint row = 0u; row < rows; ++row)
            for (uint c = 0u; c < 4u; ++c) check_uint(actual[i * rows + row][c], expected[row][c], "integer bits", i * rows + row, c);
    }
}

void test_half_conversion_boundaries(Device &device) {
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::bit_cast<float>(0x7fc12345u);
    vector<float4> input{
        make_float4(4097.f, 4099.f, -4097.f, -4099.f),
        make_float4(1.00048828125f, 1.00146484375f, -1.00048828125f, -1.00146484375f),
        make_float4(0.f, -0.f, infinity, -infinity),
        make_float4(nan, -nan, 65504.f, 65520.f),
        make_float4(0x1p-24f, -0x1p-24f, 0x1p-25f, -0x1p-25f),
        make_float4(0x1.8p-24f, -0x1.8p-24f, 0x1.ffcp-15f, -0x1.ffcp-15f)};
    constexpr uint rows = 6u;
    auto source = device.create_buffer<float4>(input.size());
    auto destination = device.create_buffer<float4>(input.size() * rows);
    Kernel kernel = [](BufferVar<float4> in, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Float4 v = in.read(i);
        out.write(i * rows, make_float4(cast<float>(cast<half>(v.x)), cast<float>(cast<half>(v.y)),
                                       cast<float>(cast<half>(v.z)), cast<float>(cast<half>(v.w))));
        out.write(i * rows + 1u, make_float4(cast<float>(float2half(v.x)), cast<float>(float2half(v.y)),
                                             cast<float>(float2half(v.z)), cast<float>(float2half(v.w))));
        Half implicit_value = v.x;
        Float first = cast<float>(implicit_value);
        implicit_value = v.y;
        Half2 pair = make_half2(v.z, v.w);
        out.write(i * rows + 2u, make_float4(first, cast<float>(implicit_value), make_float2(pair)));
        Half4 implicit_vector = v;
        out.write(i * rows + 3u, make_float4(implicit_vector));
        implicit_vector = v;
        out.write(i * rows + 4u, make_float4(implicit_vector));
        auto matrix_value = make_half2x2(v.x, v.y, v.z, v.w);
        out.write(i * rows + 5u, make_float4(make_float2(matrix_value[0u]), make_float2(matrix_value[1u])));
    };
    auto shader = device.compile(kernel, "compute-math-half-boundaries");
    vector<float4> actual(input.size() * rows);
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, destination).dispatch(uint(input.size()))
           << destination.download(actual.data()) << synchronize() << commit();
    for (uint i = 0u; i < input.size(); ++i) {
        for (uint row = 0u; row < rows; ++row) {
            for (uint c = 0u; c < 4u; ++c) {
                const float expected = half_roundtrip_reference(input[i][c]);
                check_float(actual[i * rows + row][c], expected, "half conversion", i * rows + row, c, true);
                // NaN payloads may be canonicalized, but every finite/inf/zero
                // value must match the independent binary16 oracle exactly.
                if (!std::isnan(expected))
                    check_uint(std::bit_cast<uint>(actual[i * rows + row][c]), std::bit_cast<uint>(expected),
                               "half conversion bits", i * rows + row, c);
            }
        }
    }
}

void test_half_conversion_exhaustive(Device &device) {
    constexpr uint count = 65536u, rows = 4u;
    auto output = device.create_buffer<uint4>(count * rows);
    Kernel kernel = [](BufferVar<uint4> out) {
        Uint i = dispatch_id();
        Half2 pair = as<half2>(i | ((i ^ 0x8000u) << 16u));
        Half2 other = as<half2>((i ^ 0x03ffu) | ((i ^ 0x83ffu) << 16u));
        Half3 triple = make_half3(pair, other.x);
        Half4 quad = make_half4(pair, other);
        Float scalar = cast<float>(pair.x);
        Float2 decoded_pair = make_float2(pair);
        Float3 decoded_triple = make_float3(triple);
        Float4 decoded_quad = make_float4(quad);
        out.write(i * rows, make_uint4(as<uint>(scalar), as<uint>(decoded_pair.x), as<uint>(decoded_pair.y), 0u));
        out.write(i * rows + 1u, make_uint4(as<uint3>(decoded_triple), 0u));
        out.write(i * rows + 2u, as<uint4>(decoded_quad));
        Half2 encoded_pair = make_half2(decoded_pair);
        Half3 encoded_triple = make_half3(decoded_triple);
        Half4 encoded_quad = make_half4(decoded_quad);
        Half2 encoded_low = make_half2(encoded_quad.x, encoded_quad.y);
        Half2 encoded_high = make_half2(encoded_quad.z, encoded_quad.w);
        out.write(i * rows + 3u, make_uint4(as<uint>(encoded_pair), as<uint>(make_half2(encoded_triple.y, encoded_triple.z)),
                                            as<uint>(encoded_low), as<uint>(encoded_high)));
    };
    auto shader = device.compile(kernel, "compute-math-half-exhaustive");
    vector<uint4> actual(count * rows);
    auto stream = device.create_stream();
    stream << shader(output).dispatch(count) << output.download(actual.data()) << synchronize() << commit();
    auto decoded_bits = [](uint bits) {
        const uint exponent = (bits >> 10u) & 31u, mantissa = bits & 1023u;
        if (exponent == 31u) return ((bits & 0x8000u) << 16u) | 0x7f800000u | (mantissa << 13u);
        const float magnitude = exponent ? std::ldexp(1.f + float(mantissa) / 1024.f, int(exponent) - 15)
                                         : std::ldexp(float(mantissa), -24);
        return std::bit_cast<uint>(std::copysign(magnitude, bits & 0x8000u ? -1.f : 1.f));
    };
    auto encoded_bits = [](uint bits) {
        // Numeric conversion quiets NaNs; sign and the remaining payload survive.
        return (bits & 0x7c00u) == 0x7c00u && (bits & 0x03ffu) != 0u ? bits | 0x0200u : bits;
    };
    for (uint i = 0u; i < count; ++i) {
        const array<uint, 4> bits{i, i ^ 0x8000u, i ^ 0x03ffu, i ^ 0x83ffu};
        const array<uint4, rows> expected{
            make_uint4(decoded_bits(bits[0]), decoded_bits(bits[0]), decoded_bits(bits[1]), 0u),
            make_uint4(decoded_bits(bits[0]), decoded_bits(bits[1]), decoded_bits(bits[2]), 0u),
            make_uint4(decoded_bits(bits[0]), decoded_bits(bits[1]), decoded_bits(bits[2]), decoded_bits(bits[3])),
            make_uint4(encoded_bits(bits[0]) | (encoded_bits(bits[1]) << 16u),
                       encoded_bits(bits[1]) | (encoded_bits(bits[2]) << 16u),
                       encoded_bits(bits[0]) | (encoded_bits(bits[1]) << 16u),
                       encoded_bits(bits[2]) | (encoded_bits(bits[3]) << 16u))};
        for (uint row = 0u; row < rows; ++row)
            for (uint c = 0u; c < 4u; ++c)
                check_uint(actual[i * rows + row][c], expected[row][c], "exhaustive half conversion", i * rows + row, c);
    }
}

void test_half_conversion_midpoints(Device &device) {
    constexpr uint count = 0x7bffu;
    vector<float4> input(count);
    vector<uint2> expected(count);
    for (uint i = 0u; i < count; ++i) {
        auto value = [](uint bits) {
            const uint exponent = bits >> 10u, mantissa = bits & 1023u;
            return exponent ? std::ldexp(1.f + float(mantissa) / 1024.f, int(exponent) - 15)
                            : std::ldexp(float(mantissa), -24);
        };
        const float midpoint = (value(i) + value(i + 1u)) * 0.5f;
        input[i] = make_float4(midpoint, std::nextafter(midpoint, -std::numeric_limits<float>::infinity()),
                              std::nextafter(midpoint, std::numeric_limits<float>::infinity()), -midpoint);
        const uint tie = i + (i & 1u);
        expected[i] = make_uint2(tie | (i << 16u), (i + 1u) | ((tie | 0x8000u) << 16u));
    }
    auto source = device.create_buffer<float4>(count);
    auto output = device.create_buffer<uint2>(count);
    Kernel kernel = [](BufferVar<float4> in, BufferVar<uint2> out) {
        Half4 converted = make_half4(in.read(dispatch_id()));
        Half2 low = make_half2(converted.x, converted.y);
        Half2 high = make_half2(converted.z, converted.w);
        out.write(dispatch_id(), make_uint2(as<uint>(low), as<uint>(high)));
    };
    auto shader = device.compile(kernel, "compute-math-half-midpoints");
    vector<uint2> actual(count);
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, output).dispatch(count)
           << output.download(actual.data()) << synchronize() << commit();
    for (uint i = 0u; i < count; ++i)
        for (uint c = 0u; c < 2u; ++c)
            check_uint(actual[i][c], expected[i][c], "half midpoint rounding", i, c);
}

void test_half_array_arithmetic(Device &device) {
    vector<float4> input{
        make_float4(1.f, 0x1p-11f, -1.f, -0x1p-11f),
        make_float4(4096.f, 1.f, 4096.f, 3.f),
        make_float4(0x1p-14f, -0x1p-24f, -0x1p-14f, 0x1p-24f),
        make_float4(65504.f, 16.f, -65504.f, -16.f)};
    auto source = device.create_buffer<float4>(input.size());
    auto output = device.create_buffer<float2>(input.size());
    Kernel kernel = [](BufferVar<float4> in, BufferVar<float2> out) {
        Uint i = dispatch_id();
        Float4 v = in.read(i);
        DynamicArray<half> lhs(2u), rhs(2u);
        lhs[0u] = cast<half>(v.x); lhs[1u] = cast<half>(v.z);
        rhs[0u] = cast<half>(v.y); rhs[1u] = cast<half>(v.w);
        auto sum = lhs + rhs;
        out.write(i, make_float2(cast<float>(sum[0u]), cast<float>(sum[1u])));
    };
    auto shader = device.compile(kernel, "compute-math-half-array-arithmetic");
    vector<float2> actual(input.size());
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, output).dispatch(uint(input.size()))
           << output.download(actual.data()) << synchronize() << commit();
    for (uint i = 0u; i < input.size(); ++i) {
        for (uint c = 0u; c < 2u; ++c) {
            const float expected = half_roundtrip_reference(half_roundtrip_reference(input[i][2u * c]) +
                                                            half_roundtrip_reference(input[i][2u * c + 1u]));
            check_uint(std::bit_cast<uint>(actual[i][c]), std::bit_cast<uint>(expected), "half array sum", i, c);
        }
    }
}

void test_half_bit_copies(Device &device) {
    // Pair the 16-bit patterns to exercise the public half2/uint bit-cast API,
    // including signaling/quiet NaNs whose payload must survive plain copies.
    vector<uint> input{0x7c017e55u, 0xfc01fe55u, 0x80000000u, 0x00018001u, 0x7c00fc00u, 0x3c00bc00u};
    auto source = device.create_buffer<uint>(input.size());
    auto halves = device.create_buffer<half2>(input.size());
    auto output = device.create_buffer<uint4>(input.size());
    auto matrix_output = device.create_buffer<uint>(input.size());
    Kernel kernel = [](BufferVar<uint> in, BufferVar<half2> stored, BufferVar<uint4> out, BufferVar<uint> matrix_out) {
        Callable identity = [](Half2 value) { return value; };
        Uint i = dispatch_id();
        Half2 value = as<half2>(in.read(i));
        Half2 copied = value;
        stored.write(i, copied);
        Half2 constructed = make_half2(cast<half2>(copied));
        Half2 selected = select(make_bool2(true), copied, make_half2(0.f));
        out.write(i, make_uint4(as<uint>(copied), as<uint>(stored.read(i)),
                               as<uint>(constructed), as<uint>(identity(selected))));
        auto matrix_value = make_half2x2(value, copied);
        auto matrix_copy = make_half2x2(matrix_value);
        matrix_out.write(i, as<uint>(matrix_copy[1u]));
    };
    auto shader = device.compile(kernel, "compute-math-half-bit-copies");
    vector<uint4> actual(input.size());
    vector<uint> actual_matrix(input.size());
    auto stream = device.create_stream();
    stream << source.upload(input.data()) << shader(source, halves, output, matrix_output).dispatch(uint(input.size()))
           << output.download(actual.data()) << matrix_output.download(actual_matrix.data()) << synchronize() << commit();
    for (uint i = 0u; i < input.size(); ++i) {
        for (uint c = 0u; c < 4u; ++c) check_uint(actual[i][c], input[i], "half bit copies", i, c);
        check_uint(actual_matrix[i], input[i], "half matrix bit copy", i, 0u);
    }
}

int callable_reference(int value) {
    if (value < 0) return -value + 11;
    return value * 3 + 5;
}

void test_control_flow(Device &device) {
    constexpr uint count = 67u;
    auto output = device.create_buffer<uint4>(count);
    Kernel kernel = [](BufferVar<uint4> out) {
        Callable transform = [](Int value) {
            $if(value < 0) { $return(-value + 11); };
            return value * 3 + 5;
        };
        Uint id = dispatch_id();
        Int value = cast<int>(id) - 33;
        Int first = transform(value), second = transform(-value);
        Int total = 0;
        $for(j, 0u, id % 11u + 1u) {
            $if(j == 2u) { $continue; };
            $if(j > 7u) { $break; };
            $if((j & 1u) == 0u) { total += cast<int>(j) * 3; }
            $else { total -= cast<int>(j); };
        };
        Int selected = 0;
        $switch(id % 4u) {
            $case(0u) { selected = 17; $break; };
            $case(1u) { selected = -13; $break; };
            $case(2u) { selected = 7; };
            $default { selected += 5; $break; };
        };
        Uint iteration = 0u;
        Int while_sum = 0;
        $while(iteration < id % 7u + 1u) {
            iteration += 1u;
            $if(iteration == 3u) { $continue; };
            while_sum += cast<int>(iteration);
        };
        Var<array<int, 5>> local;
        $for(j, 0u, 5u) { local[j] = value + cast<int>(j * j); };
        Int checksum = 0;
        $for(j, 0u, 5u) { checksum += local[(j + id) % 5u]; };
        out.write(id, make_uint4(as<uint>(first + 2 * second), as<uint>(total), as<uint>(selected + while_sum), as<uint>(checksum)));
    };
    auto shader = device.compile(kernel, "compute-math-control-flow");
    vector<uint4> actual(count);
    auto stream = device.create_stream();
    stream << shader(output).dispatch(count) << output.download(actual.data()) << synchronize() << commit();
    for (uint id = 0u; id < count; ++id) {
        const int value = int(id) - 33;
        int total = 0;
        for (uint j = 0u; j < id % 11u + 1u; ++j) {
            if (j == 2u) continue;
            if (j > 7u) break;
            total += (j & 1u) == 0u ? int(j) * 3 : -int(j);
        }
        int selected = id % 4u == 0u ? 17 : id % 4u == 1u ? -13 : id % 4u == 2u ? 12 : 5;
        int while_sum = 0;
        for (uint j = 1u; j <= id % 7u + 1u; ++j) if (j != 3u) while_sum += int(j);
        const uint4 expected = make_uint4(uint(callable_reference(value) + 2 * callable_reference(-value)),
                                           uint(total), uint(selected + while_sum), uint(5 * value + 30));
        for (uint c = 0u; c < 4u; ++c) check_uint(actual[id][c], expected[c], "control flow", id, c);
    }
}

}// namespace

int main() {
    auto device = create_test_device();
    test_integer_bits(device);
    test_control_flow(device);
    test_finite_math_and_conversions(device);
    test_half_conversion_boundaries(device);
    test_half_conversion_exhaustive(device);
    test_half_conversion_midpoints(device);
    test_half_array_arithmetic(device);
    test_half_bit_copies(device);
    test_float_special_values(device);
    if (failures != 0u) {
        std::cerr << "compute math parity: " << failures << " mismatches" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "compute math parity checks passed" << std::endl;
}
