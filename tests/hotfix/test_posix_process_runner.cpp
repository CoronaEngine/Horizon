#include "horizon/hotfix/posix_process_runner.h"

#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using namespace horizon::hotfix;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

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

void test_arguments(IProcessRunner &runner, const fs::path &child) {
    const ProcessRequest request{
        .executable = child,
        .arguments = {"arguments", "", "plain", "with spaces", "with\ta tab", "line\nbreak",
                      "quote\"inside", R"(back\slash)", "trailing\\", "$HOME;|&*?()'",
                      "text \u6D4B\u8BD5 \U0001F680"}};
    std::string expected;
    for (auto argument = request.arguments.begin() + 1; argument != request.arguments.end(); ++argument) {
        expected.append(*argument).push_back('\0');
    }

    const auto result = runner.run(request);
    expect_success(result, "the argument probe exits zero");
    expect(result.stdout_text == expected, "the child receives each exact argument in order");
}

void test_working_directory(IProcessRunner &runner, const fs::path &child, const fs::path &directory) {
    const auto caller_directory = fs::current_path();
    const auto inherited = runner.run({.executable = child, .arguments = {"working-directory"}});
    expect_success(inherited, "the inherited working directory probe exits zero");
    std::error_code error;
    expect(fs::equivalent(inherited.stdout_text, caller_directory, error),
           "the child inherits the caller's working directory when unspecified");

    const auto executable = fs::relative(child, caller_directory);
    const auto working_directory = fs::relative(directory, caller_directory);
    expect(!executable.empty() && executable.is_relative(), "the child executable is relative to the caller");
    expect(!working_directory.empty() && working_directory.is_relative(),
           "the child's working directory is relative to the caller");
    const auto result = runner.run({.executable = executable, .arguments = {"working-directory"},
                                    .working_directory = working_directory});
    expect_success(result, "the explicit working directory probe exits zero");
    expect(fs::equivalent(result.stdout_text, directory, error),
           "a relative executable is resolved before changing the child's working directory");
    expect(fs::current_path() == caller_directory, "the caller's working directory remains unchanged");
}

void test_environment(IProcessRunner &runner, const fs::path &child) {
    const auto result = runner.run({.executable = child, .arguments = {"environment"},
                                    .environment_overrides = {
                                        {"HORIZON_PROCESS_TEST_OVERRIDE", "override \u6D4B\u8BD5 \U0001F680"},
                                        {"HORIZON_PROCESS_TEST_ADDED", ""}}});
    expect_success(result, "the environment probe exits zero");
    expect(result.stdout_text == "parent\noverride \u6D4B\u8BD5 \U0001F680\ndistinct\n\n",
           "the child receives inherited, overridden and empty variables with case-sensitive names");
    const char *original = std::getenv("HORIZON_PROCESS_TEST_OVERRIDE");
    expect(original && std::string_view(original) == "original", "the caller's environment remains unchanged");
}

void test_streams(IProcessRunner &runner, const fs::path &child) {
    const auto result = runner.run({.executable = child, .arguments = {"streams"}});
    expect_success(result, "the stream probe exits zero");
    expect(result.stdout_text == std::string("stdout\0\r\n", 9) + std::string(512 * 1024, 'o'),
           "stdout preserves all bytes, including embedded NUL and CRLF");
    expect(result.stderr_text == std::string("stderr\0\r\n", 9) + std::string(512 * 1024, 'e'),
           "stderr preserves all bytes separately from stdout");
}

void test_stdin(IProcessRunner &runner, const fs::path &child) {
    const int saved_stdin = dup(STDIN_FILENO);
    if (saved_stdin < 0 && errno != EBADF) {
        expect(false, "the test can save caller stdin");
        return;
    }
    int input[2]{};
    if (pipe(input) != 0) {
        if (saved_stdin >= 0) {
            close(saved_stdin);
        }
        expect(false, "the test can create caller stdin");
        return;
    }
    const bool installed = write(input[1], "x", 1) == 1 && dup2(input[0], STDIN_FILENO) >= 0;
    close(input[1]);
    if (input[0] != STDIN_FILENO) {
        close(input[0]);
    }
    expect(installed, "the test can give the caller readable stdin");
    if (installed) {
        expect_success(runner.run({.executable = child, .arguments = {"stdin-eof"}}),
                       "the child sees EOF even when caller stdin is readable");
        char remaining{};
        expect(read(STDIN_FILENO, &remaining, 1) == 1 && remaining == 'x',
               "the runner does not consume caller stdin");
    }
    if (saved_stdin >= 0) {
        expect(dup2(saved_stdin, STDIN_FILENO) >= 0, "the test can restore caller stdin");
        close(saved_stdin);
    } else {
        close(STDIN_FILENO);
    }
}

