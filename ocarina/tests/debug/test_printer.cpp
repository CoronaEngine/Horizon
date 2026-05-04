//
// Created by GitHub Copilot on 2026/04/08.
//

#include "core/stl.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

struct CapturedLog {
    int level;
    string message;
};

[[nodiscard]] bool expect_true(bool condition, string message) {
    if (condition) {
        return true;
    }
    std::cerr << message << std::endl;
    return false;
}

template<typename Shader, typename... Args>
[[nodiscard]] vector<CapturedLog> run_and_capture(Stream &stream, Shader &shader, Args &&...args) {
    vector<CapturedLog> logs;
    stream << shader(OC_FORWARD(args)...).dispatch(1)
           << Env::printer().retrieve([&](int level, const char *message) {
               logs.emplace_back(level, string{message});
           })
           << synchronize()
           << commit();
    return logs;
}

[[nodiscard]] bool test_scalar_and_vector_logging(Device &device, Stream &stream) {
    Env::printer().reset();
    Kernel kernel = [&](Var<uint> scalar, Var<uint3> vec) {
        Env::printer().info("printer basic {} {} {} {}", scalar, vec);
    };
    auto shader = device.compile(kernel, "test_printer_basic");
    auto logs = run_and_capture(stream, shader, 7u, make_uint3(11u, 22u, 33u));
    return expect_true(logs.size() == 1u,
                       ocarina::format("expected 1 log for scalar/vector test, got {}", logs.size())) &&
           expect_true(logs[0].message == "printer basic 7 11 22 33",
                       ocarina::format("unexpected scalar/vector log: {}", logs[0].message));
}

[[nodiscard]] bool test_multiple_entries_preserve_order(Device &device, Stream &stream) {
    Env::printer().reset();
    Kernel kernel = [&](Var<int> first, Var<int> second) {
        Env::printer().warn("first {}", first);
        Env::printer().err("second {}", second);
    };
    auto shader = device.compile(kernel, "test_printer_order");
    auto logs = run_and_capture(stream, shader, 3, 9);
    return expect_true(logs.size() == 2u,
                       ocarina::format("expected 2 logs for ordering test, got {}", logs.size())) &&
           expect_true(logs[0].message == "first 3",
                       ocarina::format("unexpected first log: {}", logs[0].message)) &&
           expect_true(logs[1].message == "second 9",
                       ocarina::format("unexpected second log: {}", logs[1].message));
}

[[nodiscard]] bool test_ulong_round_trip(Device &device, Stream &stream) {
    Env::printer().reset();
    constexpr ulong expected = 0x123456789abcdef0ull;
    Kernel kernel = [&](Var<ulong> value) {
        Env::printer().info("ulong {}", value);
    };
    auto shader = device.compile(kernel, "test_printer_ulong");
    auto logs = run_and_capture(stream, shader, expected);
    auto expected_message = ocarina::format("ulong {}", expected);
    return expect_true(logs.size() == 1u,
                       ocarina::format("expected 1 log for ulong test, got {}", logs.size())) &&
           expect_true(logs[0].message == expected_message,
                       ocarina::format("unexpected ulong log: {}", logs[0].message));
}

[[nodiscard]] bool test_disabled_printer_suppresses_output(Device &device, Stream &stream) {
    Env::printer().reset();
    Env::printer().set_enabled(false);
    Kernel kernel = [&](Var<uint> value) {
        Env::printer().info("suppressed {}", value);
    };
    auto shader = device.compile(kernel, "test_printer_disabled");
    auto logs = run_and_capture(stream, shader, 42u);
    Env::printer().set_enabled(true);
    return expect_true(logs.empty(),
                       ocarina::format("expected no logs when printer is disabled, got {}", logs.size()));
}

}// namespace

int main() {
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    Stream stream = device.create_stream();
    Env::printer().init(device);

    bool passed = true;
    passed = test_scalar_and_vector_logging(device, stream) && passed;
    passed = test_multiple_entries_preserve_order(device, stream) && passed;
    passed = test_ulong_round_trip(device, stream) && passed;
    passed = test_disabled_printer_suppresses_output(device, stream) && passed;
    return passed ? 0 : 1;
}