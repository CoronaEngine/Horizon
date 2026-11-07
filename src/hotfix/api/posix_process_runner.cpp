#include "horizon/hotfix/posix_process_runner.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <utility>

#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char **environ;
#endif

namespace horizon::hotfix {
namespace {

[[noreturn]] void system_error(const char *operation, int error = errno) {
    throw std::system_error(error, std::generic_category(), operation);
}

class Descriptor {
public:
    explicit Descriptor(int value = -1) noexcept : value_(value) {}
    ~Descriptor() { reset(); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    Descriptor(Descriptor &&other) noexcept : value_(std::exchange(other.value_, -1)) {}
    Descriptor &operator=(Descriptor &&other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.value_, -1));
        }
        return *this;
    }
    int get() const noexcept { return value_; }
    void reset(int value = -1) noexcept {
        // Retrying close after EINTR can close a reused descriptor on Linux.
        if (value_ >= 0) { ::close(value_); }
        value_ = value;
    }

private:
    int value_;
};

int descriptor_control(int descriptor, int command, int value) {
    int result;
    do { result = ::fcntl(descriptor, command, value); } while (result < 0 && errno == EINTR);
    if (result < 0) { system_error("Configure child descriptor"); }
    return result;
}

void make_private(Descriptor &descriptor) {
    if (descriptor.get() < STDERR_FILENO + 1) {
        descriptor.reset(descriptor_control(descriptor.get(), F_DUPFD_CLOEXEC, STDERR_FILENO + 1));
    } else {
        descriptor_control(descriptor.get(), F_SETFD, FD_CLOEXEC);
    }
}

struct Pipe {
    Descriptor reader;
    Descriptor writer;
};

Pipe make_pipe() {
    int descriptors[2];
    int status;
    do {
#if defined(__linux__)
        status = ::pipe2(descriptors, O_CLOEXEC);
#else
        status = ::pipe(descriptors);
#endif
    } while (status < 0 && errno == EINTR);
    if (status < 0) { system_error("Create child pipe"); }
    // Own both ends before any operation that can fail.
    Pipe result{Descriptor{descriptors[0]}, Descriptor{descriptors[1]}};
    make_private(result.reader);
    make_private(result.writer);
    return result;
}

class Child {
public:
    explicit Child(pid_t process) noexcept : process_(process) {}
    ~Child() {
        if (process_ > 0) {
            ::kill(process_, SIGKILL);
            int status;
            while (::waitpid(process_, &status, 0) < 0 && errno == EINTR) {}
        }
    }
    Child(const Child &) = delete;
    Child &operator=(const Child &) = delete;

    bool completed(int &status) {
        pid_t result;
        do { result = ::waitpid(process_, &status, WNOHANG); } while (result < 0 && errno == EINTR);
        if (result < 0) {
            const int error = errno;
            if (error == ECHILD) { process_ = -1; }
            system_error("Wait for child process", error);
        }
        if (result == 0) { return false; }
        process_ = -1;
        return true;
    }

private:
    pid_t process_;
};

void validate_string(std::string_view value) {
    if (value.find('\0') != value.npos) {
        throw std::invalid_argument("Process request contains a NUL character");
    }
}

std::vector<std::string> make_environment(const std::map<std::string, std::string> &overrides) {
    std::map<std::string, std::string> entries;
#if defined(__APPLE__)
    char **inherited = *_NSGetEnviron();
#else
    char **inherited = ::environ;
#endif
    for (char **entry = inherited; entry && *entry; ++entry) {
        const std::string_view text{*entry};
        const auto separator = text.find('=');
        if (separator != text.npos) {
            entries.emplace(text.substr(0, separator), text.substr(separator + 1));
        }
    }
    for (const auto &[name, value] : overrides) {
        validate_string(name);
        validate_string(value);
        if (name.empty() || name.find('=') != name.npos) {
            throw std::invalid_argument("Invalid POSIX environment variable name");
        }
        entries.insert_or_assign(name, value);
    }
    std::vector<std::string> result;
    result.reserve(entries.size());
    for (const auto &[name, value] : entries) { result.push_back(name + '=' + value); }
    return result;
}

