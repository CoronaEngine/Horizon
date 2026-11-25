//
// Created by Zero on 2024/8/2.
//

#pragma once

#include "file_tool.h"
#include "hotfix/support.h"
#include "horizon/hotfix/build_backend.h"

#include <memory>

namespace vision::inline hotfix {
class BuildSystem {
private:
    std::shared_ptr<::horizon::hotfix::IBuildBackend> backend_;
    fs::path backend_directory_;
    string backend_configuration_;
    fs::path staging_session_;
    size_t staged_generation_{};
    [[nodiscard]] bool build_with_backend(vector<Target> &targets);

public:
    [[nodiscard]] bool set_build_backend(std::shared_ptr<::horizon::hotfix::IBuildBackend> backend,
                                         fs::path build_directory, string configuration);
    [[nodiscard]] bool build_targets(vector<Target> &targets) noexcept;
};

}// namespace vision::inline hotfix
