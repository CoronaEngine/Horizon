#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <type_traits>

using namespace ocarina;

namespace {

template<uint Dimension>
auto make_texture(Device &device, uint3 extent, PixelStorage storage) {
    if constexpr (Dimension == 2u)
        return device.create_texture2d(make_uint2(extent.x, extent.y), storage);
    else
        return device.create_texture3d(extent, storage);
}

template<typename Pixel>
auto &component(Pixel &pixel, uint channel) {
    if constexpr (type_dimension_v<Pixel> == 1u) return pixel;
    else return pixel[channel];
}

// A view-relative offset must compose with its base view offset, exactly as it
// does for BufferView::copy_from/upload. This is a shared RHI capability probe:
// a failure in both backends is still a failure against the CPU oracle.
template<uint Dimension>
int probe_texture_buffer_view(Device &device, std::ostream &diagnostics) {
    constexpr uint3 extent = make_uint3(3u, 2u, Dimension == 2u ? 1u : 2u);
    constexpr uint count = extent.x * extent.y * extent.z;
    constexpr uint prefix = 5u;
    constexpr uint relative_offset = 2u;
    vector<uint> source(prefix + relative_offset + count + 3u);
    for (uint i = 0; i < source.size(); ++i) source[i] = 1009u + i * 73u;
    auto buffer = device.create_buffer<uint>(source.size());
    auto texture = make_texture<Dimension>(device, extent, PixelStorage::UINT1);
    auto view = buffer.view(prefix, relative_offset + count);
    vector<uint> actual(count);
    auto stream = device.create_stream();
    stream << buffer.upload(source.data()) << texture.copy_from_buffer(view, relative_offset)
           << texture.download(actual.data()) << synchronize() << commit();
    int failures = 0;
    for (uint i = 0; i < count; ++i) {
        const uint expected = source[prefix + relative_offset + i];
        diagnostics << "VIEW dimension=" << Dimension << " index=" << i
                  << " actual=" << actual[i] << " expected=" << expected << '\n';
        failures += actual[i] != expected;
    }
    return failures;
}

// CUDA returns zero for every absent channel, including alpha. Preserve that
// observed backend contract for both direct and bindless sample(4).
template<typename Pixel, uint Dimension>
int probe_narrow_channels(Device &device, std::ostream &diagnostics) {
    constexpr uint channels = type_dimension_v<Pixel>;
    constexpr bool normalized = std::is_same_v<type_element_t<Pixel>, uchar>;
    constexpr uint3 extent = make_uint3(2u, 2u, Dimension == 2u ? 1u : 2u);
    constexpr uint count = extent.x * extent.y * extent.z;
    vector<Pixel> pixels(count);
    vector<float3> positions(count);
    for (uint i = 0; i < count; ++i) {
        for (uint c = 0; c < channels; ++c) {
            if constexpr (normalized) component(pixels[i], c) = uchar(13u + 17u * i + 31u * c);
            else component(pixels[i], c) = float(2u + 3u * i + c) * 0.125f;
        }
        positions[i] = make_float3((float(i % 2u) + 0.5f) / 2.f,
                                    (float((i / 2u) % 2u) + 0.5f) / 2.f,
                                    Dimension == 2u ? 0.5f : (float(i / 4u) + 0.5f) / 2.f);
    }
    auto texture = make_texture<Dimension>(device, extent, PixelStorageImpl<Pixel>::storage);
    auto bindless = device.create_bindless_array();
    bindless.emplace(texture);
    auto coordinates = device.create_buffer<float3>(count);
    auto output = device.create_buffer<float4>(count * 2u);
    using TextureVar = std::conditional_t<Dimension == 2u, Texture2DVar, Texture3DVar>;
    Kernel kernel = [&bindless](TextureVar texture, BufferVar<float3> coordinates, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Float3 p = coordinates.read(i);
        Float4 direct = make_float4(0.f);
        Float4 indexed = make_float4(0.f);
        if constexpr (Dimension == 2u) {
            auto a = texture.sample(4u, p.x, p.y);
            auto b = bindless.tex2d_var(0u).sample(4u, p.x, p.y);
            for (uint c = 0; c < 4u; ++c) { direct[c] = a[c]; indexed[c] = b[c]; }
        } else {
            auto a = texture.sample(4u, p.x, p.y, p.z);
            auto b = bindless.tex3d_var(0u).sample(4u, p.x, p.y, p.z);
            for (uint c = 0; c < 4u; ++c) { direct[c] = a[c]; indexed[c] = b[c]; }
        }
        out.write(i * 2u, direct);
        out.write(i * 2u + 1u, indexed);
    };
    auto shader = device.compile(kernel, "resource-probe-narrow-texture");
    vector<float4> actual(count * 2u);
    auto stream = device.create_stream();
    stream << texture.upload(pixels.data()) << bindless.upload_handles()
           << coordinates.upload(positions.data()) << shader(texture, coordinates, output).dispatch(count)
           << output.download(actual.data()) << synchronize() << commit();
    int failures = 0;
    for (uint i = 0; i < count; ++i)
        for (uint c = 0; c < 4u; ++c) {
            const float direct = actual[i * 2u][c];
            const float indexed = actual[i * 2u + 1u][c];
            diagnostics << "NARROW dimension=" << Dimension << " format=" << uint(PixelStorageImpl<Pixel>::storage)
                      << " index=" << i << " channel=" << c << " direct=" << direct << " bindless=" << indexed;
            const float expected = c < channels ? float(component(pixels[i], c)) / (normalized ? 255.f : 1.f) : 0.f;
            diagnostics << " expected=" << expected;
            failures += !std::isfinite(direct) || std::abs(direct - expected) > 1e-6f;
            diagnostics << '\n';
            failures += !std::isfinite(direct) || !std::isfinite(indexed) || direct != indexed;
        }
    return failures;
}

// CUDA integer surface accesses use sizeof(Word) byte-x, independently of the
// image's storage channels. Use raw words (including NaN payloads and signed
// zero) so a float conversion or an incorrect texel coordinate cannot pass.
template<typename Word, uint Dimension>
int probe_uint_reinterpret(Device &device, PixelStorage storage, std::ostream &diagnostics) {
    constexpr uint word_channels = type_dimension_v<Word>;
    constexpr uint3 extent = make_uint3(16u, 3u, Dimension == 2u ? 1u : 2u);
    const uint storage_channels = 1u << (uint(storage) % 3u);
    const uint component_bytes = storage <= PixelStorage::BYTE4 ? 1u : 4u;
    const uint row_accesses = extent.x * storage_channels * component_bytes / sizeof(Word);
    const uint count = row_accesses * extent.y * extent.z;
    constexpr uint patterns[] = {0u, 0x80000000u, 0x7fc12345u, 0xffc54321u,
                                 1u, 0x007fffffu, 0x00800000u, 0x7f800000u,
                                 0xff800000u, 0x3f800000u, 0xdeadbeefu, 0xabcdef01u};
    vector<Word> source(count), values(count), cleared(count), downloaded(count);
    vector<uint3> coordinates;
    for (uint z = 0; z < extent.z; ++z)
        for (uint y = 0; y < extent.y; ++y)
            for (uint x = 0; x < row_accesses; ++x) {
                const uint i = uint(coordinates.size());
                coordinates.push_back(make_uint3(x, y, z));
                for (uint c = 0; c < word_channels; ++c) {
                    component(source[i], c) = patterns[(i * word_channels + c) % 12u];
                    component(values[i], c) = patterns[(i * word_channels + c + 5u) % 12u];
                }
            }
    coordinates.push_back(make_uint3(row_accesses, 0u, 0u));
    coordinates.push_back(make_uint3(0u, extent.y, 0u));
    coordinates.push_back(make_uint3(0xffffffffu, 1u, 0u));
    if constexpr (Dimension == 3u) coordinates.push_back(make_uint3(0u, 0u, extent.z));
    const uint total = uint(coordinates.size());
    values.resize(total); // Out-of-bounds writes use valid input buffer elements.
    auto input = make_texture<Dimension>(device, extent, storage);
    auto target = make_texture<Dimension>(device, extent, storage);
    auto positions = device.create_buffer<uint3>(total);
    auto replacement = device.create_buffer<Word>(total);
    auto output = device.create_buffer<Word>(total * 2u);
    using TextureVar = std::conditional_t<Dimension == 2u, Texture2DVar, Texture3DVar>;
    // BindlessArrayTexture exposes sampling only; raw surface reads use the
    // public typed texture argument API. Bindless sampling is covered above.
    Kernel writer = [](TextureVar input, TextureVar target, BufferVar<uint3> positions,
                       BufferVar<Word> replacement, BufferVar<Word> out, Uint valid_count) {
        Uint i = dispatch_id();
        // Every tested count is 3 * 2^k, hence 37 is coprime. Unlike 97,
        // it also permutes the 48/96-element cases instead of becoming identity.
        // Out-of-bounds test entries retain their original indices.
        $if (i < valid_count) { i = (i * 37u) % valid_count; };
        Uint3 p = positions.read(i);
        if constexpr (Dimension == 2u) {
            out.write(i, input.template read<Word>(p.x, p.y));
            target.write(replacement.read(i), p.x, p.y);
        } else {
            out.write(i, input.template read<Word>(p.x, p.y, p.z));
            target.write(replacement.read(i), p.x, p.y, p.z);
        }
    };
    writer.function()->set_block_dim(32u);
    Kernel reader = [](TextureVar target, BufferVar<uint3> positions, BufferVar<Word> out, Uint total) {
        Uint i = dispatch_id();
        Uint3 p = positions.read(i);
        if constexpr (Dimension == 2u)
            out.write(total + i, target.template read<Word>(p.x, p.y));
        else
            out.write(total + i, target.template read<Word>(p.x, p.y, p.z));
    };
    auto write_shader = device.compile(writer, "resource-probe-uint-surface-write");
    auto read_shader = device.compile(reader, "resource-probe-uint-surface-read");
    vector<Word> actual(total * 2u);
    auto stream = device.create_stream();
    stream << input.upload(source.data()) << positions.upload(coordinates.data())
           << replacement.upload(values.data());
    int failures = 0;
    // Permuted invocations write independent components of a wider texel,
    // including components reached by different 32-thread workgroups.
    // Repeat after clearing to catch lost read/modify/write updates and locks
    // left held by an earlier dispatch. No accesses overlap at byte granularity.
    for (uint iteration = 0; iteration < 3u; ++iteration) {
        stream << target.upload(cleared.data())
               << write_shader(input, target, positions, replacement, output, count).dispatch(total)
               << read_shader(target, positions, output, total).dispatch(total)
               << output.download(actual.data()) << target.download(downloaded.data())
               << synchronize() << commit();
        auto compare = [&](uint actual_value, uint expected, uint index, uint channel, uint path) {
            if (actual_value == expected) return;
            if (failures < 12)
                diagnostics << "REINTERPRET_MISMATCH dimension=" << Dimension << " format=" << uint(storage)
                            << " word_channels=" << word_channels << " iteration=" << iteration
                            << " index=" << index << " channel=" << channel << " path=" << path
                            << " actual=" << actual_value << " expected=" << expected << '\n';
            ++failures;
        };
        for (uint i = 0; i < total; ++i)
            for (uint c = 0; c < word_channels; ++c) {
                const uint expected_source = i < count ? component(source[i], c) : 0u;
                const uint expected_target = i < count ? component(values[i], c) : 0u;
                compare(component(actual[i], c), expected_source, i, c, 0u);
                compare(component(actual[total + i], c), expected_target, i, c, 1u);
                if (i < count) compare(component(downloaded[i], c), expected_target, i, c, 2u);
            }
    }
    diagnostics << "REINTERPRET dimension=" << Dimension << " format=" << uint(storage)
                << " word_channels=" << word_channels << " row_accesses=" << row_accesses
                << " tested_words=" << count * word_channels << " oracle_failures=" << failures << '\n';
    return failures;
}

template<uint Dimension>
int probe_uint_reinterpret_formats(Device &device, std::ostream &diagnostics) {
    int failures = 0;
    for (PixelStorage storage : {PixelStorage::BYTE1, PixelStorage::BYTE2, PixelStorage::BYTE4,
                                 PixelStorage::FLOAT1, PixelStorage::FLOAT2, PixelStorage::FLOAT4,
                                 PixelStorage::UINT1, PixelStorage::UINT2, PixelStorage::UINT4}) {
        failures += probe_uint_reinterpret<uint, Dimension>(device, storage, diagnostics);
        failures += probe_uint_reinterpret<uint2, Dimension>(device, storage, diagnostics);
        failures += probe_uint_reinterpret<uint4, Dimension>(device, storage, diagnostics);
    }
    return failures;
}

double mirror_coordinate(double value) {
    const double period = value - std::floor(value * 0.5) * 2.0;
    return period <= 1.0 ? period : 2.0 - period;
}

double linear_reference(const vector<float4> &pixels, uint3 extent, float3 uv,
                        uint channel, bool quantized) {
    double position[3] = {mirror_coordinate(uv.x) * extent.x - 0.5,
                          mirror_coordinate(uv.y) * extent.y - 0.5,
                          mirror_coordinate(uv.z) * extent.z - 0.5};
    int low[3];
    double fraction[3];
    for (uint axis = 0; axis < 3u; ++axis) {
        low[axis] = int(std::floor(position[axis]));
        fraction[axis] = position[axis] - low[axis];
        if (quantized) fraction[axis] = std::round(fraction[axis] * 256.0) / 256.0;
    }
    double result = 0.0;
    for (uint z = 0; z < 2u; ++z)
        for (uint y = 0; y < 2u; ++y)
            for (uint x = 0; x < 2u; ++x) {
                const uint ix = uint(std::clamp(low[0] + int(x), 0, int(extent.x) - 1));
                const uint iy = uint(std::clamp(low[1] + int(y), 0, int(extent.y) - 1));
                const uint iz = uint(std::clamp(low[2] + int(z), 0, int(extent.z) - 1));
                result += pixels[(iz * extent.y + iy) * extent.x + ix][channel] *
                          (x ? fraction[0] : 1.0 - fraction[0]) *
                          (y ? fraction[1] : 1.0 - fraction[1]) *
                          (z ? fraction[2] : 1.0 - fraction[2]);
            }
    return result;
}

template<uint Dimension>
int probe_linear_fraction(Device &device, std::ostream &diagnostics) {
    constexpr uint3 extent = make_uint3(4u, 3u, Dimension == 2u ? 1u : 3u);
    vector<float4> pixels(extent.x * extent.y * extent.z);
    float low[4], high[4];
    std::fill_n(low, 4, std::numeric_limits<float>::infinity());
    std::fill_n(high, 4, -std::numeric_limits<float>::infinity());
    for (uint i = 0; i < pixels.size(); ++i)
        for (uint c = 0; c < 4u; ++c) {
            pixels[i][c] = float(int((i * 19u + c * 11u) % 67u) - 29) / 16.f;
            low[c] = std::min(low[c], pixels[i][c]);
            high[c] = std::max(high[c], pixels[i][c]);
        }
    const float axes[] = {-1.713123f, -1.00031f, -0.01377f, 0.11937f, 0.25113f,
                          0.49327f, 0.78841f, 0.99913f, 1.19731f, 2.36591f};
    constexpr uint count = uint(sizeof(axes) / sizeof(axes[0]));
    vector<float3> positions(count);
    for (uint i = 0; i < count; ++i)
        positions[i] = make_float3(axes[i], axes[(i * 3u + 1u) % count],
                                    Dimension == 2u ? 0.5f : axes[(i * 7u + 2u) % count]);
    auto texture = make_texture<Dimension>(device, extent, PixelStorage::FLOAT4);
    auto bindless = device.create_bindless_array();
    bindless.emplace(texture);
    auto coordinates = device.create_buffer<float3>(count);
    auto output = device.create_buffer<float4>(count * 2u);
    using TextureVar = std::conditional_t<Dimension == 2u, Texture2DVar, Texture3DVar>;
    Kernel kernel = [&bindless](TextureVar texture, BufferVar<float3> coordinates, BufferVar<float4> out) {
        Uint i = dispatch_id();
        Float3 p = coordinates.read(i);
        Float4 direct = make_float4(0.f);
        Float4 indexed = make_float4(0.f);
        if constexpr (Dimension == 2u) {
            auto a = texture.sample(4u, p.x, p.y);
            auto b = bindless.tex2d_var(0u).sample(4u, p.x, p.y);
            for (uint c = 0; c < 4u; ++c) { direct[c] = a[c]; indexed[c] = b[c]; }
        } else {
            auto a = texture.sample(4u, p.x, p.y, p.z);
            auto b = bindless.tex3d_var(0u).sample(4u, p.x, p.y, p.z);
            for (uint c = 0; c < 4u; ++c) { direct[c] = a[c]; indexed[c] = b[c]; }
        }
        out.write(i * 2u, direct);
        out.write(i * 2u + 1u, indexed);
    };
    auto shader = device.compile(kernel, "resource-probe-fractional-texture-filter");
    vector<float4> actual(count * 2u);
    auto stream = device.create_stream();
    stream << texture.upload(pixels.data()) << bindless.upload_handles()
           << coordinates.upload(positions.data()) << shader(texture, coordinates, output).dispatch(count)
           << output.download(actual.data()) << synchronize() << commit();
    int failures = 0;
    for (uint i = 0; i < count; ++i)
        for (uint c = 0; c < 4u; ++c) {
            const double ideal = linear_reference(pixels, extent, positions[i], c, false);
            const double quantized = linear_reference(pixels, extent, positions[i], c, true);
            const double direct = actual[i * 2u][c];
            const double indexed = actual[i * 2u + 1u][c];
            // Each axis can perturb a convex interpolation by at most its
            // channel range times one 8-bit fractional step. The second CPU
            // reference models nearest 1/256 weights for diagnostic output;
            // it is not used to demand unspecified rounding from the driver.
            const double tolerance = double(high[c] - low[c]) * Dimension / 256.0 + 1e-5;
            diagnostics << "FILTER dimension=" << Dimension << " index=" << i << " channel=" << c
                      << " direct=" << direct << " bindless=" << indexed << " ideal=" << ideal
                      << " quantized8=" << quantized << " error=" << direct - ideal
                      << " tolerance=" << tolerance << '\n';
            failures += !std::isfinite(direct) || std::abs(direct - ideal) > tolerance;
            failures += !std::isfinite(indexed) || direct != indexed;
        }
    return failures;
}

}// namespace

