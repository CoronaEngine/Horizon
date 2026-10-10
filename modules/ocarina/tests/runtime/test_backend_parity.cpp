#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"
#include <cmath>
#include <iostream>

using namespace ocarina;

struct ParityRecord {
    float tag;
    float3 position;
    array<float, 2> weights;
};
OC_STRUCT(, ParityRecord, tag, position, weights) {};

struct NestedRecord {
    uint tag;
    array<float3, 2> points;
    float3x3 transform;
};
OC_STRUCT(, NestedRecord, tag, points, transform) {};

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}
bool close(float a, float b) { return std::abs(a - b) < 1e-4f; }

void test_command_order_and_byte_fill(Device &device) {
    auto empty = device.create_buffer<uint>(0u, "empty-buffer");
    require(empty.handle() == 0, "empty buffer must use the null handle");
    auto bytes = device.create_buffer<uchar>(17u);
    vector<uchar> host(17u, 0x11u);
    Stream stream = device.create_stream();
    stream << bytes.upload(host.data())
           << BufferByteSetCommand::create(bytes.handle(), 13u, 0xabu)
           << bytes.download(host.data()) << synchronize() << commit();
    for (uint i = 0; i < 17u; ++i)
        require(host[i] == (i < 13u ? 0xabu : 0x11u), "byte fill corrupted its tail");
    stream << bytes.view(3u, 5u).byte_set(0x5cu) << bytes.download(host.data()) << synchronize() << commit();
    for (uint i = 0; i < 17u; ++i)
        require(host[i] == (i >= 3u && i < 8u ? 0x5cu : i < 13u ? 0xabu : 0x11u),
                "byte fill ignored its view offset");

    auto output = device.create_buffer<uint>(1u);
    Kernel kernel = [](BufferVar<uint> out, Uint value) { out.write(0u, value); };
    auto shader = device.compile(kernel, "backend-parity-command-order");
    array<uint, 7> results{};
    for (uint i = 0; i < results.size(); ++i)
        stream << shader(output, 101u + i).dispatch(1u) << output.download(&results[i]);
    stream << synchronize() << commit();
    for (uint i = 0; i < results.size(); ++i)
        require(results[i] == 101u + i, "queued dispatch reused another dispatch's arguments");
    bool callback_called = false;
    stream << synchronize() << commit([&](void *) { callback_called = true; });
    require(callback_called, "stream commit callback was ignored");
}

void test_views_and_record_layout(Device &device) {
    auto input = device.create_buffer<ParityRecord>(4u);
    vector<ParityRecord> records(4u);
    for (uint i = 0; i < records.size(); ++i)
        records[i] = {float(i + 1), make_float3(float(i + 2), float(i + 3), float(i + 4)),
                      {float(i + 5), float(i + 6)}};
    input.upload_immediately(records.data());
    auto output = device.create_buffer<float4>(2u);
    auto bindless = device.create_bindless_array();
    auto slot = bindless.emplace(input.view(1u, 2u));
    Stream stream = device.create_stream();
    stream << bindless.upload_handles();
    Kernel kernel = [&bindless, slot](BufferVar<ParityRecord> view, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Var<ParityRecord> a = view.read(i);
        Var<ParityRecord> b = bindless.buffer_var<ParityRecord>(slot).read(i);
        out.write(i, make_float4(a.tag, a.position.y, b.position.z, b.weights[1]));
    };
    auto shader = device.compile(kernel, "backend-parity-record-layout");
    array<float4, 2> host{};
    stream << shader(input.view(1u, 2u), output).dispatch(2u)
           << output.download(host.data()) << synchronize() << commit();
    for (uint i = 0; i < host.size(); ++i) {
        auto expected = records[i + 1];
        require(close(host[i].x, expected.tag) && close(host[i].y, expected.position.y) &&
                    close(host[i].z, expected.position.z) && close(host[i].w, expected.weights[1]),
                "buffer view or record layout differs from the host");
    }
    auto second_slot = bindless.emplace(input.view(2u, 1u));
    require(second_slot == 1u, "bindless insertion did not append");
    bindless->remove_buffer(slot);
    require(bindless.buffer_num() == 1u, "bindless removal did not compact slots");
    auto desc = bindless.impl()->buffer_view(0u);
    require(desc.offset == 2u * sizeof(ParityRecord), "bindless removal moved the wrong slot");
    ParityRecord selected{};
    stream << bindless.buffer_view<ParityRecord>(0u).download(&selected) << synchronize() << commit();
    require(close(selected.tag, records[2].tag) && close(selected.position.z, records[2].position.z),
            "transfer from a bindless buffer view ignored its interior device address");
}

