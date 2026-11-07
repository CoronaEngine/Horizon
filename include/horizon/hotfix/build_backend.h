#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace horizon::hotfix {

struct BuildRequest {
    // An already-configured build tree; toolchain/SDK preparation is separate.
    std::filesystem::path build_directory;
    std::string target;
    std::string configuration;
    // Hints only: the backend still owns dependency tracking (including headers).
    std::vector<std::filesystem::path> modified_files;
};

struct BuildArtifacts {
    // Backend-supplied path to the loadable module, with its actual filename.
    // On success it is absolute and refers to a verified, complete output.
    // Staging a versioned copy for loading is the runtime's responsibility.
    std::filesystem::path module_path;
    std::optional<std::filesystem::path> debug_symbols_path;
    std::optional<std::filesystem::path> import_library_path;
};

enum class BuildStatus {
    not_started,
    succeeded,
    failed
};

struct BuildResult {
    BuildStatus status{BuildStatus::not_started};
    BuildArtifacts artifacts;
    // Warnings and other output may be present even when the build succeeds.
    std::string diagnostics;

    // A cheap contract check, not filesystem or ABI validation. The backend
    // must validate artifacts before publishing a successful result.
    [[nodiscard]] bool succeeded() const noexcept {
        return status == BuildStatus::succeeded && !artifacts.module_path.empty();
    }
};

class IBuildBackend {
public:
    virtual ~IBuildBackend() = default;

    // Synchronous; does not load modules or replace runtime objects. Expected
    // build failures return failed status with diagnostics and no new module.
    [[nodiscard]] virtual BuildResult build(const BuildRequest &request) = 0;
};

}// namespace horizon::hotfix
