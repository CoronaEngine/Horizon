#include "tests/backend_test.h"
//
// Created by GitHub Copilot on 2026/04/08.
//

#include "core/stl.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

template<typename T>
concept HasShaderPrint = requires(const T &value) { print("{}", value); } ||
                         requires(const T &value) { prints("{}", value); };

static_assert(!HasShaderPrint<Var<uint>>, "Shader logging uses Env::printer()");

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
                       ocarina::format("unexpected scalar/vector log: {}", logs[0].message)) &&
           expect_true(logs[0].level == 2, "info callback level must remain 2");
}

[[nodiscard]] bool test_outlined_local_logging(Device &device, Stream &stream) {
    Env::printer().reset();
    Kernel kernel = [](Var<uint> seed) {
        Var<uint> local = seed + 6u;
        outline("printer capture", [&] { Env::printer().info("outlined local {}", local); });
    };
    auto shader = device.compile(kernel, "test_printer_outlined_capture");
    auto logs = run_and_capture(stream, shader, 7u);
    return expect_true(logs.size() == 1u, "outlined local must produce one log") &&
           expect_true(logs[0].message == "outlined local 13" && logs[0].level == 2,
                       "outlined log must contain the runtime shader value and info level");
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
                       ocarina::format("unexpected second log: {}", logs[1].message)) &&
           expect_true(logs[0].level == 3 && logs[1].level == 4,
                       "warning/error callback levels must remain 3/4");
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
    const auto log_path = std::filesystem::current_path() / "test-printer-quill.log";
    horizon::core::LoggingOptions logging;
    logging.console = false;
    logging.file_path = log_path;
    horizon::core::initialize_logging(logging);
    RHIContext &context = RHIContext::instance();
    Device device = create_test_device();
    Stream stream = device.create_stream();
    Env::printer().init(device);

    bool passed = true;
    passed = test_scalar_and_vector_logging(device, stream) && passed;
    passed = test_outlined_local_logging(device, stream) && passed;
    passed = test_multiple_entries_preserve_order(device, stream) && passed;
    passed = test_ulong_round_trip(device, stream) && passed;
    passed = test_disabled_printer_suppresses_output(device, stream) && passed;
    Env::printer().reset();
    Kernel kernel = [](Var<uint> value) { Env::printer().info("printer default {}", value); };
    auto shader = device.compile(kernel, "test_printer_quill");
    stream << shader(17u).dispatch(1) << Env::printer().retrieve() << synchronize() << commit();
    log_flush();
    passed = expect_true(text_file_contents(log_path).find("printer default 17") != string::npos,
                         "default GPU Printer output must reach the configured Quill file") && passed;
    return passed ? 0 : 1;
}
