//
// Created by Zero on 2024/8/2.
//

#include "build_system.h"
#include "core/util/logging.h"

namespace vision::inline hotfix {

BuildSystem::BuildSystem() = default;


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

void BuildSystem::compile(const Target &target) const noexcept {
    for (const fs::path &item : target.modified_files) {
        const CompileOptions &options = build_rules()->compile_options(item.string());
        compiler()->compile(options);
    }
}

void BuildSystem::link(const Target &target, const CmdProcess::callback_t &callback) const noexcept {
    fs::path fn("bin");
    fn = fn / target.name;
    const LinkOptions &options = build_rules()->link_options(fn.string());
    vector<string> extension_objs = build_rules()->obj_paths(ModuleInterface::src_path());
    compiler()->link(options, target, FileTool::files_string(extension_objs), callback);
}

void BuildSystem::create_temp_path(const fs::path &path) noexcept {
    if (fs::exists(path)) {
        return;
    }
    fs::create_directory(path);
}

void BuildSystem::build_target(const Target &target,
                               const CmdProcess::callback_t& callback) const noexcept {
    create_temp_path(target.temp_directory());
    compiler()->setup_environment();
    compile(target);
    link(target,callback);
}

void BuildSystem::build_targets(const vector<Target> &targets,
                                const std::function<void(bool, Target)> &callback) const noexcept {
    for (const auto &target : targets) {
        OC_INFO_FORMAT("target {} has been modified", target.name);
        for (const fs::path &fn : target.modified_files) {
            OC_INFO_FORMAT("file: {}", fn.string());
        }
        build_target(target, [target, callback](const string &cmd, bool success) {
            callback(success, target);
        });
    }
}

}// namespace vision::inline hotfix