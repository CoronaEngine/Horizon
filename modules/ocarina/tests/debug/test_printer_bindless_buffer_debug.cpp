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

void dump_printer_raw(const char *label, uint max_words = 24u) {
    auto &printer_buffer = Env::printer().buffer();
    printer_buffer.download_immediately();
    uint used_words = printer_buffer.host_buffer().empty() ? 0u : printer_buffer.host_buffer().back();
    uint dump_words = std::min<uint>(max_words, static_cast<uint>(printer_buffer.host_buffer().size()));
    std::cout << "  RAW [" << label << "] used_words=" << used_words << " first_words=";
    for (uint index = 0; index < dump_words; ++index) {
        std::cout << printer_buffer.host_buffer()[index];
        if (index + 1u != dump_words) {
            std::cout << ',';
        }
    }
    std::cout << std::endl;
}

template<typename Shader, typename... Args>
[[nodiscard]] vector<CapturedLog> run_and_capture(Stream &stream, Shader &shader, uint dispatch_size, Args &&...args) {
    vector<CapturedLog> logs;
    stream << shader(OC_FORWARD(args)...).dispatch(dispatch_size)
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
    for (const auto &log : logs) {
        std::cerr << "  actual log: [" << log.level << "] " << log.message << std::endl;
    }
    return false;
}

[[nodiscard]] float4 make_input(uint index) {
    float value = static_cast<float>(index * 3u);
    return make_float4(value + 1.f, value + 2.f, value + 3.f, value + 4.f);
}

[[nodiscard]] float4 transform_input(float4 value) {
    return value + make_float4(10.f, 20.f, 30.f, 40.f);
}

[[nodiscard]] bool equal_float4(float4 lhs, float4 rhs, float eps = 1e-5f) {
    return std::abs(lhs.x - rhs.x) <= eps &&
           std::abs(lhs.y - rhs.y) <= eps &&
           std::abs(lhs.z - rhs.z) <= eps &&
           std::abs(lhs.w - rhs.w) <= eps;
}

[[nodiscard]] int run_bindless_typed_buffer_staged_printer(Device &device, Stream &stream) {
    std::cout << "=== run_bindless_typed_buffer_staged_printer ===" << std::endl;
    constexpr uint count = 16u;
    auto src = device.create_buffer<float4>(count, "test_printer_bindless_staged_src");
    auto dst = device.create_buffer<float4>(count, "test_printer_bindless_staged_dst");
    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_input(index);
    }
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float4 value = bindless.buffer_var<float4>(src_slot).read(index);
            bindless.buffer_var<float4>(dst_slot).write(index, value + make_float4(10.f, 20.f, 30.f, 40.f));
        };
    };
    auto shader = device.compile(kernel, "test_bindless_typed_buffer_staged_write");
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    int failures = 0;
    for (uint index = 0; index < count; ++index) {
        if (!equal_float4(host_dst[index], transform_input(host_src[index]))) {
            std::cerr << "typed staged mismatch at " << index << std::endl;
            ++failures;
        }
    }

    Env::printer().reset();
    Kernel print_kernel = [&](BufferVar<float4> input, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float4 value = input.read(index);
            Env::printer().info("typed_staged {} {} {} {} {}",
                                index,
                                value.x.cast<int>(),
                                value.y.cast<int>(),
                                value.z.cast<int>(),
                                value.w.cast<int>());
        };
    };
    auto print_shader = device.compile(print_kernel, "test_bindless_typed_buffer_staged_printer");
    Stream print_stream = device.create_stream();
    auto logs = run_and_capture(print_stream, print_shader, count, dst, count);
    failures += !expect_log(logs, "typed_staged 0 11 22 33 44");
    failures += !expect_log(logs, "typed_staged 15 56 67 78 89");
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

