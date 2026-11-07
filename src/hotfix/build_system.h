//
// Created by Zero on 2024/8/2.
//

#pragma once

#include "file_tool.h"
#include "hotfix/support.h"
#include "horizon/hotfix/build_backend.h"

#include <memory>

#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
#include "compiler.h"
#include "build_rules.h"
#endif

namespace vision::inline hotfix {
class BuildSystem {
private:
#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
    mutable BuildRules::Handle build_rules_{BuildRules::Handle(nullptr, nullptr)};
    mutable Compiler::Handle compiler_{Compiler::Handle{nullptr, nullptr}};
    [[nodiscard]] bool compile(const Target &target) const noexcept;
    [[nodiscard]] bool link(const Target &target) const noexcept;
#endif
    std::shared_ptr<::horizon::hotfix::IBuildBackend> backend_;
    fs::path backend_directory_;
    string backend_configuration_;
    fs::path staging_session_;
    size_t staged_generation_{};
    [[nodiscard]] bool build_with_backend(vector<Target> &targets);

public:
    BuildSystem();
    void clear() noexcept;
    [[nodiscard]] static fs::path directory() noexcept;
    [[nodiscard]] bool set_build_backend(std::shared_ptr<::horizon::hotfix::IBuildBackend> backend,
                                         fs::path build_directory, string configuration);
    [[nodiscard]] bool build_targets(vector<Target> &targets) noexcept;
#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
    [[nodiscard]] BuildRules::Handle& build_rules() const noexcept;
    [[nodiscard]] Compiler::Handle& compiler() const noexcept;
#endif
    static void create_temp_path(const fs::path &path) noexcept;
};

}// namespace vision::inline hotfix
