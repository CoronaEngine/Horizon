//
// Created by Zero on 2024/8/2.
//

#include "compiler.h"
#include "build_rules.h"
#include "cmd_process.h"
#include "hotfix/support.h"

namespace vision::inline hotfix {
class BuildSystem {
private:
    mutable BuildRules::Handle build_rules_{BuildRules::Handle(nullptr, nullptr)};
    mutable Compiler::Handle compiler_{Compiler::Handle{nullptr, nullptr}};

public:
    BuildSystem();
    void clear() noexcept;
    [[nodiscard]] static fs::path directory() noexcept;
    void build_targets(const vector<Target> &targets,
                       const std::function<void(bool, Target)> &callback = nullptr) const noexcept;
    void build_target(const Target &target,
                      const CmdProcess::callback_t& callback = nullptr) const noexcept;
    [[nodiscard]] BuildRules::Handle& build_rules() const noexcept;
    [[nodiscard]] Compiler::Handle& compiler() const noexcept;
    static void create_temp_path(const fs::path &path) noexcept;
    [[nodiscard]] bool is_working() const noexcept { return compiler()->is_working(); }
    void compile(const Target &target) const noexcept;
    void link(const Target &target, const CmdProcess::callback_t &callback = nullptr) const noexcept;
};

}// namespace vision::inline hotfix