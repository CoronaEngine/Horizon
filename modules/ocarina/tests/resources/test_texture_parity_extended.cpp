#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <type_traits>

using namespace ocarina;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "texture parity: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

template<typename Pixel>
auto &component(Pixel &pixel, uint channel) {
    if constexpr (type_dimension_v<Pixel> == 1u) return pixel;
    else return pixel[channel];
}

template<typename Pixel>
auto component(const Pixel &pixel, uint channel) {
    if constexpr (type_dimension_v<Pixel> == 1u) return pixel;
    else return pixel[channel];
}

template<uint Dimension>
auto make_texture(Device &device, uint3 extent, PixelStorage storage) {
    if constexpr (Dimension == 2u)
        return device.create_texture2d(make_uint2(extent.x, extent.y), storage);
    else
        return device.create_texture3d(extent, storage);
}

template<typename Pixel, uint Dimension>
void test_surface_bounds(Device &device) {
    std::cout << "surface bounds " << Dimension << "D format=" << uint(PixelStorageImpl<Pixel>::storage) << std::endl;
    constexpr uint channels = type_dimension_v<Pixel>;
    constexpr bool normalized = std::is_same_v<type_element_t<Pixel>, uchar>;
    using FloatPixel = std::conditional_t<channels == 1u, float,
                       std::conditional_t<channels == 2u, float2, float4>>;
    using Value = std::conditional_t<normalized, FloatPixel, Pixel>;
    constexpr uint3 extent = make_uint3(3u, 2u, Dimension == 2u ? 1u : 2u);
    constexpr uint count = extent.x * extent.y * extent.z;
    vector<Pixel> pixels(count), cleared(count), downloaded(count);
    vector<uint3> coordinates;
    for (uint z = 0; z < extent.z; ++z)
        for (uint y = 0; y < extent.y; ++y)
            for (uint x = 0; x < extent.x; ++x) {
                uint index = uint(coordinates.size());
                coordinates.push_back(make_uint3(x, y, z));
                for (uint channel = 0; channel < channels; ++channel)
                    component(pixels[index], channel) = type_element_t<Pixel>(11u + index * 7u + channel);
            }
    coordinates.push_back(make_uint3(extent.x, 0u, 0u));
    coordinates.push_back(make_uint3(0u, extent.y, 0u));
    coordinates.push_back(make_uint3(0xffffffffu, 1u, 0u));
    coordinates.push_back(make_uint3(1u, 0xffffffffu, 0u));
    if constexpr (Dimension == 3u) coordinates.push_back(make_uint3(0u, 0u, extent.z));

    auto source = make_texture<Dimension>(device, extent, PixelStorageImpl<Pixel>::storage);
    auto target = make_texture<Dimension>(device, extent, PixelStorageImpl<Pixel>::storage);
    auto positions = device.create_buffer<uint3>(coordinates.size());
    auto result = device.create_buffer<Value>(coordinates.size());
    const uint outside_count = uint(coordinates.size()) - count;
    auto outside_result = device.create_buffer<Value>(outside_count);
    using TextureVar = std::conditional_t<Dimension == 2u, Texture2DVar, Texture3DVar>;
    Kernel kernel = [](TextureVar input, TextureVar output, BufferVar<uint3> positions, BufferVar<Value> values) {
        Uint i = dispatch_id();
        Uint3 p = positions.read(i);
        if constexpr (Dimension == 2u) {
            Var<Value> value = input.template read<Value>(p.x, p.y);
            values.write(i, value);
            output.write(value, p.x, p.y);
        } else {
            Var<Value> value = input.template read<Value>(p.x, p.y, p.z);
            values.write(i, value);
            output.write(value, p.x, p.y, p.z);
        }
    };
    auto shader = device.compile(kernel, "texture-parity-surface-bounds");
    auto stream = device.create_stream();
    vector<Value> actual(coordinates.size());
    vector<Value> outside_actual(outside_count);
    stream << source.upload(pixels.data()) << target.upload(cleared.data())
           << positions.upload(coordinates.data())
           << shader(source, target, positions, result).dispatch(uint(coordinates.size()))
           << result.download(actual.data())
           << shader(source, target, positions.view(count, outside_count), outside_result).dispatch(outside_count)
           << outside_result.download(outside_actual.data()) << target.download(downloaded.data())
           << synchronize() << commit();
    for (uint i = 0; i < coordinates.size(); ++i)
        for (uint channel = 0; channel < channels; ++channel) {
            double expected = i < count ? double(component(pixels[i], channel)) : 0.0;
            if constexpr (normalized) expected /= 255.0;
            require(std::abs(double(component(actual[i], channel)) - expected) <= 1e-6,
                    "surface read must return zero outside every dimension");
        }
    for (const auto &pixel : outside_actual)
        for (uint channel = 0; channel < channels; ++channel)
            require(component(pixel, channel) == 0, "out-of-bounds-only dispatch must read zero");
    for (uint i = 0; i < count; ++i)
        for (uint channel = 0; channel < channels; ++channel)
            require(component(downloaded[i], channel) == component(pixels[i], channel),
                    "out-of-bounds surface writes must leave valid pixels intact");
}

