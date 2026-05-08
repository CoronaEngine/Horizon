#include <cstdlib>
#include <iostream>

#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/base.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

struct CapturedLog {
    int level;
    string message;
};

template<typename Shader, typename... Args>
[[nodiscard]] vector<CapturedLog> run_and_capture(Stream &stream, Shader &shader, Args &&...args) {
    vector<CapturedLog> logs;
    stream << shader(OC_FORWARD(args)...).dispatch(2)
           << Env::printer().retrieve([&](int level, const char *message) {
               logs.emplace_back(level, string{message});
           })
           << synchronize()
           << commit();
    return logs;
}

[[nodiscard]] bool expect_log(const vector<CapturedLog> &logs, string_view expected) {
    for (const auto &log : logs) {
        if (log.message == expected) {
            return true;
        }
    }
    std::cerr << "missing log: " << expected << std::endl;
    return false;
}

[[nodiscard]] int run_buffer_control(Device &device, Stream &stream) {
    std::cout << "=== run_buffer_control ===" << std::endl;
    auto buffer = device.create_buffer<float4>(2u, "test_printer_buffer_control");
    vector<float4> host = {
        make_float4(1.f, 2.f, 3.f, 4.f),
        make_float4(5.f, 6.f, 7.f, 8.f)};
    buffer.upload_immediately(host.data());

    Env::printer().reset();
    Kernel kernel = [&](BufferVar<float4> input) {
        Uint index = dispatch_id();
        Float4 value = input.read(index);
        Env::printer().info("buffer {} {} {} {} {}",
                            index,
                            value.x.cast<int>(),
                            value.y.cast<int>(),
                            value.z.cast<int>(),
                            value.w.cast<int>());
    };
    auto shader = device.compile(kernel, "test_printer_buffer_control");
    auto logs = run_and_capture(stream, shader, buffer);

    int failures = 0;
    failures += !expect_log(logs, "buffer 0 1 2 3 4");
    failures += !expect_log(logs, "buffer 1 5 6 7 8");
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

[[nodiscard]] int run_byte_buffer_repro(Device &device, Stream &stream) {
    std::cout << "=== run_byte_buffer_repro ===" << std::endl;
    auto buffer = device.create_byte_buffer(2u * sizeof(float4), "test_printer_byte_buffer_repro");
    vector<float4> host = {
        make_float4(11.f, 12.f, 13.f, 14.f),
        make_float4(21.f, 22.f, 23.f, 24.f)};
    buffer.upload_immediately(host.data());

    Env::printer().reset();
    Kernel kernel = [&](ByteBufferVar input) {
        Uint index = dispatch_id();
        Uint offset = index * static_cast<uint>(sizeof(float4));
        Float4 value = input.load_as<float4>(offset);
        Env::printer().info("byte_buffer {} {} {} {} {}",
                            index,
                            value.x.cast<int>(),
                            value.y.cast<int>(),
                            value.z.cast<int>(),
                            value.w.cast<int>());
    };
    auto shader = device.compile(kernel, "test_printer_byte_buffer_repro");
    auto logs = run_and_capture(stream, shader, buffer);

    int failures = 0;
    failures += !expect_log(logs, "byte_buffer 0 11 12 13 14");
    failures += !expect_log(logs, "byte_buffer 1 21 22 23 24");
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

[[nodiscard]] int run_bindless_byte_buffer_then_printer_repro(Device &device, Stream &stream) {
    std::cout << "=== run_bindless_byte_buffer_then_printer_repro ===" << std::endl;

    constexpr uint count = 2u;
    auto src = device.create_byte_buffer(count * sizeof(float4), "test_printer_bindless_byte_buffer_src");
    auto dst = device.create_byte_buffer(count * sizeof(float4), "test_printer_bindless_byte_buffer_dst");
    vector<float4> host_src = {
        make_float4(31.f, 32.f, 33.f, 34.f),
        make_float4(41.f, 42.f, 43.f, 44.f)};
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel write_kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Uint offset = index * static_cast<uint>(sizeof(float4));
            Float4 value = bindless.byte_buffer_var(src_slot).load_as<float4>(offset);
            bindless.byte_buffer_var(dst_slot).store(offset, value + make_float4(1.f, 1.f, 1.f, 1.f));
        };
    };
    auto write_shader = device.compile(write_kernel, "test_bindless_then_printer_write");
    stream << bindless->upload_buffer_handles(false)
           << write_shader(count).dispatch(count)
           << synchronize()
           << commit();

    Env::printer().reset();
    Kernel print_kernel = [&](ByteBufferVar input) {
        Uint index = dispatch_id();
        Uint offset = index * static_cast<uint>(sizeof(float4));
        Float4 value = input.load_as<float4>(offset);
        Env::printer().info("after_bindless {} {} {} {} {}",
                            index,
                            value.x.cast<int>(),
                            value.y.cast<int>(),
                            value.z.cast<int>(),
                            value.w.cast<int>());
    };
    auto print_shader = device.compile(print_kernel, "test_bindless_then_printer_readback");
    auto logs = run_and_capture(stream, print_shader, dst);

    int failures = 0;
    failures += !expect_log(logs, "after_bindless 0 32 33 34 35");
    failures += !expect_log(logs, "after_bindless 1 42 43 44 45");
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
    Env::printer().init(device);

    int failures = 0;
    failures += run_buffer_control(device, stream);
    failures += run_byte_buffer_repro(device, stream);
    failures += run_bindless_byte_buffer_then_printer_repro(device, stream);

    if (failures != 0) {
        std::cerr << "[test-printer-bytebuffer-debug] failures=" << failures << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[test-printer-bytebuffer-debug] all checks passed" << std::endl;
    return EXIT_SUCCESS;
}