[[nodiscard]] int run_bindless_typed_buffer_in_kernel_printer(Device &device, Stream &stream) {
    std::cout << "=== run_bindless_typed_buffer_in_kernel_printer ===" << std::endl;
    constexpr uint count = 16u;
    auto src = device.create_buffer<float4>(count, "test_printer_bindless_kernel_src");
    auto dst = device.create_buffer<float4>(count, "test_printer_bindless_kernel_dst");
    vector<float4> host_src(count);
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_input(index);
    }
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Env::printer().reset();
    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float4 value = bindless.buffer_var<float4>(src_slot).read(index);
            Float4 transformed = value + make_float4(10.f, 20.f, 30.f, 40.f);
            bindless.buffer_var<float4>(dst_slot).write(index, transformed);
            $if(index == 0u || index + 1u == n) {
                Env::printer().info("typed_inline {} {} {} {} {}",
                                    index,
                                    transformed.x.cast<int>(),
                                    transformed.y.cast<int>(),
                                    transformed.z.cast<int>(),
                                    transformed.w.cast<int>());
            };
        };
    };
    auto shader = device.compile(kernel, "test_bindless_typed_buffer_inline_printer");
    vector<float4> host_dst(count, make_float4(0.f));
    vector<CapturedLog> logs;
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << synchronize()
           << commit();
    dst.download_immediately(host_dst.data());
    dump_printer_raw("inline-before-decode");
    Env::printer().output_log([&](int level, const char *message) {
        logs.emplace_back(level, string{message});
    });
    Env::printer().reset();

    int failures = 0;
    failures += !expect_log(logs, "typed_inline 0 11 22 33 44");
    failures += !expect_log(logs, "typed_inline 15 56 67 78 89");
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

[[nodiscard]] int run_bindless_typed_buffer_inline_retrieve(Device &device, Stream &stream) {
    std::cout << "=== run_bindless_typed_buffer_inline_retrieve ===" << std::endl;
    constexpr uint count = 16u;
    auto src = device.create_buffer<float4>(count, "test_printer_bindless_inline_src");
    auto dst = device.create_buffer<float4>(count, "test_printer_bindless_inline_dst");
    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_input(index);
    }
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Env::printer().reset();
    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float4 value = bindless.buffer_var<float4>(src_slot).read(index);
            Float4 transformed = value + make_float4(10.f, 20.f, 30.f, 40.f);
            bindless.buffer_var<float4>(dst_slot).write(index, transformed);
            $if(index == 0u || index + 1u == n) {
                Env::printer().info("typed_retrieve {} {} {} {} {}",
                                    index,
                                    transformed.x.cast<int>(),
                                    transformed.y.cast<int>(),
                                    transformed.z.cast<int>(),
                                    transformed.w.cast<int>());
            };
        };
    };
    auto shader = device.compile(kernel, "test_bindless_typed_buffer_inline_retrieve");
    vector<CapturedLog> logs;
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data())
           << Env::printer().retrieve([&](int level, const char *message) {
               logs.emplace_back(level, string{message});
           })
           << synchronize()
           << commit();

    int failures = 0;
    for (uint index = 0; index < count; ++index) {
        if (!equal_float4(host_dst[index], transform_input(host_src[index]))) {
            std::cerr << "typed retrieve mismatch at " << index << std::endl;
            ++failures;
        }
    }
    failures += !expect_log(logs, "typed_retrieve 0 11 22 33 44");
    failures += !expect_log(logs, "typed_retrieve 15 56 67 78 89");
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

}// namespace

int main(int argc, char **argv) {
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    Stream stream = device.create_stream();
    Env::printer().init(device);

    string mode = argc > 1 ? argv[1] : "all";
    int failures = 0;
    if (mode == "staged" || mode == "all") {
        failures += run_bindless_typed_buffer_staged_printer(device, stream);
    }
    if (mode == "inline" || mode == "all") {
        failures += run_bindless_typed_buffer_in_kernel_printer(device, stream);
    }
    if (mode == "inline-retrieve" || mode == "all") {
        failures += run_bindless_typed_buffer_inline_retrieve(device, stream);
    }

    if (failures != 0) {
        std::cerr << "[test-printer-bindless-buffer-debug] failures=" << failures << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "[test-printer-bindless-buffer-debug] all checks passed" << std::endl;
    return EXIT_SUCCESS;
}