int main(int argc, char **argv) {
    if (argc != 3 || std::strcmp(argv[1], "--case") != 0) {
        std::cerr << "Usage: test_resource_capability_probe --case "
                     "texture-buffer-view-offset|texture-narrow-channels|texture-linear-fraction|texture-uint-reinterpret\n";
        return 2;
    }
    const bool view = std::strcmp(argv[2], "texture-buffer-view-offset") == 0;
    const bool narrow = std::strcmp(argv[2], "texture-narrow-channels") == 0;
    const bool fraction = std::strcmp(argv[2], "texture-linear-fraction") == 0;
    const bool reinterpret = std::strcmp(argv[2], "texture-uint-reinterpret") == 0;
    if (!view && !narrow && !fraction && !reinterpret) {
        std::cerr << "Unknown resource probe case: " << argv[2] << '\n';
        return 2;
    }
    std::ostringstream diagnostics;
    diagnostics << std::setprecision(17);
    int failures = 0;
    {
        auto device = RHIContext::instance().create_device(test_backend_name());
        if (view) {
            failures += probe_texture_buffer_view<2u>(device, diagnostics);
            failures += probe_texture_buffer_view<3u>(device, diagnostics);
        } else if (narrow) {
            failures += probe_narrow_channels<float, 2u>(device, diagnostics);
            failures += probe_narrow_channels<float2, 2u>(device, diagnostics);
            failures += probe_narrow_channels<uchar, 2u>(device, diagnostics);
            failures += probe_narrow_channels<uchar2, 2u>(device, diagnostics);
            failures += probe_narrow_channels<float, 3u>(device, diagnostics);
            failures += probe_narrow_channels<float2, 3u>(device, diagnostics);
            failures += probe_narrow_channels<uchar, 3u>(device, diagnostics);
            failures += probe_narrow_channels<uchar2, 3u>(device, diagnostics);
        } else if (reinterpret) {
            failures += probe_uint_reinterpret_formats<2u>(device, diagnostics);
            failures += probe_uint_reinterpret_formats<3u>(device, diagnostics);
        } else {
            failures += probe_linear_fraction<2u>(device, diagnostics);
            failures += probe_linear_fraction<3u>(device, diagnostics);
        }
    }
    // Finish all resource/device destruction before emitting the diagnostics in
    // one write, so asynchronous engine logs cannot split individual fields.
    diagnostics << "RESOURCE_PROBE case=" << argv[2] << " oracle_failures=" << failures << '\n';
    const auto output = diagnostics.str();
    std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
    std::cout.flush();
    return failures == 0 ? 0 : 1;
}
