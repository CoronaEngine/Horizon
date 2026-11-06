//
// Created by Zero on 2024/8/2.
//

#include "compiler.h"
#include "build_rules.h"
#include "hotfix/support.h"

namespace vision::inline hotfix {
class BuildSystem {
private:
    mutable BuildRules::Handle build_rules_{BuildRules::Handle(nullptr, nullptr)};
    mutable Compiler::Handle compiler_{Compiler::Handle{nullptr, nullptr}};
    [[nodiscard]] bool compile(const Target &target) const noexcept;
    [[nodiscard]] bool link(const Target &target) const noexcept;

public:
    BuildSystem();
    void clear() noexcept;
    [[nodiscard]] static fs::path directory() noexcept;
    [[nodiscard]] bool build_targets(const vector<Target> &targets) const noexcept;
    [[nodiscard]] BuildRules::Handle& build_rules() const noexcept;
    [[nodiscard]] Compiler::Handle& compiler() const noexcept;
    static void create_temp_path(const fs::path &path) noexcept;
};

}// namespace vision::inline hotfix