float mirrored(float coordinate) {
    float period = coordinate - std::floor(coordinate * 0.5f) * 2.f;
    return period <= 1.f ? period : 2.f - period;
}

template<typename Pixel>
float reference_sample(const vector<Pixel> &pixels, uint3 extent, float3 uv, uint channel) {
    float3 position = make_float3(mirrored(uv.x) * float(extent.x) - 0.5f,
                                  mirrored(uv.y) * float(extent.y) - 0.5f,
                                  mirrored(uv.z) * float(extent.z) - 0.5f);
    int3 low = make_int3(int(std::floor(position.x)), int(std::floor(position.y)), int(std::floor(position.z)));
    float3 fraction = position - make_float3(float(low.x), float(low.y), float(low.z));
    float result = 0.f;
    for (uint dz = 0; dz < 2u; ++dz)
        for (uint dy = 0; dy < 2u; ++dy)
            for (uint dx = 0; dx < 2u; ++dx) {
                uint x = uint(std::clamp(low.x + int(dx), 0, int(extent.x) - 1));
                uint y = uint(std::clamp(low.y + int(dy), 0, int(extent.y) - 1));
                uint z = uint(std::clamp(low.z + int(dz), 0, int(extent.z) - 1));
                float value = float(component(pixels[(z * extent.y + y) * extent.x + x], channel));
                if constexpr (std::is_same_v<type_element_t<Pixel>, uchar>) value /= 255.f;
                result += value * (dx ? fraction.x : 1.f - fraction.x) *
                          (dy ? fraction.y : 1.f - fraction.y) * (dz ? fraction.z : 1.f - fraction.z);
            }
    return result;
}

