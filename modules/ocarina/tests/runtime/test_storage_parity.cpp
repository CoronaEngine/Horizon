#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"
#include <cmath>
#include <bit>
#include <cstring>
#include <cstdlib>
#include <iostream>

using namespace ocarina;

struct StorageRecord {
    uint tag;
    array<float3, 2> points;
    float3x3 transform;
};
OC_STRUCT(, StorageRecord, tag, points, transform) {};

struct VectorStorageRecord {
    uint tag;
    float4 color;
    float2 uv;
    uint enabled;
    array<float, 3> point;
    uint guard;
};
OC_STRUCT(, VectorStorageRecord, tag, color, uv, enabled, point, guard) {};

struct NestedVectorPrefix {
    uint tag;
    float value;
    uint2 unsigned_pair;
};
OC_STRUCT(, NestedVectorPrefix, tag, value, unsigned_pair) {};
struct NestedIntegerStorageRecord {
    NestedVectorPrefix prefix;
    int2 signed_pair;
    uint2 unsigned_pair;
    int4 signed_quad;
    uint4 unsigned_quad;
};
OC_STRUCT(, NestedIntegerStorageRecord, prefix, signed_pair, unsigned_pair, signed_quad, unsigned_quad) {};
static_assert(sizeof(NestedIntegerStorageRecord) == 64u);

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void test_nested_writes(Device &device) {
    constexpr uint count = 6;
    array<StorageRecord, count> initial{};
    for (uint i = 0; i < count; ++i) {
        initial[i].tag = 10u + i;
        initial[i].points = {make_float3(1.f, 2.f, 3.f), make_float3(4.f, 5.f, 6.f)};
        initial[i].transform = float3x3(make_float3(11.f, 12.f, 13.f), make_float3(14.f, 15.f, 16.f),
                                           make_float3(17.f, 18.f, 19.f));
    }
    auto typed = device.create_buffer<StorageRecord>(count);
    auto bytes = device.create_byte_buffer(sizeof(initial));
    auto indirect = device.create_byte_buffer(sizeof(initial));
    auto bindless = device.create_bindless_array();
    auto slot = bindless.emplace(indirect.view(sizeof(StorageRecord), 4u * sizeof(StorageRecord)));
    Kernel kernel = [&bindless, slot](BufferVar<StorageRecord> out, ByteBufferVar raw) {
        Uint i = dispatch_id();
        Uint point = i % 2u;
        Uint column = i % 3u;
        Uint row = (i + 1u) % 3u;
        Uint offset = i * uint(sizeof(StorageRecord));
        out[i].tag = 100u + i;
        out[i].points[point].y = cast<float>(20u + i);
        out[i].points[point].xz() = out[i].points[point].zx();
        out[i].transform[column][row] = cast<float>(30u + i);
        raw.load_as<StorageRecord>(offset).tag = 100u + i;
        raw.load_as<StorageRecord>(offset).points[point].y = cast<float>(20u + i);
        raw.load_as<StorageRecord>(offset).points[point].xz() = raw.load_as<StorageRecord>(offset).points[point].zx();
        raw.load_as<StorageRecord>(offset).transform[column][row] = cast<float>(30u + i);
        auto buffer = bindless.byte_buffer_var(slot);
        buffer.load_as<StorageRecord>(offset).tag = 100u + i;
        buffer.load_as<StorageRecord>(offset).points[point].y = cast<float>(20u + i);
        buffer.load_as<StorageRecord>(offset).points[point].xz() = buffer.load_as<StorageRecord>(offset).points[point].zx();
        buffer.load_as<StorageRecord>(offset).transform[column][row] = cast<float>(30u + i);
    };
    auto shader = device.compile(kernel, "storage-parity-nested-writes");
    array<StorageRecord, count> typed_host{}, bytes_host{}, indirect_host{};
    Stream stream = device.create_stream();
    stream << typed.upload(initial.data()) << bytes.upload(initial.data()) << indirect.upload(initial.data())
           << bindless.upload_handles()
           << shader(typed.view(1u, 4u), bytes.view(sizeof(StorageRecord), 4u * sizeof(StorageRecord))).dispatch(4u)
           << typed.download(typed_host.data()) << bytes.download(bytes_host.data())
           << indirect.download(indirect_host.data()) << synchronize() << commit();
    auto expected = initial;
    for (uint i = 0; i < 4u; ++i) {
        expected[i + 1].tag = 100u + i;
        expected[i + 1].points[i % 2].y = float(20u + i);
        std::swap(expected[i + 1].points[i % 2].x, expected[i + 1].points[i % 2].z);
        expected[i + 1].transform[i % 3][(i + 1) % 3] = float(30u + i);
    }
    for (const auto *values : {typed_host.data(), bytes_host.data(), indirect_host.data()}) {
        for (uint i = 0; i < count; ++i) {
            if (values[i].tag != expected[i].tag) {
                const char *kind = values == typed_host.data() ? "typed" : values == bytes_host.data() ? "byte" : "bindless";
                std::cerr << "nested " << kind << " record=" << i << " tag=" << values[i].tag << " expected=" << expected[i].tag << std::endl;
            }
            require(values[i].tag == expected[i].tag, "nested scalar assignment or view offset differs");
            for (uint j = 0; j < 2; ++j)
                for (uint k = 0; k < 3; ++k)
                    require(values[i].points[j][k] == expected[i].points[j][k], "nested vector assignment changed another member");
            for (uint j = 0; j < 3; ++j)
                for (uint k = 0; k < 3; ++k)
                    require(values[i].transform[j][k] == expected[i].transform[j][k], "nested matrix assignment used the wrong column stride");
        }
    }
}

