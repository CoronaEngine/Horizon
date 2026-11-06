//
// Created by Zero on 2024/8/2.
//

#include "build_system.h"
#include "core/util/logging.h"

namespace vision::inline hotfix {

BuildSystem::BuildSystem() {
    DynamicModule::add_search_path(directory() / "bin");
}


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

void BuildSystem::clear() noexcept {
}

fs::path BuildSystem::directory() noexcept {
    return fs::current_path().parent_path();
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

void BuildSystem::create_temp_path(const fs::path &path) noexcept {
    if (fs::exists(path)) {
        return;
    }
    fs::create_directory(path);
}

bool BuildSystem::build_targets(const vector<Target> &targets) const noexcept {
    if (!compiler()->setup_environment()) {
        OC_WARNING("Hotfix toolchain setup failed");
        return false;
    }
    for (const auto &target : targets) {
        OC_INFO_FORMAT("target {} has been modified", target.name);
        for (const fs::path &fn : target.modified_files) {
            OC_INFO_FORMAT("file: {}", fn.string());
        }
        if (!compile(target)) {
            OC_INFO_FORMAT("module {} build failure!", target.name);
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
}

}// namespace vision::inline hotfix
