#include <cmath>
#include <cstdlib>
#include <iostream>

#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/base.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

[[nodiscard]] size_t pixel_index(uint2 res, uint x, uint y) {
    return static_cast<size_t>(y) * res.x + x;
}

[[nodiscard]] bool close_float(float lhs, float rhs, float eps = 1e-5f) {
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool equal_float4(float4 lhs, float4 rhs, float eps = 1e-5f) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps) &&
           close_float(lhs.z, rhs.z, eps) &&
           close_float(lhs.w, rhs.w, eps);
}

[[nodiscard]] float4 make_read_write_source(uint2 res, uint x, uint y) {
    float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(res.x);
    float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(res.y);
    float checker = static_cast<float>(((x * 17u + y * 31u) % 19u)) / 19.f;
    return make_float4(u, v, checker, 0.5f + 0.25f * u - 0.125f * v);
}

[[nodiscard]] float4 transform_read_write(uint x, uint y, float4 value) {
    float parity = static_cast<float>((x ^ y) & 1u) * 0.5f;
    return make_float4(value.z + 0.25f,
                       value.x * 2.f,
                       value.y + parity,
                       value.w + value.x - value.z * 0.5f);
}

[[nodiscard]] float4 make_affine_sample_source(uint2 res, uint x, uint y) {
    float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(res.x);
    float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(res.y);
    return make_float4(0.5f * u + 0.25f * v + 0.1f,
                       -0.125f * u + 0.75f * v + 0.2f,
                       0.33f * u - 0.2f * v + 0.3f,
                       1.f);
}

[[nodiscard]] float4 eval_affine_sample(float2 uv) {
    return make_float4(0.5f * uv.x + 0.25f * uv.y + 0.1f,
                       -0.125f * uv.x + 0.75f * uv.y + 0.2f,
                       0.33f * uv.x - 0.2f * uv.y + 0.3f,
                       1.f);
}

[[nodiscard]] float2 sample_uv(uint index) {
    float gx = static_cast<float>(index % 4u);
    float gy = static_cast<float>(index / 4u);
    return make_float2((gx + 0.35f) / 4.5f,
                       (gy + 0.4f) / 4.7f);
}

void log_pixel_mismatch(const char *label, uint x, uint y, float4 actual, float4 expected) {
    std::cerr << "  FAIL [" << label << "] at (" << x << ", " << y << ")"
              << " expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ", " << expected.w << ")"
              << " actual=(" << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w << ")"
              << std::endl;
}

void log_sample_mismatch(const char *label, uint index, float2 uv, float4 actual, float4 expected) {
    std::cerr << "  FAIL [" << label << "] at sample " << index
              << " uv=(" << uv.x << ", " << uv.y << ")"
              << " expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ", " << expected.w << ")"
              << " actual=(" << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w << ")"
              << std::endl;
}