template<typename Pixel, uint Dimension>
void test_sampling_and_bindless_updates(Device &device) {
    std::cout << "mirror sampling " << Dimension << "D format=" << uint(PixelStorageImpl<Pixel>::storage) << std::endl;
    constexpr uint channels = type_dimension_v<Pixel>;
    constexpr uint3 extent = make_uint3(4u, 2u, Dimension == 2u ? 1u : 2u);
    constexpr uint count = extent.x * extent.y * extent.z;
    vector<Pixel> a(count), b(count);
    for (uint i = 0; i < count; ++i)
        for (uint channel = 0; channel < channels; ++channel) {
            if constexpr (std::is_same_v<type_element_t<Pixel>, uchar>) {
                component(a[i], channel) = uchar((i * 29u + channel * 47u) % 256u);
                component(b[i], channel) = uchar(255u - component(a[i], channel));
            } else {
                component(a[i], channel) = float(i * 3u + channel) / 16.f - 1.f;
                component(b[i], channel) = 2.f - component(a[i], channel);
            }
        }
    // Dyadic fractions make interpolation weights exactly representable by
    // DSL's texture units. These include texel centers, edges and mirror seams.
    array<float, 14> axes{-2.125f, -1.125f, -1.f, -0.125f, 0.f, 0.125f, 0.25f,
                          0.5f, 0.875f, 1.f, 1.125f, 1.75f, 2.f, 2.125f};
    vector<float3> coordinates;
    for (uint i = 0; i < axes.size(); ++i)
        coordinates.push_back(make_float3(axes[i], axes[(i * 3u + 2u) % axes.size()],
                                          Dimension == 2u ? 0.5f : axes[(i * 5u + 1u) % axes.size()]));
    auto first = make_texture<Dimension>(device, extent, PixelStorageImpl<Pixel>::storage);
    auto second = make_texture<Dimension>(device, extent, PixelStorageImpl<Pixel>::storage);
    auto bindless = device.create_bindless_array();
    require(bindless.emplace(first) == 0u && bindless.emplace(second) == 1u,
            "texture slots must append in order");
    auto positions = device.create_buffer<float3>(coordinates.size());
    auto result = device.create_buffer<float4>(coordinates.size() * 2u);
    using TextureVar = std::conditional_t<Dimension == 2u, Texture2DVar, Texture3DVar>;
    Kernel kernel = [&bindless](TextureVar direct, BufferVar<float3> positions, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Float3 p = positions.read(i);
        Float4 direct_value = make_float4(0.f);
        Float4 bindless_value = make_float4(0.f);
        if constexpr (Dimension == 2u) {
            auto sampled = direct.sample(channels, p.x, p.y);
            auto dynamic = bindless.tex2d_var(i % 2u).sample(channels, p.x, p.y);
            for (uint channel = 0; channel < channels; ++channel) {
                direct_value[channel] = sampled[channel];
                bindless_value[channel] = dynamic[channel];
            }
        } else {
            auto sampled = direct.sample(channels, p.x, p.y, p.z);
            auto dynamic = bindless.tex3d_var(i % 2u).sample(channels, p.x, p.y, p.z);
            for (uint channel = 0; channel < channels; ++channel) {
                direct_value[channel] = sampled[channel];
                bindless_value[channel] = dynamic[channel];
            }
        }
        out.write(i * 2u, direct_value);
        out.write(i * 2u + 1u, bindless_value);
    };
    auto shader = device.compile(kernel, "texture-parity-mirror-linear-bindless");
    auto stream = device.create_stream();
    vector<float4> actual(coordinates.size() * 2u);
    stream << first.upload(a.data()) << second.upload(b.data()) << positions.upload(coordinates.data());
    for (uint phase = 0; phase < 3u; ++phase) {
        if (phase == 1u) {
            if constexpr (Dimension == 2u) bindless.set_texture2d(0u, second);
            else bindless.set_texture3d(0u, second);
        } else if (phase == 2u) {
            if constexpr (Dimension == 2u) bindless->remove_texture2d(0u);
            else bindless->remove_texture3d(0u);
            require(bindless.emplace(first) == 1u, "removed texture slots must compact before append");
        }
        stream << bindless.upload_handles() << shader(first, positions, result).dispatch(uint(coordinates.size()))
               << result.download(actual.data()) << synchronize() << commit();
        for (uint i = 0; i < coordinates.size(); ++i)
            for (uint channel = 0; channel < channels; ++channel) {
                bool use_a = phase == 0u ? i % 2u == 0u : phase == 2u && i % 2u != 0u;
                float direct = reference_sample(a, extent, coordinates[i], channel);
                float indexed = reference_sample(use_a ? a : b, extent, coordinates[i], channel);
                require(std::abs(actual[i * 2u][channel] - direct) <= 2e-4f,
                        "normalized mirror/linear sampling differs from the reference");
                require(std::abs(actual[i * 2u + 1u][channel] - indexed) <= 2e-4f,
                        "nonuniform bindless sampling or set/remove ordering differs");
            }
    }
}

template<uint Dimension>
void test_dimension(Device &device) {
    test_surface_bounds<uchar, Dimension>(device);
    test_surface_bounds<uchar2, Dimension>(device);
    test_surface_bounds<uchar4, Dimension>(device);
    test_surface_bounds<float, Dimension>(device);
    test_surface_bounds<float2, Dimension>(device);
    test_surface_bounds<float4, Dimension>(device);
    test_surface_bounds<uint, Dimension>(device);
    test_surface_bounds<uint2, Dimension>(device);
    test_surface_bounds<uint4, Dimension>(device);
    test_sampling_and_bindless_updates<uchar, Dimension>(device);
    test_sampling_and_bindless_updates<uchar2, Dimension>(device);
    test_sampling_and_bindless_updates<uchar4, Dimension>(device);
    test_sampling_and_bindless_updates<float, Dimension>(device);
    test_sampling_and_bindless_updates<float2, Dimension>(device);
    test_sampling_and_bindless_updates<float4, Dimension>(device);
}

}

int main() {
    auto device = create_test_device();
    test_dimension<2u>(device);
    test_dimension<3u>(device);
    std::cout << "extended texture parity checks passed" << std::endl;
}
