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

[[nodiscard]] bool close_float(float lhs, float rhs, float eps = 1e-5f) {
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool equal_float4(float4 lhs, float4 rhs, float eps = 1e-5f) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps) &&
           close_float(lhs.z, rhs.z, eps) &&
           close_float(lhs.w, rhs.w, eps);
}

[[nodiscard]] float4 make_input(uint index) {
    float value = static_cast<float>(index * 4u);
    return make_float4(value + 2.f, value + 4.f, value + 6.f, value + 8.f);
}

[[nodiscard]] float4 transform_value(float4 value) {
    return value * 2.f + make_float4(1.f, 3.f, 5.f, 7.f);
}

void log_mismatch(const char *label, uint index, float4 actual, float4 expected) {
    std::cerr << "  FAIL [" << label << "] at index " << index
              << " expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ", " << expected.w << ")"
              << " actual=(" << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w << ")"
              << std::endl;
}

int verify_output(const char *label, const vector<float4> &actual, const vector<float4> &input) {
    int failures = 0;
    for (uint index = 0; index < input.size(); ++index) {
        float4 expected = transform_value(input[index]);
        if (!equal_float4(actual[index], expected)) {
            log_mismatch(label, index, actual[index], expected);
            ++failures;
        }
    }
    return failures;
}

int test_bytebuffer_param(Device &device, Stream &stream) {
    std::cout << "=== test_bytebuffer_param ===" << std::endl;
    constexpr uint count = 24u;
    auto src = device.create_byte_buffer(count * sizeof(float4), "test_bytebuffer_param_src");
    auto dst = device.create_byte_buffer(count * sizeof(float4), "test_bytebuffer_param_dst");
    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_input(index);
    }
    src.upload_immediately(host_src.data());

    Kernel kernel = [&](ByteBufferVar input, ByteBufferVar output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Uint offset = index * static_cast<uint>(sizeof(float4));
            Float4 value = input.load_as<float4>(offset);
            output.store(offset, value * 2.f + make_float4(1.f, 3.f, 5.f, 7.f));
        };
    };

    auto shader = device.compile(kernel, "test_bytebuffer_param");
    stream << shader(src, dst, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    int failures = verify_output("bytebuffer_param", host_dst, host_src);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bytebuffer_capture(Device &device, Stream &stream) {
    std::cout << "=== test_bytebuffer_capture ===" << std::endl;
    constexpr uint count = 24u;
    auto src = device.create_byte_buffer(count * sizeof(float4), "test_bytebuffer_capture_src");
    auto dst = device.create_byte_buffer(count * sizeof(float4), "test_bytebuffer_capture_dst");
    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_input(index + 50u);
    }
    src.upload_immediately(host_src.data());

    Kernel kernel = [&]() {
        Uint index = dispatch_id();
        $if(index < count) {
            Uint offset = index * static_cast<uint>(sizeof(float4));
            Float4 value = src.load_as<float4>(offset);
            dst.store(offset, value * 2.f + make_float4(1.f, 3.f, 5.f, 7.f));
        };
    };

    auto shader = device.compile(kernel, "test_bytebuffer_capture");
    stream << shader().dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    int failures = verify_output("bytebuffer_capture", host_dst, host_src);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_bytebuffer_capture(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_bytebuffer_capture ===" << std::endl;
    constexpr uint count = 24u;
    auto src = device.create_byte_buffer(count * sizeof(float4), "test_bindless_bytebuffer_capture_src");
    auto dst = device.create_byte_buffer(count * sizeof(float4), "test_bindless_bytebuffer_capture_dst");
    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_input(index + 100u);
    }
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Uint offset = index * static_cast<uint>(sizeof(float4));
            Float4 value = bindless.byte_buffer_var(src_slot).load_as<float4>(offset);
            bindless.byte_buffer_var(dst_slot).store(offset, value * 2.f + make_float4(1.f, 3.f, 5.f, 7.f));
        };
    };

    auto shader = device.compile(kernel, "test_bindless_bytebuffer_capture");
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    int failures = verify_output("bindless_bytebuffer_capture", host_dst, host_src);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_bytebuffer_param(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_bytebuffer_param ===" << std::endl;
    constexpr uint count = 24u;
    auto src = device.create_byte_buffer(count * sizeof(float4), "test_bindless_bytebuffer_param_src");
    auto dst = device.create_byte_buffer(count * sizeof(float4), "test_bindless_bytebuffer_param_dst");
    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_input(index + 150u);
    }
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    (void)bindless.emplace(src);
    (void)bindless.emplace(dst);

    Kernel kernel = [&](BindlessArrayVar ba, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Uint offset = index * static_cast<uint>(sizeof(float4));
            Float4 value = ba.byte_buffer_var(0u).load_as<float4>(offset);
            ba.byte_buffer_var(1u).store(offset, value * 2.f + make_float4(1.f, 3.f, 5.f, 7.f));
        };
    };

    auto shader = device.compile(kernel, "test_bindless_bytebuffer_param");
    stream << bindless->upload_buffer_handles(false)
           << shader(bindless, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    int failures = verify_output("bindless_bytebuffer_param", host_dst, host_src);
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
    total_failures += test_bytebuffer_param(device, stream);
    total_failures += test_bytebuffer_capture(device, stream);
    total_failures += test_bindless_bytebuffer_capture(device, stream);
    total_failures += test_bindless_bytebuffer_param(device, stream);

    if (total_failures != 0) {
        std::cerr << "[test-bytebuffer-access-modes] failed with " << total_failures << " mismatches" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "[test-bytebuffer-access-modes] all checks passed" << std::endl;
    return EXIT_SUCCESS;
}