void test_vector_record_copies(Device &device) {
    constexpr uint count = 4u;
    array<VectorStorageRecord, count> source{}, destination{}, expected{};
    std::memset(source.data(), 0xa5, sizeof(source));
    std::memset(destination.data(), 0xcd, sizeof(destination));
    std::memset(expected.data(), 0xcd, sizeof(expected));
    constexpr uint bits[]{0x00000000u, 0x80000000u, 0x7fc12345u, 0x00000001u};
    for (uint i = 0; i < count; ++i) {
        source[i].tag = 100u + i;
        for (uint j = 0; j < 4u; ++j) source[i].color[j] = std::bit_cast<float>(bits[(i + j) % 4u]);
        source[i].uv = make_float2(float(i) + .25f, -float(i) - .5f);
        source[i].enabled = i % 2u;
        source[i].point = {float(i) + 1.f, float(i) + 2.f, float(i) + 3.f};
        source[i].guard = 0x12345678u + i;
        expected[i].tag = source[i].tag;
        for (uint j = 0; j < 4u; ++j) expected[i].color[j] = source[i].color[j];
        for (uint j = 0; j < 2u; ++j) expected[i].uv[j] = source[i].uv[j];
        expected[i].enabled = source[i].enabled;
        for (uint j = 0; j < 3u; ++j) expected[i].point[j] = source[i].point[j];
        expected[i].guard = source[i].guard;
    }
    auto typed_source = device.create_buffer<VectorStorageRecord>(count);
    auto typed_destination = device.create_buffer<VectorStorageRecord>(count);
    Kernel typed_kernel = [](BufferVar<VectorStorageRecord> in, BufferVar<VectorStorageRecord> out) {
        out.write(dispatch_id(), in.read(dispatch_id()));
    };
    auto typed_shader = device.compile(typed_kernel, "storage-parity-native-vector-record");
    array<VectorStorageRecord, count> typed_host{};
    Stream stream = device.create_stream();
    stream << typed_source.upload(source.data()) << typed_destination.upload(destination.data())
           << typed_shader(typed_source, typed_destination).dispatch(count)
           << typed_destination.download(typed_host.data()) << synchronize() << commit();
    require(std::memcmp(typed_host.data(), expected.data(), sizeof(expected)) == 0,
            "native vector record copy changed FP32 bits, padding or scalar fields");

    // A byte view may contain the same type at a root that is only word aligned.
    // Both direct and bindless accesses must preserve the original scalar path.
    for (uint prefix : {4u, 8u, 16u}) {
        const uint byte_count = prefix + uint(sizeof(source)) + 16u;
        vector<uchar> input(byte_count, 0xa5u), output(byte_count, 0xcdu), expected_bytes(byte_count, 0xcdu);
        std::memcpy(input.data() + prefix, source.data(), sizeof(source));
        std::memcpy(expected_bytes.data() + prefix, expected.data(), sizeof(expected));
        auto bytes_source = device.create_byte_buffer(byte_count);
        auto bytes_destination = device.create_byte_buffer(byte_count);
        auto indirect_destination = device.create_byte_buffer(byte_count);
        auto bindless = device.create_bindless_array();
        auto source_slot = bindless.emplace(bytes_source.view(prefix, sizeof(source)));
        auto destination_slot = bindless.emplace(indirect_destination.view(prefix, sizeof(source)));
        Kernel bytes_kernel = [&bindless, source_slot, destination_slot](ByteBufferVar in, ByteBufferVar out) {
            Uint offset = dispatch_id() * uint(sizeof(VectorStorageRecord));
            out.store(offset, in.load_as<VectorStorageRecord>(offset));
            bindless.byte_buffer_var(destination_slot).store(offset,
                bindless.byte_buffer_var(source_slot).load_as<VectorStorageRecord>(offset));
        };
        auto bytes_shader = device.compile(bytes_kernel, "storage-parity-vector-record-byte-root-" + std::to_string(prefix));
        vector<uchar> direct_host(byte_count), indirect_host(byte_count);
        stream << bytes_source.upload(input.data()) << bytes_destination.upload(output.data())
               << indirect_destination.upload(output.data()) << bindless.upload_handles()
               << bytes_shader(bytes_source.view(prefix, sizeof(source)), bytes_destination.view(prefix, sizeof(source))).dispatch(count)
               << bytes_destination.download(direct_host.data()) << indirect_destination.download(indirect_host.data())
               << synchronize() << commit();
        require(direct_host == expected_bytes && indirect_host == expected_bytes,
                "vector record byte copy changed unaligned fields, padding, sentinels or FP32 payloads");
    }
}