std::vector<char *> pointers(std::vector<std::string> &strings) {
    std::vector<char *> result;
    result.reserve(strings.size() + 1);
    for (auto &string : strings) { result.push_back(string.data()); }
    result.push_back(nullptr);
    return result;
}

struct LaunchError {
    int stage;
    int error;
};

// Everything below this boundary in the child is async-signal-safe. All strings,
// pointer arrays and descriptors were prepared before fork.
[[noreturn]] void child_failure(int descriptor, int stage) noexcept {
    const LaunchError failure{stage, errno};
    const char *data = reinterpret_cast<const char *>(&failure);
    std::size_t remaining = sizeof(failure);
    while (remaining != 0) {
        const auto written = ::write(descriptor, data, remaining);
        if (written < 0 && errno == EINTR) { continue; }
        if (written <= 0) { break; }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    ::_exit(127);
}

void redirect_stream(int source, int destination, int error_descriptor) noexcept {
    int result;
    do { result = ::dup2(source, destination); } while (result < 0 && errno == EINTR);
    if (result < 0) { child_failure(error_descriptor, 1); }
}

void check_launch(int descriptor) {
    LaunchError failure{};
    std::size_t received = 0;
    while (received != sizeof(failure)) {
        const auto count = ::read(descriptor, reinterpret_cast<char *>(&failure) + received,
                                  sizeof(failure) - received);
        if (count < 0 && errno == EINTR) { continue; }
        if (count == 0 || (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            if (received != 0) { throw std::runtime_error("Incomplete child launch status"); }
            return;
        }
        if (count < 0) { system_error("Read child launch status"); }
        received += static_cast<std::size_t>(count);
    }
    const char *operation = failure.stage == 1 ? "Redirect child standard streams"
                          : failure.stage == 2 ? "Change child working directory" : "Execute child process";
    system_error(operation, failure.error);
}

bool drain_available(Descriptor &reader, std::string &output) {
    if (reader.get() < 0) { return false; }
    int available;
    int result;
    do { result = ::ioctl(reader.get(), FIONREAD, &available); } while (result < 0 && errno == EINTR);
    if (result < 0) { system_error("Inspect child output"); }
    // A finite snapshot prevents a continuously writing descendant from
    // delaying the direct child's completion. The sole reader is nonblocking.
    std::size_t remaining = static_cast<std::size_t>(std::max(available, 1));
    std::array<char, 8192> buffer;
    bool captured = false;
    while (remaining != 0) {
        const auto count = ::read(reader.get(), buffer.data(), std::min(remaining, buffer.size()));
        if (count < 0 && errno == EINTR) { continue; }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { break; }
        if (count < 0) { system_error("Read child output"); }
        if (count == 0) { reader.reset(); break; }
        output.append(buffer.data(), static_cast<std::size_t>(count));
        remaining -= static_cast<std::size_t>(count);
        captured = true;
    }
    return captured;
}

void run_process(const ProcessRequest &request, ProcessResult &result) {
    if (request.executable.empty()) { throw std::invalid_argument("A process executable is required"); }
    validate_string(request.executable.native());
    validate_string(request.working_directory.native());
    const auto executable = std::filesystem::absolute(request.executable).native();
    const auto directory = request.working_directory.empty() ? std::string{}
                         : std::filesystem::absolute(request.working_directory).native();
    std::vector<std::string> arguments{executable};
    for (const auto &argument : request.arguments) {
        validate_string(argument);
        arguments.push_back(argument);
    }
    auto environment = make_environment(request.environment_overrides);
    auto argv = pointers(arguments);
    auto envp = pointers(environment);
    const char *program = executable.c_str();
    const char *cwd = directory.empty() ? nullptr : directory.c_str();
    char *const *argument_data = argv.data();
    char *const *environment_data = envp.data();

    // POSIX pipe()+fcntl() is not atomic on macOS. Serialize our launches until
    // the parent's inheritable writer ends have closed; never mutate cwd/env.
    static std::mutex launch_mutex;
    std::unique_lock lock{launch_mutex};
    auto stdout_pipe = make_pipe();
    auto stderr_pipe = make_pipe();
    auto launch_pipe = make_pipe();
    int null_descriptor;
    do { null_descriptor = ::open("/dev/null", O_RDONLY | O_CLOEXEC); }
    while (null_descriptor < 0 && errno == EINTR);
    if (null_descriptor < 0) { system_error("Open child standard input"); }
    Descriptor input{null_descriptor};
    make_private(input);
    descriptor_control(stdout_pipe.reader.get(), F_SETFL, O_NONBLOCK);
    descriptor_control(stderr_pipe.reader.get(), F_SETFL, O_NONBLOCK);
    descriptor_control(launch_pipe.reader.get(), F_SETFL, O_NONBLOCK);
    const int input_fd = input.get();
    const int stdout_fd = stdout_pipe.writer.get();
    const int stderr_fd = stderr_pipe.writer.get();
    const int error_fd = launch_pipe.writer.get();
    const std::array readers{stdout_pipe.reader.get(), stderr_pipe.reader.get(), launch_pipe.reader.get()};

    pid_t process;
    do { process = ::fork(); } while (process < 0 && errno == EINTR);
    if (process < 0) { system_error("Fork child process"); }
    if (process == 0) {
        for (int reader : readers) { ::close(reader); }
        redirect_stream(input_fd, STDIN_FILENO, error_fd);
        redirect_stream(stdout_fd, STDOUT_FILENO, error_fd);
        redirect_stream(stderr_fd, STDERR_FILENO, error_fd);
        ::close(input_fd);
        ::close(stdout_fd);
        ::close(stderr_fd);
        if (cwd) {
            int changed;
            do { changed = ::chdir(cwd); } while (changed < 0 && errno == EINTR);
            if (changed < 0) { child_failure(error_fd, 2); }
        }
        do { ::execve(program, argument_data, environment_data); } while (errno == EINTR);
        child_failure(error_fd, 3);
    }
    Child child{process};
    stdout_pipe.writer.reset();
    stderr_pipe.writer.reset();
    launch_pipe.writer.reset();
    input.reset();
    lock.unlock();

    int status;
    for (;;) {
        const bool out = drain_available(stdout_pipe.reader, result.stdout_text);
        const bool err = drain_available(stderr_pipe.reader, result.stderr_text);
        if (child.completed(status)) { break; }
        if (!out && !err) {
            pollfd streams[]{{stdout_pipe.reader.get(), POLLIN, 0},
                             {stderr_pipe.reader.get(), POLLIN, 0}};
            // The finite timeout also observes child exit when descendants
            // still hold otherwise idle stream writers.
            if (::poll(streams, 2, 25) < 0 && errno != EINTR) { system_error("Wait for child output"); }
        }
    }
    drain_available(stdout_pipe.reader, result.stdout_text);
    drain_available(stderr_pipe.reader, result.stderr_text);
    // After reaping, any launch error is already queued. On systems without
    // atomic CLOEXEC pipes, another launcher can hold a writer open past exit.
    check_launch(launch_pipe.reader.get());
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.error_message = "Child process terminated by signal " + std::to_string(WTERMSIG(status));
    } else {
        result.error_message = "Child process terminated without an exit code";
    }
}

}// namespace

ProcessResult PosixProcessRunner::run(const ProcessRequest &request) {
    ProcessResult result;
    try {
        run_process(request, result);
    } catch (const std::runtime_error &error) {
        result.error_message = error.what();
    } catch (const std::invalid_argument &error) {
        result.error_message = error.what();
    }
    return result;
}

}// namespace horizon::hotfix
