#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/base.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

[[nodiscard]] bool close_float(float lhs, float rhs, float eps = 1e-5f) {
    float diff = lhs - rhs;
    if (diff < 0.f) {
        diff = -diff;
    }
    return diff <= eps;
}

[[nodiscard]] bool equal_float2(float2 lhs, float2 rhs, float eps = 1e-5f) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps);
}

[[nodiscard]] bool equal_float3(float3 lhs, float3 rhs, float eps = 1e-5f) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps) &&
           close_float(lhs.z, rhs.z, eps);
}

[[nodiscard]] bool equal_float4(float4 lhs, float4 rhs, float eps = 1e-5f) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps) &&
           close_float(lhs.z, rhs.z, eps) &&
           close_float(lhs.w, rhs.w, eps);
}

void log_float2_mismatch(const char *label, uint index, float2 actual, float2 expected) {
    std::cerr << "  FAIL [" << label << "] at " << index
              << " expected=(" << expected.x << ", " << expected.y << ")"
              << " actual=(" << actual.x << ", " << actual.y << ")"
              << std::endl;
}

void log_float3_mismatch(const char *label, uint index, float3 actual, float3 expected) {
    std::cerr << "  FAIL [" << label << "] at " << index
              << " expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ")"
              << " actual=(" << actual.x << ", " << actual.y << ", " << actual.z << ")"
              << std::endl;
}

void log_float4_mismatch(const char *label, uint index, float4 actual, float4 expected) {
    std::cerr << "  FAIL [" << label << "] at " << index
              << " expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ", " << expected.w << ")"
              << " actual=(" << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w << ")"
              << std::endl;
}

int test_host_swizzle() {
    std::cout << "=== test_host_swizzle ===" << std::endl;
    int failures = 0;

    {
        float2 value = make_float2(1.f, 2.f);
        float2 reversed = value.yx();
        if (!equal_float2(reversed, make_float2(2.f, 1.f))) {
            log_float2_mismatch("host-float2-read", 0u, reversed, make_float2(2.f, 1.f));
            ++failures;
        }

        value.xy() = value.yx();
        if (!equal_float2(value, make_float2(2.f, 1.f))) {
            log_float2_mismatch("host-float2-assign", 0u, value, make_float2(2.f, 1.f));
            ++failures;
        }
    }

    {
        float3 value = make_float3(1.f, 2.f, 3.f);
        float3 reordered = value.zxy();
        if (!equal_float3(reordered, make_float3(3.f, 1.f, 2.f))) {
            log_float3_mismatch("host-float3-read", 0u, reordered, make_float3(3.f, 1.f, 2.f));
            ++failures;
        }

        value.yzx() = value.xxy();
        value.xy() += make_float2(10.f, 20.f);
        if (!equal_float3(value, make_float3(12.f, 21.f, 1.f))) {
            log_float3_mismatch("host-float3-write", 0u, value, make_float3(12.f, 21.f, 1.f));
            ++failures;
        }
    }

    {
        const float4 source = make_float4(1.f, 2.f, 3.f, 4.f);
        float4 reversed = source.wzyx();
        if (!equal_float4(reversed, make_float4(4.f, 3.f, 2.f, 1.f))) {
            log_float4_mismatch("host-float4-read", 0u, reversed, make_float4(4.f, 3.f, 2.f, 1.f));
            ++failures;
        }

        float4 value = source;
        value.xy() = value.zw();
        value.wz() += make_float2(10.f, 20.f);
        if (!equal_float4(value, make_float4(3.f, 4.f, 23.f, 14.f))) {
            log_float4_mismatch("host-float4-write", 0u, value, make_float4(3.f, 4.f, 23.f, 14.f));
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

[[nodiscard]] float4 make_input(uint index) {
    float i = static_cast<float>(index);
    return make_float4(i + 0.25f,
                       i * 1.5f + 1.f,
                       -i - 2.f,
                       i * 0.75f + 3.5f);
}

int test_device_swizzle(Device &device, Stream &stream) {
    std::cout << "=== test_device_swizzle ===" << std::endl;
    int failures = 0;

    constexpr uint count = 8u;
    vector<float4> host_input(count);
    vector<float2> host_out2(count, make_float2(0.f));
    vector<float3> host_out3(count, make_float3(0.f));
    vector<float4> host_out4(count, make_float4(0.f));

    for (uint index = 0; index < count; ++index) {
        host_input[index] = make_input(index);
    }

    Buffer<float4> input = device.create_buffer<float4>(count, "test_swizzle_input");
    Buffer<float2> out2 = device.create_buffer<float2>(count, "test_swizzle_out2");
    Buffer<float3> out3 = device.create_buffer<float3>(count, "test_swizzle_out3");
    Buffer<float4> out4 = device.create_buffer<float4>(count, "test_swizzle_out4");
    input.upload_immediately(host_input.data());

    Kernel kernel = [&](BufferVar<float4> src,
                        BufferVar<float2> dst2,
                        BufferVar<float3> dst3,
                        BufferVar<float4> dst4,
                        Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float4 value = src.read(index);
            Float2 pair = value.xy();
            Float3 triple = value.xyz();
            Float4 quad = value;

            dst2.write(index, pair.yx() + make_float2(0.5f, 1.5f));

            triple.yzx() = triple.xxy();
            triple.xy() += make_float2(10.f, 20.f);
            dst3.write(index, triple);

            quad.xy() = quad.zw();
            quad.wz() += make_float2(10.f, 20.f);
            dst4.write(index, quad);
        };
    };

    auto shader = device.compile(kernel, "test_swizzle_device");
    stream << shader(input, out2, out3, out4, count).dispatch(count)
           << out2.download(host_out2.data())
           << out3.download(host_out3.data())
           << out4.download(host_out4.data())
           << synchronize()
           << commit();

    for (uint index = 0; index < count; ++index) {
        float4 value = host_input[index];

        float2 expected2 = make_float2(value.y + 0.5f, value.x + 1.5f);
        if (!equal_float2(host_out2[index], expected2)) {
            log_float2_mismatch("device-float2", index, host_out2[index], expected2);
            ++failures;
        }

        float3 expected3 = make_float3(value.y + 10.f,
                                       value.x + 20.f,
                                       value.x);
        if (!equal_float3(host_out3[index], expected3)) {
            log_float3_mismatch("device-float3", index, host_out3[index], expected3);
            ++failures;
        }

        float4 expected4 = make_float4(value.z,
                                       value.w,
                                       value.z + 20.f,
                                       value.w + 10.f);
        if (!equal_float4(host_out4[index], expected4)) {
            log_float4_mismatch("device-float4", index, host_out4[index], expected4);
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
    total_failures += test_host_swizzle();
    total_failures += test_device_swizzle(device, stream);

    if (total_failures != 0) {
        std::cerr << "[test-swizzle] failed with " << total_failures << " mismatches" << std::endl;
        return 1;
    }

    std::cout << "[test-swizzle] all checks passed" << std::endl;
    return 0;
}