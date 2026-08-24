#include "core/util/logging.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

int duplicate_stdout() {
#if defined(_WIN32)
    return _dup(_fileno(stdout));
#else
    return dup(fileno(stdout));
#endif
}

bool restore_stdout(int descriptor) {
#if defined(_WIN32)
    const bool restored = _dup2(descriptor, _fileno(stdout)) == 0;
    _close(descriptor);
#else
    const bool restored = dup2(descriptor, fileno(stdout)) >= 0;
    close(descriptor);
#endif
    return restored;
}

}// namespace

int main() {
    const std::filesystem::path output_path =
        std::filesystem::current_path() / "horizon-test-core-logging.log";
    std::filesystem::remove(output_path);

    const int original_stdout = duplicate_stdout();
    expect(original_stdout >= 0, "stdout can be duplicated for log capture");
    if (original_stdout < 0) {
        return 1;
    }

    FILE *redirected_stdout = std::freopen(output_path.string().c_str(), "w", stdout);
    expect(redirected_stdout != nullptr, "stdout can be redirected for log capture");
    if (redirected_stdout == nullptr) {
        restore_stdout(original_stdout);
        return 1;
    }

    horizon::core::log_level_debug();
    horizon::core::debug("debug value ", 1);
    horizon::core::log_level_info();
    horizon::core::info("info value ", 2);
    horizon::core::log_level_warning();
    horizon::core::warning("warning value ", 3);
    horizon::core::info("suppressed info");
    horizon::core::log_level_error();
    horizon::core::warning("suppressed warning");
    horizon::core::log_level_debug();
    horizon::core::log_flush();
    std::fflush(stdout);

    expect(restore_stdout(original_stdout), "stdout is restored after log capture");

    std::ifstream output_stream{output_path};
    const std::string output{std::istreambuf_iterator<char>{output_stream},
                             std::istreambuf_iterator<char>{}};
    expect(output.find("debug value 1") != std::string::npos,
           "Debug messages reach the Quill console sink");
    expect(output.find("info value 2") != std::string::npos,
           "Info messages reach the Quill console sink");
    expect(output.find("warning value 3") != std::string::npos,
           "Warning messages reach the Quill console sink");
    expect(output.find("suppressed info") == std::string::npos,
           "Info messages are filtered at Warning level");
    expect(output.find("suppressed warning") == std::string::npos,
           "Warning messages are filtered at Error level");
    output_stream.close();
    std::filesystem::remove(output_path);

    bool caught = false;
    try {
        OC_EXCEPTION("logging exception ", 42);
    } catch (const std::runtime_error &exception) {
        caught = true;
        const std::string_view message{exception.what()};
        expect(message.find("logging exception 42") != std::string_view::npos,
               "OC_EXCEPTION preserves serialized arguments");
        expect(message.find("test_logging.cpp") != std::string_view::npos,
               "OC_EXCEPTION preserves the source location");
    }
    expect(caught, "OC_EXCEPTION throws std::runtime_error");

    return failures == 0 ? 0 : 1;
}
