#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace horizon::hotfix {

struct ProcessRequest {
    std::filesystem::path executable;
    // UTF-8 arguments, passed individually rather than interpreted as shell text.
    std::vector<std::string> arguments;
    // Empty means inherit the caller's working directory.
    std::filesystem::path working_directory;
    // UTF-8 overrides merged with the inherited environment, not a replacement.
    std::map<std::string, std::string> environment_overrides;
};

struct ProcessResult {
    // Present when the OS reports an exit code after termination. Absent before
    // execution, after launch/wait failure, or when no exit code is available.
    std::optional<int> exit_code;
    std::string stdout_text;
    std::string stderr_text;
    // Process execution errors, not compiler diagnostics written to stderr.
    std::string error_message;

    [[nodiscard]] bool succeeded() const noexcept {
        return exit_code.has_value() && *exit_code == 0 && error_message.empty();
    }
};

class IProcessRunner {
public:
    virtual ~IProcessRunner() = default;

    // Synchronous: returns after exit or execution failure. Scheduling belongs
    // to the caller. Expected execution failures are reported in the result.
    [[nodiscard]] virtual ProcessResult run(const ProcessRequest &request) = 0;
};

}// namespace horizon::hotfix
