#include "horizon/core/logging.h"
#ifdef HORIZON_TEST_ENGINE_LOGGING
#include "Horizon_old.h"
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    std::string read_file(const std::filesystem::path& path)
    {
        std::ifstream stream { path };
        return { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    }
} // namespace

int main(int argc, char**)
{
    const bool console_enabled = argc > 1;
    const auto directory = std::filesystem::current_path() /
                           (console_enabled ? "logging-relative-file" : "logging-configuration");
    const auto console_path = directory / "console.log";
    std::filesystem::create_directories(directory);
    if (!std::freopen(console_path.string().c_str(), "w", stdout))
    {
        return 1;
    }

    horizon::core::LoggingOptions options;
    options.console = false;
    bool rejected = false;
    try
    {
        horizon::core::initialize_logging(options);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    expect(rejected, "An initialization without any outputs is rejected and can be retried");
    options.console = console_enabled;
    options.file_path = console_enabled ? std::filesystem::path { "horizon_core_console" }
                                        : directory / "nested" / "messages.log";
    options.level = horizon::core::LogLevel::Warning;
    horizon::core::initialize_logging(options);
    LOG_INFO(horizon::core::get_quill_logger(), "filtered core info");
    CFW_LOG_INFO("filtered legacy info");
    LOG_WARNING(horizon::core::get_quill_logger(), "core file message");
    CFW_LOG_WARNING("legacy file value {}", 42);
    PY_LOG_WARNING("python value {}", 7);
    VUE_LOG_WARNING("vue value {}", 9);
    LOG_WARNING(horizon::core::get_quill_logger(), "UTF-8: {}", "\xe4\xb8\xad\xe6\x96\x87");
    horizon::core::log_flush();

    // Repeated initialization must neither truncate the file nor reset its level.
    horizon::core::initialize_logging(options);
    Corona::Kernel::CoronaLogger::initialize();
    CFW_LOG_INFO("filtered after repeated initialization");

    std::vector<std::thread> writers;
    for (int i = 0; i < 4; ++i)
    {
        writers.emplace_back([i] {
            LOG_WARNING(horizon::core::get_quill_logger(), "core worker {}", i);
            CFW_LOG_WARNING("legacy worker {}", i);
        });
    }
    for (auto& writer : writers)
    {
        writer.join();
    }
    CFW_LOG_FLUSH();

    const auto output = read_file(options.file_path);
    expect(output.find("filtered") == std::string::npos, "Configured level filters both interfaces");
    expect(output.find("core file message") != std::string::npos, "Core messages reach the configured file");
    expect(output.find("legacy file value 42") != std::string::npos, "Legacy messages reach the same file");
    expect(output.find("[Python] python value 7") != std::string::npos, "Python prefix is preserved");
    expect(output.find("[Vue] vue value 9") != std::string::npos, "Vue prefix is preserved");
    expect(output.find("test_logging_configuration.cpp") != std::string::npos, "Legacy source locations are preserved");
    expect(output.find("UTF-8: \xe4\xb8\xad\xe6\x96\x87") != std::string::npos, "UTF-8 is not escaped by the backend");
    for (int i = 0; i < 4; ++i)
    {
        expect(output.find("core worker " + std::to_string(i)) != std::string::npos, "Concurrent Core messages are flushed");
        expect(output.find("legacy worker " + std::to_string(i)) != std::string::npos, "Concurrent legacy messages are flushed");
    }
    std::fflush(stdout);
    const auto console_output = read_file(console_path);
    if (console_enabled)
    {
        const auto first = console_output.find("core file message");
        expect(first != std::string::npos, "Console output remains enabled alongside a relative log file");
        expect(first == std::string::npos || console_output.find("core file message", first + 1) == std::string::npos,
               "A relative filename does not duplicate the console sink");
    }
    else
    {
        expect(console_output.empty(), "File-only configuration does not write to the console");
    }
    return failures == 0 ? 0 : 1;
}