class NamedSemaphore {
public:
    explicit NamedSemaphore(std::string semaphore_name)
        : name(std::move(semaphore_name)), handle(sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, 0)) {}

    NamedSemaphore(const NamedSemaphore &) = delete;
    NamedSemaphore &operator=(const NamedSemaphore &) = delete;

    ~NamedSemaphore() {
        if (handle != SEM_FAILED) {
            sem_close(handle);
            sem_unlink(name.c_str());
        }
    }

    const std::string name;
    sem_t *const handle;
};

bool wait_for_readiness(sem_t *semaphore) {
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    do {
        if (sem_trywait(semaphore) == 0) {
            return true;
        }
        if (errno != EAGAIN && errno != EINTR) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

void test_descendant_output_handles(IProcessRunner &runner, const fs::path &child) {
    const auto prefix = "/hz-hotfix-" + std::to_string(getpid());
    const NamedSemaphore ready(prefix + "-ready");
    const NamedSemaphore release(prefix + "-release");
    if (ready.handle == SEM_FAILED || release.handle == SEM_FAILED) {
        expect(false, "the test can create descendant synchronization semaphores");
        return;
    }

    auto run = std::async(std::launch::async, [&] {
        return runner.run({.executable = child, .arguments = {"launch-descendant", ready.name, release.name}});
    });
    const bool descendant_ready = wait_for_readiness(ready.handle);
    expect(descendant_ready, "the descendant holds both inherited output streams");
    if (descendant_ready) {
        expect(run.wait_for(5s) == std::future_status::ready,
               "the runner returns while the descendant still holds the output streams");
    }
    expect(sem_post(release.handle) == 0, "the test can release the descendant");

    const auto result = run.get();
    expect_success(result, "the direct child exits zero");
    expect(result.stdout_text == "direct child stdout\n", "the runner preserves the direct child's stdout");
    expect(result.stderr_text == "direct child stderr\n", "the runner preserves the direct child's stderr");
}

void test_process_errors(IProcessRunner &runner, const fs::path &child) {
    const auto nonzero = runner.run({.executable = child, .arguments = {"exit-7"}});
    expect(nonzero.exit_code == 7 && nonzero.error_message.empty() && !nonzero.succeeded(),
           "a normal nonzero exit is distinct from an execution error");
    const auto exit_127 = runner.run({.executable = child, .arguments = {"exit-127"}});
    expect(exit_127.exit_code == 127 && exit_127.error_message.empty() && !exit_127.succeeded(),
           "a normal exit code of 127 is distinct from a launch error");

    expect_execution_error(runner.run({.executable = child, .arguments = {"signal"}}),
                           "a signal termination reports an error without an exit code");
    expect_execution_error(runner.run({.executable = child / "missing"}),
                           "a missing executable reports a launch error");
    expect_execution_error(runner.run({.executable = child, .working_directory = child}),
                           "an invalid working directory reports a launch error");

    const std::string embedded_nul("a\0b", 3);
    expect_execution_error(runner.run({.executable = child, .arguments = {"arguments", embedded_nul}}),
                           "an embedded NUL in an argument reports an execution error");
    expect_execution_error(runner.run({.executable = child, .arguments = {"environment"},
                                       .environment_overrides = {{"HORIZON_PROCESS_TEST_OVERRIDE", embedded_nul}}}),
                           "an embedded NUL in an environment value reports an execution error");
}

}// namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Expected the child executable and working directory paths\n";
        return 1;
    }
    const fs::path child = argv[1];
    PosixProcessRunner posix_runner;
    IProcessRunner &runner = posix_runner;
    test_arguments(runner, child);
    test_working_directory(runner, child, argv[2]);
    test_environment(runner, child);
    test_streams(runner, child);
    test_stdin(runner, child);
    test_descendant_output_handles(runner, child);
    test_process_errors(runner, child);
    return failures == 0 ? 0 : 1;
}