void test_nested_integer_vector_copies(Device &device) {
    constexpr uint count = 4u;
    array<NestedIntegerStorageRecord, count> initial{}, host{};
    for (uint i = 0u; i < count; ++i) {
        initial[i].prefix.tag = 0xfedc0000u + i;
        initial[i].prefix.value = std::bit_cast<float>(0x7fc12345u + i);
        initial[i].prefix.unsigned_pair = make_uint2(0x80000000u + i, 0xffffffffu - i);
        initial[i].signed_pair = make_int2(std::bit_cast<int>(0x80000000u + i), -int(i) - 1);
        initial[i].unsigned_pair = make_uint2(i, 0x7fffffffu - i);
        initial[i].signed_quad = make_int4(-1, 0, 2147483647, std::bit_cast<int>(0x80000000u));
        initial[i].unsigned_quad = make_uint4(0u, 0xffffffffu, 0x80000000u + i, 0x7fffffffu - i);
    }
    auto input = device.create_buffer<NestedIntegerStorageRecord>(count);
    auto output = device.create_buffer<NestedIntegerStorageRecord>(count);
    Kernel kernel = [](BufferVar<NestedIntegerStorageRecord> in, BufferVar<NestedIntegerStorageRecord> out) {
        out.write(dispatch_id(), in.read(dispatch_id()));
    };
    auto shader = device.compile(kernel, "storage-parity-nested-native-integer-vectors");
    Stream stream = device.create_stream();
    stream << input.upload(initial.data()) << shader(input, output).dispatch(count)
           << output.download(host.data()) << synchronize() << commit();
    require(std::memcmp(host.data(), initial.data(), sizeof(initial)) == 0,
            "nested vector copying changed NaN payloads, nested offsets or int/uint vector bits");
}

