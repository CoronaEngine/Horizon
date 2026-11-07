//
// Created by Zero on 2024/8/2.
//

#include "build_system.h"
#include "horizon/core/logging.h"

#include <fmt/format.h>

#include <chrono>

namespace vision::inline hotfix {

BuildSystem::BuildSystem() {
#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
    DynamicModule::add_search_path(directory() / "bin");
#endif
}

#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
Compiler::Handle &BuildSystem::compiler() const noexcept {
    if (compiler_ == nullptr) {
        compiler_ = Compiler::create();
    }
    return compiler_;
}

BuildRules::Handle &BuildSystem::build_rules() const noexcept {
    if (build_rules_ == nullptr) {
        build_rules_ = BuildRules::create();
    }
    return build_rules_;
}

bool BuildSystem::compile(const Target &target) const noexcept {
    for (const fs::path &item : target.modified_files) {
        const CompileOptions &options = build_rules()->compile_options(item.string());
        if (!compiler()->compile(options)) {
            return false;
        }
    }
    return true;
}

bool BuildSystem::link(const Target &target) const noexcept {
    fs::path fn(target.name);
    if (!fn.has_parent_path()) {
        fn = fs::path("bin") / fn;
    }
    const LinkOptions &options = build_rules()->link_options(fn.string());
    vector<string> extension_objs = build_rules()->obj_paths(ModuleInterface::src_path());
    return compiler()->link(options, target, FileTool::files_string(extension_objs));
}
#endif

void BuildSystem::clear() noexcept {
}

fs::path BuildSystem::directory() noexcept {
    return fs::current_path().parent_path();
}

void BuildSystem::create_temp_path(const fs::path &path) noexcept {
    if (fs::exists(path)) {
        return;
    }
    fs::create_directory(path);
}

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
#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
    if (!compiler()->setup_environment()) {
        CFW_LOG_WARNING("Hotfix toolchain setup failed");
        return false;
    }
    for (const auto &target : targets) {
        CFW_LOG_INFO("target {} has been modified", target.name);
        for (const fs::path &fn : target.modified_files) {
            CFW_LOG_INFO("file: {}", fn.string());
        }
        if (!compile(target)) {
            CFW_LOG_INFO("module {} build failure!", target.name);
            return false;
        }
    }
    for (const auto &target : targets) {
        create_temp_path(target.temp_directory());
        if (!link(target)) {
            OC_INFO_FORMAT("module {} build failure!", target.name);
            return false;
        }
    }
    return true;
#else
    CFW_LOG_WARNING("Hotfix requires a configured build backend");
    return false;
#endif
}

}// namespace vision::inline hotfix