void test_configured_dispatch(Device &device) {
    auto output = device.create_buffer<uint4>(17u * 9u);
    auto linear = device.create_buffer<uint>(17u * 9u);
    Kernel kernel = [](BufferVar<uint4> out, BufferVar<uint> linear) {
        out.write(dispatch_id(), make_uint4(block_idx().x, block_idx().y, thread_idx().x, thread_idx().y));
        linear.write(dispatch_id(), thread_id());
    };
    kernel.function()->configure(make_uint3(4u, 4u, 1u), make_uint3(8u, 4u, 1u));
    auto shader = device.compile(kernel, "backend-parity-configured-dispatch");
    vector<uint4> host(17u * 9u);
    vector<uint> linear_host(17u * 9u);
    Stream stream = device.create_stream();
    stream << shader(output, linear).dispatch(make_uint2(17u, 9u))
           << output.download(host.data()) << linear.download(linear_host.data()) << synchronize() << commit();
    for (uint y = 0; y < 9u; ++y)
        for (uint x = 0; x < 17u; ++x) {
            auto v = host[y * 17u + x];
            require(v.x == x / 8u && v.y == y / 4u && v.z == x % 8u && v.w == y % 4u,
                    "configured workgroup dimensions were ignored");
            require(linear_host[y * 17u + x] == 32u * (x / 8u + (y / 4u) * 4u) + (y % 4u) * 8u + x % 8u,
                    "thread id ignored the configured grid dimensions");
        }
}

void test_nested_storage_layout(Device &device) {
    array<NestedRecord, 2> records{};
    for (uint i = 0; i < records.size(); ++i) {
        records[i].tag = 7u + i;
        records[i].points = {make_float3(float(i + 1), 2.f, 3.f), make_float3(4.f, 5.f, float(i + 6))};
        records[i].transform = float3x3(make_float3(1.f, 2.f, 3.f), make_float3(4.f, 5.f, 6.f),
                                         make_float3(7.f, 8.f, float(i + 9)));
    }
    auto input = device.create_buffer<NestedRecord>(2u, "nested-layout-input");
    auto output = device.create_buffer<NestedRecord>(2u, "nested-layout-output");
    auto bindless = device.create_bindless_array();
    auto slot = bindless.emplace(input);
    Kernel kernel = [&bindless, slot](BufferVar<NestedRecord> in, BufferVar<NestedRecord> out) {
        Uint i = dispatch_id();
        Var<NestedRecord> record = bindless.buffer_var<NestedRecord>(slot).read(i);
        record.points[0] = in.read(i).points[1];
        out.write(i, record);
    };
    auto shader = device.compile(kernel, "backend-parity-nested-layout");
    array<NestedRecord, 2> host{};
    Stream stream = device.create_stream();
    stream << input.upload(records.data()) << bindless.upload_handles()
           << shader(input, output).dispatch(2u) << output.download(host.data()) << synchronize() << commit();
    for (uint i = 0; i < records.size(); ++i) {
        require(host[i].tag == records[i].tag, "nested record tag layout differs");
        for (uint j = 0; j < 3u; ++j) {
            require(close(host[i].points[0][j], records[i].points[1][j]) &&
                        close(host[i].points[1][j], records[i].points[1][j]),
                    "nested float3 array stride differs");
            for (uint k = 0; k < 3u; ++k)
                require(close(host[i].transform[j][k], records[i].transform[j][k]),
                        "nested matrix stride differs");
        }
    }
}

void test_matrix_algebra(Device &device) {
    float3x3 matrix(make_float3(2.f, 1.f, 0.f), make_float3(0.f, 3.f, 1.f), make_float3(1.f, 0.f, 4.f));
    auto output = device.create_buffer<float3>(3u);
    Kernel kernel = [](Var<float3x3> m, BufferVar<float3> out) {
        Float3 v = make_float3(2.f, 3.f, 5.f);
        out.write(0u, m * v);
        out.write(1u, inverse(m) * (m * v));
        out.write(2u, transpose(m) * v);
    };
    auto shader = device.compile(kernel, "backend-parity-matrix-algebra");
    array<float3, 3> host{};
    Stream stream = device.create_stream();
    stream << shader(matrix, output).dispatch(1u)
           << output.download(host.data()) << synchronize() << commit();
    auto v = make_float3(2.f, 3.f, 5.f);
    array<float3, 3> expected{matrix * v, v, transpose(matrix) * v};
    for (uint i = 0; i < host.size(); ++i)
        for (uint j = 0; j < 3u; ++j)
            require(close(host[i][j], expected[i][j]), "matrix algebra differs from the host");
}

void test_float_atomics(Device &device) {
    auto counter = device.create_buffer<float>(2u);
    array<float, 2> host{17.f, 0.f};
    counter.upload_immediately(host.data());
    Kernel kernel = [](BufferVar<float> value) { value.atomic(0u).fetch_add(0.5f); };
    auto shader = device.compile(kernel, "backend-parity-float-atomic");
    Stream stream = device.create_stream();
    stream << shader(counter.view(1u, 1u)).dispatch(128u)
           << counter.download(host.data()) << synchronize() << commit();
    require(close(host[0], 17.f) && close(host[1], 64.f), "float atomic or buffer view offset differs");
}