template<uint N>
void test_bool_vectors(Device &device) {
    using Value = Vector<bool, N>;
    array<Value, 6> initial{};
    for (uint i = 0; i < initial.size(); ++i)
        for (uint j = 0; j < N; ++j) initial[i][j] = ((i + j) % 2u) != 0;
    auto input = device.create_byte_buffer(sizeof(initial));
    auto output = device.create_byte_buffer(sizeof(initial));
    auto raw = device.create_byte_buffer(sizeof(initial));
    auto bindless = device.create_bindless_array();
    auto slot = bindless.emplace(input.view(sizeof(Value), 4u * sizeof(Value)));
    Kernel kernel = [&bindless, slot](ByteBufferVar src, ByteBufferVar dst, ByteBufferVar bytes) {
        Uint i = dispatch_id();
        Uint offset = i * uint(sizeof(Value));
        dst.store(offset, src.load_as<Value>(offset));
        dst.load_as<Value>(offset)[i % N] = true;
        bytes.store(offset, bindless.byte_buffer_var(slot).load_as<Value>(offset));
        bytes.load_as<Value>(i * uint(sizeof(Value)))[i % N] = true;
    };
    auto shader = device.compile(kernel, "storage-parity-bool" + std::to_string(N));
    array<Value, 6> typed_host{}, raw_host{};
    Stream stream = device.create_stream();
    stream << input.upload(initial.data()) << output.upload(initial.data()) << raw.upload(initial.data())
           << bindless.upload_handles()
           << shader(input.view(sizeof(Value), 4u * sizeof(Value)), output.view(sizeof(Value), 4u * sizeof(Value)), raw.view(sizeof(Value), 4u * sizeof(Value))).dispatch(4u)
           << output.download(typed_host.data()) << raw.download(raw_host.data()) << synchronize() << commit();
    for (uint i = 0; i < initial.size(); ++i)
        for (uint j = 0; j < N; ++j) {
            bool expected = (i >= 1u && i <= 4u && j == (i - 1u) % N) || initial[i][j];
            require(typed_host[i][j] == expected && raw_host[i][j] == expected,
                    "bool vector storage differs from its byte-sized host layout");
        }
}

void test_half_vectors(Device &device) {
    array<half3, 6> halves{};
    for (uint i = 0; i < 6; ++i) {
        halves[i] = make_half3(half(1.f), half(2.f), half(3.f));
    }
    auto h = device.create_buffer<half3>(6u);
    Kernel kernel = [](BufferVar<half3> h) {
        Uint i = dispatch_id();
        h[i][i % 3u] = cast<half>(cast<float>(i) + 0.5f);
    };
    auto shader = device.compile(kernel, "storage-parity-half-vectors");
    Stream stream = device.create_stream();
    stream << h.upload(halves.data()) << shader(h.view(1u, 4u)).dispatch(4u)
           << h.download(halves.data())
           << synchronize() << commit();
    for (uint i = 0; i < 6; ++i)
        for (uint j = 0; j < 3; ++j) {
            bool changed = i >= 1u && i <= 4u && j == (i - 1u) % 3u;
            require(float(halves[i][j]) == (changed ? float(i - 1u) + .5f : float(j + 1u)), "half3 sub-element store changed adjacent values");
        }
}