int test_texture_param(Device &device, Stream &stream) {
    std::cout << "=== test_texture_param ===" << std::endl;
    constexpr uint2 res = make_uint2(17u, 11u);
    vector<float4> host_src(res.x * res.y);
    vector<float4> host_dst(res.x * res.y, make_float4(0.f));
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            host_src[pixel_index(res, x, y)] = make_read_write_source(res, x, y);
        }
    }

    auto src = device.create_texture2d(res, PixelStorage::FLOAT4, "test_texture_param_src");
    auto dst = device.create_texture2d(res, PixelStorage::FLOAT4, "test_texture_param_dst");
    src.upload_immediately(host_src.data());

    Kernel kernel = [&](Texture2DVar input, Texture2DVar output) {
        Uint2 xy = dispatch_idx().xy();
        Float4 value = input.read<float4>(xy);
        Float parity = cast<float>((xy.x ^ xy.y) & 1u) * 0.5f;
        Float4 transformed = make_float4(value.z + 0.25f,
                                         value.x * 2.f,
                                         value.y + parity,
                                         value.w + value.x - value.z * 0.5f);
        output.write(transformed, xy);
    };

    auto shader = device.compile(kernel, "test_texture_param");
    stream << shader(src, dst).dispatch(res)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    int failures = 0;
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            float4 expected = transform_read_write(x, y, host_src[pixel_index(res, x, y)]);
            float4 actual = host_dst[pixel_index(res, x, y)];
            if (!equal_float4(actual, expected)) {
                log_pixel_mismatch("texture_param", x, y, actual, expected);
                ++failures;
            }
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_texture_capture(Device &device, Stream &stream) {
    std::cout << "=== test_texture_capture ===" << std::endl;
    constexpr uint2 res = make_uint2(17u, 11u);
    vector<float4> host_src(res.x * res.y);
    vector<float4> host_dst(res.x * res.y, make_float4(0.f));
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            host_src[pixel_index(res, x, y)] = make_read_write_source(res, x, y);
        }
    }

    auto src = device.create_texture2d(res, PixelStorage::FLOAT4, "test_texture_capture_src");
    auto dst = device.create_texture2d(res, PixelStorage::FLOAT4, "test_texture_capture_dst");
    src.upload_immediately(host_src.data());

    Kernel kernel = [&]() {
        Uint2 xy = dispatch_idx().xy();
        Float4 value = src.read<float4>(xy);
        Float parity = cast<float>((xy.x ^ xy.y) & 1u) * 0.5f;
        Float4 transformed = make_float4(value.z + 0.25f,
                                         value.x * 2.f,
                                         value.y + parity,
                                         value.w + value.x - value.z * 0.5f);
        dst.write(transformed, xy);
    };

    auto shader = device.compile(kernel, "test_texture_capture");
    stream << shader().dispatch(res)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    int failures = 0;
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            float4 expected = transform_read_write(x, y, host_src[pixel_index(res, x, y)]);
            float4 actual = host_dst[pixel_index(res, x, y)];
            if (!equal_float4(actual, expected)) {
                log_pixel_mismatch("texture_capture", x, y, actual, expected);
                ++failures;
            }
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_texture_capture(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_texture_capture ===" << std::endl;
    constexpr uint2 res = make_uint2(8u, 6u);
    constexpr uint sample_count = 16u;
    vector<float4> host_src(res.x * res.y);
    vector<float4> host_samples(sample_count, make_float4(0.f));
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            host_src[pixel_index(res, x, y)] = make_affine_sample_source(res, x, y);
        }
    }

    auto tex = device.create_texture2d(res, PixelStorage::FLOAT4, "test_bindless_texture_capture_src");
    auto out = device.create_buffer<float4>(sample_count, "test_bindless_texture_capture_out");
    tex.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint tex_slot = bindless.emplace(tex);

    Kernel kernel = [&](BufferVar<float4> output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float gx = cast<float>(index % 4u);
            Float gy = cast<float>(index / 4u);
            Float2 uv = make_float2((gx + 0.35f) / 4.5f,
                                    (gy + 0.4f) / 4.7f);
            Float4 value = bindless.tex2d_var(tex_slot).sample(4, uv).as_vec4();
            output.write(index, value);
        };
    };

    auto shader = device.compile(kernel, "test_bindless_texture_capture");
    stream << bindless->upload_texture2d_handles(true)
           << synchronize()
           << shader(out, sample_count).dispatch(sample_count)
           << out.download(host_samples.data())
           << synchronize()
           << commit();

    int failures = 0;
    for (uint index = 0; index < sample_count; ++index) {
        float2 uv = sample_uv(index);
        float4 expected = eval_affine_sample(uv);
        float4 actual = host_samples[index];
        if (!equal_float4(actual, expected, 3e-4f)) {
            log_sample_mismatch("bindless_texture_capture", index, uv, actual, expected);
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_texture_param(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_texture_param ===" << std::endl;
    constexpr uint2 res = make_uint2(8u, 6u);
    constexpr uint sample_count = 16u;
    vector<float4> host_src(res.x * res.y);
    vector<float4> host_samples(sample_count, make_float4(0.f));
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            host_src[pixel_index(res, x, y)] = make_affine_sample_source(res, x, y);
        }
    }

    auto tex = device.create_texture2d(res, PixelStorage::FLOAT4, "test_bindless_texture_param_src");
    auto out = device.create_buffer<float4>(sample_count, "test_bindless_texture_param_out");
    tex.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    (void)bindless.emplace(tex);

    Kernel kernel = [&](BindlessArrayVar ba, BufferVar<float4> output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float gx = cast<float>(index % 4u);
            Float gy = cast<float>(index / 4u);
            Float2 uv = make_float2((gx + 0.35f) / 4.5f,
                                    (gy + 0.4f) / 4.7f);
            Float4 value = ba.tex2d_var(0u).sample(4, uv).as_vec4();
            output.write(index, value);
        };
    };

    auto shader = device.compile(kernel, "test_bindless_texture_param");
    stream << bindless->upload_texture2d_handles(true)
           << synchronize()
           << shader(bindless, out, sample_count).dispatch(sample_count)
           << out.download(host_samples.data())
           << synchronize()
           << commit();

    int failures = 0;
    for (uint index = 0; index < sample_count; ++index) {
        float2 uv = sample_uv(index);
        float4 expected = eval_affine_sample(uv);
        float4 actual = host_samples[index];
        if (!equal_float4(actual, expected, 3e-4f)) {
            log_sample_mismatch("bindless_texture_param", index, uv, actual, expected);
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

}// namespace

int main() {
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    Stream stream = device.create_stream();

    int total_failures = 0;
    total_failures += test_texture_param(device, stream);
    total_failures += test_texture_capture(device, stream);
    total_failures += test_bindless_texture_capture(device, stream);
    total_failures += test_bindless_texture_param(device, stream);

    if (total_failures != 0) {
        std::cerr << "[test-texture-access-modes] failed with " << total_failures << " mismatches" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "[test-texture-access-modes] all checks passed" << std::endl;
    return EXIT_SUCCESS;
}