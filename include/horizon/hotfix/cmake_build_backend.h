#pragma once

#include "horizon/hotfix/build_backend.h"
#include "horizon/hotfix/process_runner.h"

namespace horizon::hotfix {

// Builds a registered module in an existing CMake build tree. The caller owns
// the runner and serializes builds that use the same tree.
class CMakeBuildBackend final : public IBuildBackend {
public:
    CMakeBuildBackend(IProcessRunner &runner, std::filesystem::path cmake_executable,
                      std::map<std::string, std::string> environment_overrides = {});

    [[nodiscard]] BuildResult build(const BuildRequest &request) override;

private:
    IProcessRunner &runner_;
    std::filesystem::path cmake_executable_;
    std::map<std::string, std::string> environment_overrides_;
};

}// namespace horizon::hotfix
