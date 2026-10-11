#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace ocarina;

struct CapabilityRecord {
    uint guard;
    array<float3, 2> points;
    float3x3 matrix;
};
OC_STRUCT(, CapabilityRecord, guard, points, matrix) {};

namespace {

bool check(float actual, float expected, std::string_view operation, uint index) {
    const bool passed = std::isfinite(actual) && std::abs(actual - expected) <= 2e-6f * std::max(1.f, std::abs(expected));
    std::cout << operation << " index=" << index << " actual=" << actual
              << " expected=" << expected << " result=" << (passed ? "PASS" : "FAIL") << std::endl;
    return passed;
}

bool inverse2(Device &device) {
    auto output = device.create_buffer<float>(4u);
    Kernel kernel = [](BufferVar<float> out, Float4 columns) {
        auto m = make_float2x2(columns.x, columns.y, columns.z, columns.w);
        auto inv = inverse(m);
        out.write(0u, inv[0u][0u]);
        out.write(1u, inv[0u][1u]);
        out.write(2u, inv[1u][0u]);
        out.write(3u, inv[1u][1u]);
    };
    auto shader = device.compile(kernel, "capability-inverse2");
    array<float, 4> actual{};
    const array<float, 4> expected{0.6f, -0.2f, -0.7f, 0.4f};
    auto stream = device.create_stream();
    stream << shader(output, make_float4(4.f, 2.f, 7.f, 6.f)).dispatch(1u)
           << output.download(actual.data()) << synchronize() << commit();
    bool passed = true;
    for (uint i = 0u; i < 4u; ++i) passed &= check(actual[i], expected[i], "inverse2", i);
    return passed;
}

bool determinant2(Device &device) {
    auto output = device.create_buffer<float>(1u);
    Kernel kernel = [](BufferVar<float> out, Float4 columns) {
        auto m = make_float2x2(columns.x, columns.y, columns.z, columns.w);
        out.write(0u, determinant(m));
    };
    auto shader = device.compile(kernel, "capability-determinant2");
    float actual{};
    auto stream = device.create_stream();
    stream << shader(output, make_float4(4.f, 2.f, 7.f, 6.f)).dispatch(1u)
           << output.download(&actual) << synchronize() << commit();
    return check(actual, 10.f, "determinant2", 0u);
}

bool callable_storage_matrix(Device &device) {
    auto matrices = device.create_buffer<float3x3>(1u);
    Kernel kernel = [](BufferVar<float3x3> values) {
        Callable mutate = [](Var<float3x3> &value) {
            value[1u][2u] += 7.f;
            value[2u][0u] = value[0u][1u] * 3.f;
        };
        mutate(values[0u]);
    };
    auto shader = device.compile(kernel, "capability-callable-storage");
    auto original = make_float3x3(make_float3(1.f, 2.f, 3.f), make_float3(4.f, 5.f, 6.f), make_float3(7.f, 8.f, 9.f));
    auto expected = original;
    expected[1][2] += 7.f;
    expected[2][0] = expected[0][1] * 3.f;
    float3x3 actual{};
    auto stream = device.create_stream();
    stream << matrices.upload(&original) << shader(matrices).dispatch(1u)
           << matrices.download(&actual) << synchronize() << commit();
    bool passed = true;
    for (uint c = 0u; c < 3u; ++c)
        for (uint r = 0u; r < 3u; ++r) passed &= check(actual[c][r], expected[c][r], "callable-storage", c * 3u + r);
    return passed;
}

bool callable_storage_records(Device &device) {
    array<CapabilityRecord, 3> initial{};
    for (auto &record : initial) {
        record.guard = 17u;
        record.points = {make_float3(1.f, 2.f, 3.f), make_float3(4.f, 5.f, 6.f)};
        record.matrix = make_float3x3(make_float3(1.f, 2.f, 3.f), make_float3(4.f, 5.f, 6.f), make_float3(7.f, 8.f, 9.f));
    }
    auto typed = device.create_buffer<CapabilityRecord>(initial.size());
    auto raw = device.create_byte_buffer(sizeof(initial));
    auto output = device.create_buffer<float>(4u);
    Kernel kernel = [](BufferVar<CapabilityRecord> values, ByteBufferVar bytes, BufferVar<float> out, Uint index) {
        Callable mutate = [](Var<CapabilityRecord> &record, Float &member) {
            Callable nested = [](Var<float3x3> &matrix, Float &component) {
                matrix[1u][2u] += 7.f;
                component += matrix[1u][2u];
            };
            // Matrix/component and record/member references deliberately overlap.
            nested(record.matrix, record.matrix[1u][2u]);
            member += 1.f;
            record.points[1u].y += member;
            record.guard += 1u;
            return record.points[1u].y + record.matrix[1u][2u];
        };
        out.write(0u, mutate(values[index], values[index].points[1u].y));
        Uint offset = index * uint(sizeof(CapabilityRecord));
        out.write(1u, mutate(bytes.load_as<CapabilityRecord>(offset),
                            bytes.load_as<CapabilityRecord>(offset).points[1u].y));
        Callable observe = [](Var<CapabilityRecord> &record, BufferVar<CapabilityRecord> &other, Uint i) {
            record.guard = 27u;
            // A copy-in/out implementation would read the old value here.
            return cast<float>(other[i].guard);
        };
        out.write(2u, observe(values[index], values, index));
        Var<CapabilityRecord> local = values[index];
        // Also emit the original callable using local inout parameters.
        out.write(3u, mutate(local, local.points[1u].y));
    };
    auto shader = device.compile(kernel, "capability-callable-storage-records");
    array<CapabilityRecord, 3> typed_actual{}, raw_actual{};
    array<float, 4> results{};
    auto stream = device.create_stream();
    stream << typed.upload(initial.data()) << raw.upload(initial.data())
           << shader(typed, raw, output, 1u).dispatch(1u)
           << typed.download(typed_actual.data()) << raw.download(raw_actual.data())
           << output.download(results.data()) << synchronize() << commit();
    bool passed = true;
    const array<float, 4> expected_results{38.f, 38.f, 27.f, 92.f};
    for (uint i = 0; i < results.size(); ++i) passed &= check(results[i], expected_results[i], "callable-storage-return", i);
    for (const auto *actual : {typed_actual.data(), raw_actual.data()}) {
        for (uint i = 0; i < initial.size(); ++i) {
            CapabilityRecord expected = initial[i];
            if (i == 1u) {
                expected.guard = actual == typed_actual.data() ? 27u : 18u;
                expected.points[1].y = 12.f;
                expected.matrix[1][2] = 26.f;
            }
            passed &= check(float(actual[i].guard), float(expected.guard), "callable-storage-guard", i);
            for (uint j = 0; j < 2u; ++j)
                for (uint k = 0; k < 3u; ++k) passed &= check(actual[i].points[j][k], expected.points[j][k], "callable-storage-array", i * 6u + j * 3u + k);
            for (uint j = 0; j < 3u; ++j)
                for (uint k = 0; k < 3u; ++k) passed &= check(actual[i].matrix[j][k], expected.matrix[j][k], "callable-storage-matrix", i * 9u + j * 3u + k);
        }
    }
    return passed;
}

bool callable_storage_bools(Device &device) {
    const array<bool3, 3> initial{make_bool3(false, true, false), make_bool3(true, false, true), make_bool3(false, true, true)};
    auto bytes = device.create_byte_buffer(sizeof(initial));
    Kernel kernel = [](ByteBufferVar values, Uint index) {
        Callable mutate = [](Var<bool3> &value, Bool &component) {
            value.x = !value.x;
            component = !value.x;
            value.z = !value.z;
        };
        Uint offset = index * uint(sizeof(bool3));
        mutate(values.load_as<bool3>(offset), values.load_as<bool3>(offset).x);
        Callable scalar = [](Bool &first, Bool &second) {
            first = !first;
            second = !second;
        };
        scalar(values.load_as<bool>(offset + 1u), values.load_as<bool>(offset + 1u));
    };
    auto shader = device.compile(kernel, "capability-callable-storage-bools");
    array<bool3, 3> actual{};
    auto stream = device.create_stream();
    stream << bytes.upload(initial.data()) << shader(bytes, 1u).dispatch(1u)
           << bytes.download(actual.data()) << synchronize() << commit();
    bool passed = true;
    for (uint i = 0; i < initial.size(); ++i)
        for (uint j = 0; j < 3u; ++j) {
            const bool expected = i == 1u && j == 2u ? !initial[i][j] : initial[i][j];
            passed &= check(float(actual[i][j]), float(expected), "callable-storage-bool", i * 3u + j);
        }
    return passed;
}

bool callable_storage(Device &device) {
    const bool matrix_passed = callable_storage_matrix(device);
    const bool records_passed = callable_storage_records(device);
    const bool bools_passed = callable_storage_bools(device);
    return matrix_passed && records_passed && bools_passed;
}

bool callable_alias(Device &device) {
    auto output = device.create_buffer<float>(3u);
    Kernel kernel = [](BufferVar<float> out) {
        Callable mutate = [](Float &first, Float &second) {
            first += 1.f;
            second += first;
        };
        Float shared = 2.f;
        mutate(shared, shared);
        out.write(0u, shared);
        Float first = 2.f, second = 5.f;
        mutate(first, second);
        out.write(1u, first);
        out.write(2u, second);
    };
    auto shader = device.compile(kernel, "capability-callable-alias");
    array<float, 3> actual{};
    const array<float, 3> expected{6.f, 3.f, 8.f};
    auto stream = device.create_stream();
    stream << shader(output).dispatch(1u) << output.download(actual.data()) << synchronize() << commit();
    bool passed = true;
    for (uint i = 0u; i < 3u; ++i) passed &= check(actual[i], expected[i], "callable-alias", i);
    return passed;
}

bool fmod_mod(Device &device) {
    auto output = device.create_buffer<float4>(1u);
    Kernel kernel = [](BufferVar<float4> out, Float2 pair) {
        out.write(0u, make_float4(ocarina::fmod(pair.x, pair.y), ocarina::mod(pair.x, pair.y),
                                 ocarina::fmod(-pair.x, -pair.y), ocarina::mod(-pair.x, -pair.y)));
    };
    auto shader = device.compile(kernel, "capability-fmod-mod");
    array<float4, 1> actual{};
    const float a = -5.5f, b = 2.f;
    const float truncating = std::fmod(a, b), floored = a - b * std::floor(a / b);
    const array<float4, 1> expected{make_float4(truncating, floored, -truncating, -floored)};
    auto stream = device.create_stream();
    stream << shader(output, make_float2(a, b)).dispatch(1u)
           << output.download(actual.data()) << synchronize() << commit();
    bool passed = true;
    for (uint row = 0u; row < 1u; ++row)
        for (uint c = 0u; c < 4u; ++c) passed &= check(actual[row][c], expected[row][c], "fmod-mod", row * 4u + c);

    // Read binary16 operands from storage so neither backend can constant-fold
    // the subnormal conversions or the remainder operation.
    constexpr float unit = 0x1p-24f;
    const array<float2, 8> half_inputs{
        make_float2(unit, 3.f * unit), make_float2(-unit, 3.f * unit),
        make_float2(unit, -3.f * unit), make_float2(-unit, -3.f * unit),
        make_float2(5.5f, 2.f), make_float2(-5.5f, 2.f),
        make_float2(5.5f, -2.f), make_float2(-5.5f, -2.f)};
    array<half2, 8> encoded_inputs{};
    for (uint row = 0u; row < half_inputs.size(); ++row) {
        encoded_inputs[row] = make_half2(half(half_inputs[row].x), half(half_inputs[row].y));
    }
    auto half_input = device.create_buffer<half2>(encoded_inputs.size());
    auto half_output = device.create_buffer<uint2>(encoded_inputs.size());
    Kernel half_kernel = [](BufferVar<half2> in, BufferVar<uint2> out) {
        Uint index = dispatch_id();
        Half2 pair = in.read(index);
        Half truncating = ocarina::fmod(pair.x, pair.y);
        Half floored = ocarina::mod(pair.x, pair.y);
        out.write(index, make_uint2(as<uint>(cast<float>(truncating)), as<uint>(cast<float>(floored))));
    };
    auto half_shader = device.compile(half_kernel, "capability-half-fmod-mod");
    array<uint2, 8> half_actual{};
    stream << half_input.upload(encoded_inputs.data())
           << half_shader(half_input, half_output).dispatch(uint(encoded_inputs.size()))
           << half_output.download(half_actual.data()) << synchronize() << commit();
    auto round_half = [](float value) { return half_to_float(float_to_half(value)); };
    for (uint row = 0u; row < half_inputs.size(); ++row) {
        const float lhs = half_inputs[row].x, rhs = half_inputs[row].y;
        // CUDA fmod widens its half operands to float before fmodf. CUDA mod
        // uses half division, floor, multiplication and subtraction. These
        // operands have exact products/subtractions, avoiding contraction
        // ambiguity while distinguishing truncating and floor remainders.
        const float quotient = round_half(lhs / rhs);
        const float product = round_half(rhs * round_half(std::floor(quotient)));
        const float2 half_expected = make_float2(round_half(std::fmod(lhs, rhs)), round_half(lhs - product));
        for (uint c = 0u; c < 2u; ++c) {
            const uint expected_bits = bit_cast<uint>(half_expected[c]);
            const bool equal = half_actual[row][c] == expected_bits;
            std::cout << "half-fmod-mod row=" << row << " op=" << (c == 0u ? "fmod" : "mod")
                      << " input=" << lhs << " divisor=" << rhs
                      << " actual=" << bit_cast<float>(half_actual[row][c]) << " expected=" << half_expected[c]
                      << " actual_bits=" << half_actual[row][c] << " expected_bits=" << expected_bits
                      << " result=" << (equal ? "PASS" : "FAIL") << std::endl;
            passed &= equal;
        }
    }
    return passed;
}

bool ulong_makers(Device &device) {
    auto output = device.create_buffer<ulong>(9u);
    Kernel kernel = [](BufferVar<ulong> out, Ulong value) {
        auto pair = make_ulong2(value, value + ulong(1));
        auto triple = make_ulong3(pair, value + ulong(2));
        auto quad = make_ulong4(triple, value + ulong(3));
        out.write(0u, pair.x); out.write(1u, pair.y);
        out.write(2u, triple.x); out.write(3u, triple.y); out.write(4u, triple.z);
        out.write(5u, quad.x); out.write(6u, quad.y); out.write(7u, quad.z); out.write(8u, quad.w);
    };
    auto shader = device.compile(kernel, "capability-ulong-makers");
    const ulong input = (ulong(1) << 40u) + 9u;
    const array<ulong, 9> expected{input, input + 1, input, input + 1, input + 2, input, input + 1, input + 2, input + 3};
    array<ulong, 9> actual{};
    auto stream = device.create_stream();
    stream << shader(output, input).dispatch(1u) << output.download(actual.data()) << synchronize() << commit();
    bool passed = true;
    for (uint i = 0; i < actual.size(); ++i) {
        const bool equal = actual[i] == expected[i];
        std::cout << "ulong-makers index=" << i << " actual=" << actual[i] << " expected=" << expected[i]
                  << " result=" << (equal ? "PASS" : "FAIL") << std::endl;
        passed &= equal;
    }
    return passed;
}

}// namespace

int main(int argc, char **argv) {
    if (argc != 3 || std::string_view(argv[1]) != "--case") {
        std::cerr << "usage: --case inverse2|determinant2|callable-storage|callable-alias|fmod-mod|ulong-makers" << std::endl;
        return EXIT_FAILURE;
    }
    const std::string_view name = argv[2];
    bool (*probe)(Device &) = name == "inverse2" ? inverse2 : name == "callable-storage" ? callable_storage
        : name == "callable-alias" ? callable_alias : name == "fmod-mod" ? fmod_mod
        : name == "determinant2" ? determinant2 : name == "ulong-makers" ? ulong_makers : nullptr;
    if (!probe) {
        std::cerr << "unknown case: " << name << std::endl;
        return EXIT_FAILURE;
    }
    const char *backend = test_backend_name();
    std::cout << "case=" << name << " backend=" << backend << std::endl;
    auto device = RHIContext::instance().create_device(backend);
    const bool passed = probe(device);
    std::cout << "case=" << name << " result=" << (passed ? "PASS" : "FAIL") << std::endl;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
