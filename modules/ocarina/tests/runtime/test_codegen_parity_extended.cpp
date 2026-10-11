#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"
#include <cmath>
#include <iostream>

using namespace ocarina;

namespace {

void require_close(float actual, float expected, const char *test, uint row, uint component) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 3e-5f * std::max(1.f, std::abs(expected))) {
        std::cerr << test << " row=" << row << " component=" << component
                  << " actual=" << actual << " expected=" << expected << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void check_rows(const vector<float4> &actual, const vector<float4> &expected, const char *test) {
    for (uint i = 0; i < expected.size(); ++i)
        for (uint j = 0; j < 4u; ++j) require_close(actual[i][j], expected[i][j], test, i, j);
}

void test_sampling_frames(Device &device) {
    // Axis poles, equal-magnitude branches and both hemispheres catch a rotated
    // but otherwise valid basis, which changes a renderer's fixed-seed samples.
    vector<float3> normals{
        make_float3(0.f, 0.f, 1.f), make_float3(0.f, 0.f, -1.f),
        make_float3(1.f, 0.f, 0.f), make_float3(0.f, 1.f, 0.f),
        normalize(make_float3(1.f, 1.f, 1.f)), normalize(make_float3(-2.f, 1.f, -3.f)),
        normalize(make_float3(0.01f, -0.75f, 0.66f)), make_float3(0.f, 0.f, 2.f)};
    constexpr uint rows = 6u;
    auto input = device.create_buffer<float3>(normals.size());
    auto output = device.create_buffer<float4>(normals.size() * rows);
    Kernel kernel = [](BufferVar<float3> in, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Float3 n = in.read(i);
        Float3 a, b, tangent_a, tangent_b;
        coordinate_system(n, a, b);
        make_normal_tangent(n, make_float3(0.25f, 0.5f, 0.75f), tangent_a, tangent_b);
        // An asymmetric local sample makes a frame rotation directly observable.
        Float3 sample = a * 0.2f + b * -0.35f + n * 0.8f;
        Float3 tangent_sample = tangent_a * -0.3f + tangent_b * 0.15f + n * 0.9f;
        out.write(i * rows, make_float4(a, 0.f));
        out.write(i * rows + 1u, make_float4(b, 0.f));
        out.write(i * rows + 2u, make_float4(sample, 0.f));
        out.write(i * rows + 3u, make_float4(tangent_a, 0.f));
        out.write(i * rows + 4u, make_float4(tangent_b, 0.f));
        out.write(i * rows + 5u, make_float4(tangent_sample, 0.f));
    };
    auto shader = device.compile(kernel, "extended-parity-sampling-frames");
    vector<float4> actual(normals.size() * rows), expected(actual.size());
    for (uint i = 0; i < normals.size(); ++i) {
        float3 n = normals[i], a, b, tangent_a, tangent_b;
        coordinate_system(n, a, b);
        make_normal_tangent(n, make_float3(0.25f, 0.5f, 0.75f), tangent_a, tangent_b);
        expected[i * rows] = make_float4(a, 0.f);
        expected[i * rows + 1u] = make_float4(b, 0.f);
        expected[i * rows + 2u] = make_float4(a * 0.2f + b * -0.35f + n * 0.8f, 0.f);
        expected[i * rows + 3u] = make_float4(tangent_a, 0.f);
        expected[i * rows + 4u] = make_float4(tangent_b, 0.f);
        expected[i * rows + 5u] = make_float4(tangent_a * -0.3f + tangent_b * 0.15f + n * 0.9f, 0.f);
    }
    auto stream = device.create_stream();
    stream << input.upload(normals.data()) << shader(input, output).dispatch(uint(normals.size()))
           << output.download(actual.data()) << synchronize() << commit();
    check_rows(actual, expected, "sampling frame / world-space direction");
}

void test_matrix_construction(Device &device) {
    constexpr uint count = 33u;
    auto output = device.create_buffer<float4>(count);
    Kernel kernel = [](BufferVar<float4> out, Float scale) {
        uint row = 0u;
        auto emit3 = [&](const auto &matrix) {
            for (uint c = 0; c < 3u; ++c) out.write(row++, make_float4(matrix[c], 0.f));
        };
        Var<float3x3> identity;
        emit3(identity);
        auto diagonal = make_float3x3(scale);
        emit3(diagonal);
        auto matrix2 = make_float2x2(make_float2(scale, 3.f), make_float2(5.f, 7.f));
        auto enlarged = make_float3x3(matrix2);
        emit3(enlarged);
        auto homogeneous = make_float4x4(enlarged);
        for (uint c = 0; c < 4u; ++c) out.write(row++, homogeneous[c]);
        auto shrunk = make_float2x2(homogeneous);
        for (uint c = 0; c < 2u; ++c) out.write(row++, make_float4(shrunk[c], 0.f, 0.f));
        auto rectangular_zero = make_float3x2(scale);
        for (uint c = 0; c < 2u; ++c) out.write(row++, make_float4(rectangular_zero[c], 0.f));
        auto rectangular = make_float3x2(make_float3(scale, 1.f, -2.f), make_float3(4.f, 5.f, 6.f));
        out.write(row++, make_float4(rectangular * make_float2(0.5f, -0.25f), 0.f));
        auto transposed = transpose(rectangular);
        for (uint c = 0; c < 3u; ++c) out.write(row++, make_float4(transposed[c], 0.f, 0.f));
        auto half_diagonal = make_half3x3(cast<half>(scale));
        for (uint c = 0; c < 3u; ++c) out.write(row++, make_float4(make_float3(half_diagonal[c]), 0.f));
        Var<array<float3x3, 2>> defaults;
        emit3(defaults[0u]);
        emit3(defaults[1u]);
        out.write(row++, make_float4(enlarged * make_float3(1.f, -2.f, 0.5f), 0.f));
        out.write(row++, homogeneous * make_float4(1.f, -2.f, 0.5f, 1.f));
        out.write(row++, make_float4(inverse(enlarged) * (enlarged * make_float3(1.f, -2.f, 0.5f)), 0.f));
    };
    auto shader = device.compile(kernel, "extended-parity-matrix-construction");
    constexpr float scale = 2.5f;
    vector<float4> expected;
    auto emit3 = [&](const auto &matrix) {
        for (uint c = 0; c < 3u; ++c) expected.push_back(make_float4(matrix[c], 0.f));
    };
    float3x3 identity;
    emit3(identity);
    auto diagonal = make_float3x3(scale);
    emit3(diagonal);
    auto matrix2 = make_float2x2(make_float2(scale, 3.f), make_float2(5.f, 7.f));
    auto enlarged = make_float3x3(matrix2);
    emit3(enlarged);
    auto homogeneous = make_float4x4(enlarged);
    for (uint c = 0; c < 4u; ++c) expected.push_back(homogeneous[c]);
    auto shrunk = make_float2x2(homogeneous);
    for (uint c = 0; c < 2u; ++c) expected.push_back(make_float4(shrunk[c], 0.f, 0.f));
    auto rectangular_zero = make_float3x2(scale);
    for (uint c = 0; c < 2u; ++c) expected.push_back(make_float4(rectangular_zero[c], 0.f));
    auto rectangular = make_float3x2(make_float3(scale, 1.f, -2.f), make_float3(4.f, 5.f, 6.f));
    expected.push_back(make_float4(rectangular * make_float2(0.5f, -0.25f), 0.f));
    auto transposed = transpose(rectangular);
    for (uint c = 0; c < 3u; ++c) expected.push_back(make_float4(transposed[c], 0.f, 0.f));
    auto half_diagonal = make_half3x3(half(scale));
    for (uint c = 0; c < 3u; ++c) expected.push_back(make_float4(make_float3(half_diagonal[c]), 0.f));
    array<float3x3, 2> defaults;
    emit3(defaults[0]);
    emit3(defaults[1]);
    expected.push_back(make_float4(enlarged * make_float3(1.f, -2.f, 0.5f), 0.f));
    expected.push_back(homogeneous * make_float4(1.f, -2.f, 0.5f, 1.f));
    expected.push_back(make_float4(1.f, -2.f, 0.5f, 0.f));
    if (expected.size() != count) std::exit(EXIT_FAILURE);
    vector<float4> actual(count);
    auto stream = device.create_stream();
    stream << shader(output, scale).dispatch(1u) << output.download(actual.data()) << synchronize() << commit();
    check_rows(actual, expected, "matrix default / diagonal / resize / rectangular");
}

void test_dynamic_array_math(Device &device) {
    // Five spectral-style samples exercise fixed arrays outside Slang's native
    // vector widths, including singleton broadcasts on either side of an op.
    constexpr uint samples = 5u, rows = 4u;
    auto output = device.create_buffer<float4>(samples * rows);
    Kernel kernel = [](BufferVar<float4> out, Float scale) {
        DynamicArray<float> values(samples), other(samples);
        for (uint i = 0; i < samples; ++i) {
            values[i] = scale * (float(i) - 2.f);
            other[i] = float(i) * 0.25f + 0.75f;
        }
        auto positive = abs(values) + 0.25f;
        auto roots = sqrt(positive);
        auto sines = sin(values);
        auto exponentials = exp2(values);
        auto rounded = round(values);
        auto negated = -values;
        auto products = values * other;
        auto reciprocal = 2.f / positive;
        auto mask = values < 0.f;
        auto selected = select(mask, values, other);
        auto blended = values + 0.375f * (other - values);
        auto clamped = values.clamp(Float(-0.75f), Float(0.75f));
        auto powers = positive.map([&](uint i, const Float &x) { return pow(x, other[i]); });
        auto minima = values.map([&](uint i, const Float &x) { return min(x, other[i]); });
        auto maxima = values.map([&](uint i, const Float &x) { return max(x, other[i]); });
        auto signs = sign(values);
        for (uint i = 0; i < samples; ++i) {
            out.write(i * rows, make_float4(roots[i], sines[i], exponentials[i], rounded[i]));
            out.write(i * rows + 1u, make_float4(negated[i], products[i], reciprocal[i], selected[i]));
            out.write(i * rows + 2u, make_float4(blended[i], clamped[i], powers[i], positive[i]));
            out.write(i * rows + 3u, make_float4(minima[i], maxima[i], signs[i], cast<float>(mask[i])));
        }
    };
    auto shader = device.compile(kernel, "extended-parity-dynamic-array-math");
    constexpr float scale = 0.5f;
    vector<float4> actual(samples * rows), expected(actual.size());
    for (uint i = 0; i < samples; ++i) {
        float value = scale * (float(i) - 2.f), other = float(i) * 0.25f + 0.75f;
        float positive = std::abs(value) + 0.25f;
        expected[i * rows] = make_float4(std::sqrt(positive), std::sin(value), std::exp2(value), std::round(value));
        expected[i * rows + 1u] = make_float4(-value, value * other, 2.f / positive, value < 0.f ? value : other);
        expected[i * rows + 2u] = make_float4(value + 0.375f * (other - value), std::clamp(value, -0.75f, 0.75f),
                                            std::pow(positive, other), positive);
        expected[i * rows + 3u] = make_float4(std::min(value, other), std::max(value, other), value >= 0.f ? 1.f : -1.f,
                                            value < 0.f ? 1.f : 0.f);
    }
    auto stream = device.create_stream();
    stream << shader(output, scale).dispatch(1u) << output.download(actual.data()) << synchronize() << commit();
    check_rows(actual, expected, "dynamic array math / operators / broadcast / select");
}

}// namespace

int main() {
    auto device = RHIContext::instance().create_device(test_backend_name());
    test_sampling_frames(device);
    test_matrix_construction(device);
    test_dynamic_array_math(device);
    std::cout << "extended codegen parity checks passed" << std::endl;
}