void test_texture_formats(Device &device) {
    auto bytes = device.create_texture2d(make_uint2(2u, 1u), PixelStorage::BYTE4);
    auto integers = device.create_texture3d(make_uint3(2u, 1u, 1u), PixelStorage::UINT4);
    auto output = device.create_buffer<float4>(2u);
    auto integer_output = device.create_buffer<uint4>(2u);
    Kernel kernel = [](Texture2DVar byte_tex, Texture3DVar uint_tex, BufferVar<float4> out, BufferVar<uint4> uint_out) {
        Uint i = dispatch_id();
        byte_tex.write(make_float4(0.25f, 0.5f, 0.75f, 1.f), make_uint2(i, 0u));
        uint_tex.write(make_uint4(0xf0000000u + i, 2u, 3u, 4u), make_uint3(i, 0u, 0u));
        out.write(i, byte_tex.read<float4>(make_uint2(i, 0u)));
        uint_out.write(i, uint_tex.read<uint4>(make_uint3(i, 0u, 0u)));
    };
    auto shader = device.compile(kernel, "backend-parity-texture-formats");
    array<float4, 2> samples{};
    array<uint4, 2> uint_pixels{};
    array<uint4, 2> uint_read{};
    Stream stream = device.create_stream();
    stream << shader(bytes, integers, output, integer_output).dispatch(2u)
           << output.download(samples.data()) << integers.download(uint_pixels.data())
           << integer_output.download(uint_read.data())
           << synchronize() << commit();
    for (uint i = 0; i < 2u; ++i) {
        require(close(samples[i].x, 63.f / 255.f) && close(samples[i].y, 127.f / 255.f) &&
                    close(samples[i].z, 191.f / 255.f) && close(samples[i].w, 1.f),
                "normalized byte texture conversion differs");
        require(uint_pixels[i].x == 0xf0000000u + i && uint_pixels[i].y == 2u &&
                    uint_pixels[i].z == 3u && uint_pixels[i].w == 4u,
                "integer texture lost bits or the depth-one 3D view is invalid");
        require(uint_read[i].x == uint_pixels[i].x && uint_read[i].y == uint_pixels[i].y &&
                    uint_read[i].z == uint_pixels[i].z && uint_read[i].w == uint_pixels[i].w,
                "integer surface read differs from host download");
    }
}

void test_nonuniform_bindless_textures(Device &device) {
    auto a = device.create_texture2d(make_uint2(1u, 1u), PixelStorage::FLOAT4);
    auto b = device.create_texture2d(make_uint2(2u, 1u), PixelStorage::FLOAT4);
    float4 color_a = make_float4(1.f, 2.f, 3.f, 4.f);
    array<float4, 2> color_b{make_float4(5.f, 6.f, 7.f, 8.f), make_float4(5.f, 6.f, 7.f, 8.f)};
    auto bindless = device.create_bindless_array();
    bindless.emplace(a);
    bindless.emplace(b);
    auto output = device.create_buffer<float4>(64u, "nonuniform-texture-output");
    Kernel kernel = [&bindless](BufferVar<float4> out) {
        Uint i = dispatch_id();
        auto sampled = bindless.tex2d_var(i % 2u).sample(4u, make_float2(0.5f));
        out.write(i, make_float4(sampled[0], sampled[1], sampled[2], sampled[3]));
    };
    auto shader = device.compile(kernel, "backend-parity-nonuniform-textures");
    array<float4, 64> host{};
    Stream stream = device.create_stream();
    stream << a.upload(&color_a) << b.upload(color_b.data()) << bindless.upload_handles()
           << shader(output).dispatch(64u) << output.download(host.data()) << synchronize() << commit();
    for (uint i = 0; i < host.size(); ++i)
        for (uint j = 0; j < 4u; ++j)
            require(close(host[i][j], (i % 2u ? color_b[0] : color_a)[j]),
                    "nonuniform bindless texture selection differs");
}

void test_diagnostics_lifetime() {
    for (uint i = 0; i < 2u; ++i) {
        {
            auto device = RHIContext::instance().create_device(test_backend_name());
            Env::printer().init(device, 64u);
            Env::debugger().init(device);
        }
        require(!Env::printer().buffer().valid(),
                "global printer retained a resource from a destroyed device");
    }
}
}

int main() {
    auto device = RHIContext::instance().create_device(test_backend_name());
    test_command_order_and_byte_fill(device);
    test_views_and_record_layout(device);
    test_configured_dispatch(device);
    test_nested_storage_layout(device);
    test_matrix_algebra(device);
    test_float_atomics(device);
    test_texture_formats(device);
    test_nonuniform_bindless_textures(device);
    test_diagnostics_lifetime();
    std::cout << "backend parity regression checks passed" << std::endl;
}