void test_math_edges(Device &device) {
    array<float, 8> inputs{0.f, -0.f, .5f, -.5f, 1.5f, -1.5f, 2.5f, -2.5f};
    auto input = device.create_buffer<float>(inputs.size());
    auto output = device.create_buffer<float4>(inputs.size() + 1u);
    auto integers = device.create_buffer<uint4>(2u);
    Kernel kernel = [](BufferVar<float> in, BufferVar<float4> out, BufferVar<uint4> integers) {
        Uint i = dispatch_id();
        Float value = in.read(i);
        Half half_value = cast<half>(value);
        out.write(i, make_float4(sign(value), round(value), cast<float>(sign(half_value)), cast<float>(round(half_value))));
        $if(i == 0u) {
            out.write(8u, make_float4(fma(Float(1.000244140625f), Float(.999755859375f), Float(-1.f)),
                                    cast<float>(fma(Half(1.015625f), Half(.984375f), Half(-1.f))),
                                    cast<float>(sign(Int(0))), round(Float(8388609.f))));
            integers.write(0u, make_uint4(cast<uint>(sqr(Int(4097))), sqr(Uint(65535)),
                                          cast<uint>(sqr(Int(-4097))), sqr(Uint(4099))));
            integers.write(1u, sqr(make_uint4(Uint(4097u), Uint(65535u), Uint(4099u), Uint(65533u))));
        };
    };
    auto shader = device.compile(kernel, "storage-parity-math-edges");
    array<float4, 9> host{};
    array<uint4, 2> integer_host{};
    Stream stream = device.create_stream();
    stream << input.upload(inputs.data()) << shader(input, output, integers).dispatch(uint(inputs.size()))
           << integers.download(integer_host.data()) << output.download(host.data()) << synchronize() << commit();
    for (uint i = 0; i < inputs.size(); ++i) {
        float expected_sign = inputs[i] >= 0.f ? 1.f : -1.f;
        require(host[i].x == expected_sign && host[i].z == expected_sign, "sign of signed zero differs from DSL");
        require(host[i].y == std::round(inputs[i]) && host[i].w == std::round(inputs[i]), "round must round halfway values away from zero");
        if (inputs[i] == 0.f)
            require(std::signbit(host[i].y) == std::signbit(inputs[i]), "round lost signed zero");
    }
    require(host[8].x == -0.000000059604644775390625f && host[8].y == -0.000244140625f,
            "fma did not preserve fused multiplication and addition");
    require(host[8].z == 1.f && host[8].w == 8388609.f, "integer sign or large floating-point round differs");
    array<uint4, 2> expected_integers{make_uint4(4097u * 4097u, 65535u * 65535u, 4097u * 4097u, 4099u * 4099u),
                                    make_uint4(4097u * 4097u, 65535u * 65535u, 4099u * 4099u, 65533u * 65533u)};
    for (uint i = 0; i < 2; ++i)
        for (uint j = 0; j < 4; ++j)
            require(integer_host[i][j] == expected_integers[i][j], "integer square lost precision by converting through float");
}

void test_64bit_buffer_metadata(Device &device) {
    constexpr ulong large_size = (ulong(1) << 32u) + 17u;
    auto backing = device.create_buffer<uint>(1u);
    auto output = device.create_buffer<ulong>(2u);
    // These synthetic views exercise descriptor metadata only. No shader load or
    // store addresses the backing allocation through either large view.
    BufferView<uint> typed_view(backing.handle(), large_size);
    ByteBufferView byte_view(backing.handle(), large_size);
    Kernel kernel = [](BufferVar<uint> in, ByteBufferVar bytes, BufferVar<ulong> out) {
        out.write(0u, in.size<ulong>());
        out.write(1u, bytes.size<ulong>());
    };
    auto shader = device.compile(kernel, "storage-parity-64bit-metadata");
    array<ulong, 2> host{};
    Stream stream = device.create_stream();
    stream << shader(typed_view, byte_view, output).dispatch(1u)
           << output.download(host.data()) << synchronize() << commit();
    require(host[0] == large_size && host[1] == large_size, "buffer size was truncated to 32 bits");
}

void test_global_block_barrier(Device &device) {
    constexpr uint count = 256;
    auto scratch = device.create_buffer<uint>(count);
    auto output = device.create_buffer<uint>(count);
    Kernel kernel = [](BufferVar<uint> scratch, BufferVar<uint> out) {
        Uint i = dispatch_id();
        scratch.write(i, i + 100u);
        synchronize_block();
        out.write(i, scratch.read((i / 64u) * 64u + (i + 1u) % 64u));
    };
    kernel.function()->configure(make_uint3(4u, 1u, 1u), make_uint3(64u, 1u, 1u));
    auto shader = device.compile(kernel, "storage-parity-global-block-barrier");
    array<uint, count> host{};
    Stream stream = device.create_stream();
    stream << shader(scratch, output).dispatch(count) << output.download(host.data()) << synchronize() << commit();
    for (uint i = 0; i < count; ++i)
        require(host[i] == (i / 64u) * 64u + (i + 1u) % 64u + 100u,
                "block barrier did not make global buffer writes visible to other lanes");
}
}

int main() {
    auto device = create_test_device();
    test_nested_writes(device);
    test_vector_record_copies(device);
    test_nested_integer_vector_copies(device);
    test_bool_vectors<2>(device);
    test_bool_vectors<3>(device);
    test_bool_vectors<4>(device);
    test_half_vectors(device);
    test_math_edges(device);
    test_64bit_buffer_metadata(device);
    test_global_block_barrier(device);
    std::cout << "storage and math parity checks passed" << std::endl;
}
