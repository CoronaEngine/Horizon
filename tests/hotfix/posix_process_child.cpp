#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void write_environment_value(const char *name) {
    const auto *value = std::getenv(name);
    std::cout << (value ? value : "<unset>") << '\n';
}

void write_streams() {
    std::cout.write("stdout\0\r\n", 9);
    std::cerr.write("stderr\0\r\n", 9);
    const std::string out(64 * 1024, 'o');
    const std::string err(64 * 1024, 'e');
    // Each stream fills a pipe while the child still has more data for the other.
    for (int chunk = 0; chunk < 8; ++chunk) {
        std::cout << out;
        std::cerr << err;
    }
}

int launch_descendant(const char *ready_name, const char *release_name) {
    auto *ready = sem_open(ready_name, 0);
    if (ready == SEM_FAILED) {
        return 1;
    }
    auto *release = sem_open(release_name, 0);
    if (release == SEM_FAILED) {
        sem_close(ready);
        return 1;
    }

    const auto child = fork();
    if (child == 0) {
        // Bound the lifetime even if the parent test is interrupted before releasing us.
        std::signal(SIGALRM, SIG_DFL);
        alarm(20);
        if (fcntl(STDOUT_FILENO, F_GETFD) < 0 || fcntl(STDERR_FILENO, F_GETFD) < 0 || sem_post(ready) != 0) {
            _exit(1);
        }
        int status;
        do {
            status = sem_wait(release);
        } while (status != 0 && errno == EINTR);
        _exit(status == 0 ? 0 : 1);
    }

    sem_close(release);
    sem_close(ready);
    if (child < 0) {
        return 1;
    }
    std::cout << "direct child stdout\n";
    std::cerr << "direct child stderr\n";
    return std::cout && std::cerr ? 0 : 1;
}

}// namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        return 1;
    }

    const std::string_view mode = argv[1];
    if (mode == "arguments") {
        for (int index = 2; index < argc; ++index) {
            std::cout << argv[index];
            std::cout.put('\0');
        }
    } else if (mode == "working-directory") {
        std::cout << std::filesystem::current_path().native();
    } else if (mode == "environment") {
        write_environment_value("HORIZON_PROCESS_TEST_INHERITED");
        write_environment_value("HORIZON_PROCESS_TEST_OVERRIDE");
        write_environment_value("Horizon_Process_Test_Override");
        write_environment_value("HORIZON_PROCESS_TEST_ADDED");
    } else if (mode == "streams") {
        write_streams();
    } else if (mode == "stdin-eof") {
        char byte{};
        return read(STDIN_FILENO, &byte, 1) == 0 ? 0 : 1;
    } else if (mode == "launch-descendant" && argc == 4) {
        return launch_descendant(argv[2], argv[3]);
    } else if (mode == "exit-7") {
        return 7;
    } else if (mode == "exit-127") {
        return 127;
    } else if (mode == "signal") {
        std::signal(SIGTERM, SIG_DFL);
        std::raise(SIGTERM);
        return 1;
    } else {
        return 1;
    }
    return std::cout && std::cerr ? 0 : 1;
}
