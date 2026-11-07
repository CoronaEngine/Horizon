#include "horizon/hotfix/windows_process_runner.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace horizon::hotfix;

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_success(const ProcessResult &result, const char *message) {
    expect(result.succeeded(), message);
    if (!result.error_message.empty()) {
        std::cerr << result.error_message << '\n';
    }
}

void expect_execution_error(const ProcessResult &result, const char *message) {
    expect(!result.exit_code.has_value() && !result.error_message.empty(), message);
}

void test_arguments(IProcessRunner &runner, const std::filesystem::path &child) {
    ProcessRequest request{
        .executable = child,
        .arguments = {"arguments", "", "plain", "with spaces", "with\ta tab",
                      "quote\"inside", R"(back\\slash)", "space and trailing\\",
                      "space and trailing\\\\", "\\\"", "\\\\\"", "&|<>^%!*?()",
                      "text \u6D4B\u8BD5 \U0001F680"}};
    std::string expected;
    for (auto argument = request.arguments.begin() + 1; argument != request.arguments.end(); ++argument) {
        expected.append(*argument).push_back('\0');
    }

    const auto result = runner.run(request);
    expect_success(result, "the argument probe exits zero");
    expect(result.stdout_text == expected, "the child receives each exact argument in order");
}

void test_working_directory(IProcessRunner &runner, const std::filesystem::path &child,
                            const std::filesystem::path &directory) {
    const auto caller_directory = std::filesystem::current_path();
    const auto executable = std::filesystem::relative(child, caller_directory);
    expect(!executable.empty() && executable.is_relative(), "the child executable is relative to the caller");
    const auto result = runner.run({.executable = executable,
                                    .arguments = {"working-directory"},
                                    .working_directory = directory});
    expect_success(result, "the working directory probe exits zero");
    const auto actual = std::filesystem::path{std::u8string(result.stdout_text.begin(),
                                                           result.stdout_text.end())};
    std::error_code error;
    expect(std::filesystem::equivalent(actual, directory, error),
           "the child starts in the requested working directory");
    expect(std::filesystem::current_path() == caller_directory,
           "setting the child's working directory leaves the caller's directory unchanged");
}

void test_environment(IProcessRunner &runner, const std::filesystem::path &child) {
    const auto result = runner.run({.executable = child,
                                    .arguments = {"environment"},
                                    .environment_overrides = {
                                        {"HORIZON_PROCESS_TEST_OVERRIDE", "override \u6D4B\u8BD5 \U0001F680"}}});
    expect_success(result, "the environment probe exits zero");
    expect(result.stdout_text == "parent\noverride \u6D4B\u8BD5 \U0001F680\n1",
           "the child inherits the environment and receives one case-insensitive override");

    wchar_t original[32]{};
    const auto size = GetEnvironmentVariableW(L"Horizon_Process_Test_Override", original, 32);
    expect(size < 32 && std::wstring_view(original, size) == L"original",
           "running with an override leaves the host environment unchanged");
}

void test_output(IProcessRunner &runner, const std::filesystem::path &child) {
    const auto result = runner.run({.executable = child, .arguments = {"streams"}});
    expect_success(result, "the output probe exits zero");
    expect(result.stdout_text == std::string("stdout\0\r\n", 9) + std::string(512 * 1024, 'o'),
           "stdout preserves all bytes, including embedded NUL and CRLF");
    expect(result.stderr_text == std::string("stderr\0\r\n", 9) + std::string(512 * 1024, 'e'),
           "stderr preserves all bytes separately from stdout");
}

void test_descendant_output_handles(IProcessRunner &runner, const std::filesystem::path &child) {
    using namespace std::chrono_literals;
    using Event = std::unique_ptr<void, decltype(&CloseHandle)>;
    const auto suffix = std::to_string(GetCurrentProcessId());
    const auto ready_name = "Local\\HorizonProcessTestDescendantReady-" + suffix;
    const auto release_name = "Local\\HorizonProcessTestDescendantRelease-" + suffix;
    const Event ready{CreateEventA(nullptr, TRUE, FALSE, ready_name.c_str()), CloseHandle};
    const Event release{CreateEventA(nullptr, TRUE, FALSE, release_name.c_str()), CloseHandle};
    if (!ready || !release) {
        expect(false, "the test can create descendant synchronization events");
        return;
    }

    auto run = std::async(std::launch::async, [&] {
        return runner.run({.executable = child,
                           .arguments = {"launch-descendant", ready_name, release_name}});
    });
    const bool descendant_ready = WaitForSingleObject(ready.get(), 5000) == WAIT_OBJECT_0;
    expect(descendant_ready, "the descendant holds both inherited output pipes");
    if (descendant_ready) {
        expect(run.wait_for(3s) == std::future_status::ready,
               "the runner returns while the descendant still holds the output pipes");
    }
    expect(SetEvent(release.get()) != 0, "the test can release the descendant");

    const auto result = run.get();
    expect_success(result, "the direct child exits zero");
    expect(result.stdout_text == "direct child stdout\n", "the runner preserves the direct child's stdout");
    expect(result.stderr_text == "direct child stderr\n", "the runner preserves the direct child's stderr");
}

void test_exit_code(IProcessRunner &runner, const std::filesystem::path &child) {
    const auto result = runner.run({.executable = child, .arguments = {"exit-259"}});
    expect(result.exit_code == 259 && result.error_message.empty() && !result.succeeded(),
           "a child that exits with code 259 reports that nonzero exit without an execution error");
}

void test_execution_errors(IProcessRunner &runner, const std::filesystem::path &child) {
    expect_execution_error(runner.run({.executable = child / L"missing.exe"}),
                           "a missing executable reports an execution error");
    expect_execution_error(runner.run({.executable = child, .working_directory = child}),
                           "an invalid working directory reports an execution error");

    const std::array<std::string, 2> invalid_strings = {"\xC3\x28", std::string("a\0b", 3)};
    for (const auto &invalid : invalid_strings) {
        expect_execution_error(runner.run({.executable = child, .arguments = {"arguments", invalid}}),
                               "malformed UTF-8 or embedded NUL in an argument reports an execution error");
        expect_execution_error(runner.run({.executable = child, .arguments = {"environment"},
                                           .environment_overrides = {{invalid, "value"}}}),
                               "malformed UTF-8 or embedded NUL in an environment name reports an execution error");
        expect_execution_error(runner.run({.executable = child, .arguments = {"environment"},
                                           .environment_overrides = {{"HORIZON_PROCESS_TEST_OVERRIDE", invalid}}}),
                               "malformed UTF-8 or embedded NUL in an environment value reports an execution error");
    }
}

}// namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 3) {
        std::cerr << "Expected the child executable and working directory paths\n";
        return 1;
    }

    const std::filesystem::path child = argv[1];
    WindowsProcessRunner windows_runner;
    IProcessRunner &runner = windows_runner;
    test_arguments(runner, child);
    test_working_directory(runner, child, argv[2]);
    test_environment(runner, child);
    test_output(runner, child);
    test_descendant_output_handles(runner, child);
    test_exit_code(runner, child);
    test_execution_errors(runner, child);
    return failures == 0 ? 0 : 1;
}
