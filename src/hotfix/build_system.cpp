//
// Created by Zero on 2024/8/2.
//

#include "build_system.h"
#include "horizon/core/logging.h"

#include <fmt/format.h>

#include <chrono>

namespace vision::inline hotfix {

bool BuildSystem::set_build_backend(std::shared_ptr<::horizon::hotfix::IBuildBackend> backend,
                                    fs::path build_directory, string configuration) {
    if (!backend || build_directory.empty()) {
        return false;
    }
    std::error_code error;
    build_directory = fs::absolute(build_directory, error);
    if (error || !fs::is_directory(build_directory, error)) {
        return false;
    }
    backend_ = std::move(backend);
    backend_directory_ = std::move(build_directory);
    backend_configuration_ = std::move(configuration);
    staging_session_.clear();
    staged_generation_ = 0;
    return true;
}

bool BuildSystem::build_with_backend(vector<Target> &targets) {
    vector<fs::path> modules;
    modules.reserve(targets.size());
    for (const auto &target : targets) {
        const auto result = backend_->build({
            .build_directory = backend_directory_,
            .target = target.name,
            .configuration = backend_configuration_,
            .modified_files = target.modified_files,
        });
        if (!result.diagnostics.empty()) {
            CFW_LOG_INFO("{}", result.diagnostics);
        }
        if (!result.succeeded() || !result.artifacts.module_path.is_absolute()) {
            CFW_LOG_INFO("module {} build failure!", target.name);
            return false;
        }
        modules.push_back(result.artifacts.module_path);
    }

    // Prepare copies only after every target has built successfully.
    if (staging_session_.empty()) {
        const auto root = backend_directory_ / "hotfix-staged";
        fs::create_directories(root);
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        for (size_t attempt = 0;; ++attempt) {
            auto session = root / fmt::format("session_{}_{}", stamp, attempt);
            if (fs::create_directory(session)) {
                staging_session_ = std::move(session);
                break;
            }
        }
    }
    for (size_t index = 0; index < targets.size(); ++index) {
        const auto version = staging_session_ / fmt::format("generation_{}", ++staged_generation_);
        if (!fs::create_directory(version)) {
            CFW_LOG_WARNING("Hotfix staging directory already exists for {}", targets[index].name);
            return false;
        }
        auto copy = version / modules[index].filename();
        fs::copy_file(modules[index], copy);
        targets[index].module_path = std::move(copy);
    }
    return true;
}

bool BuildSystem::build_targets(vector<Target> &targets) noexcept {
    if (backend_) {
        try {
            return build_with_backend(targets);
        } catch (const std::exception &error) {
            CFW_LOG_WARNING("Hotfix build or staging failed: {}", error.what());
            return false;
        }
    }
    CFW_LOG_WARNING("Hotfix requires a configured build backend");
    return false;
}

}// namespace vision::inline hotfix
