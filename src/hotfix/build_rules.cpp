//
// Created by Zero on 2024/8/27.
//

#include "build_rules.h"

namespace vision::inline hotfix {

CompileOptions BuildRules::compile_options(const string &src_fn) const noexcept {
    return compile_map_.at(src_fn);
}

LinkOptions BuildRules::link_options(const std::string &target_fn) const noexcept {
    return link_map_.at(target_fn);
}

string BuildRules::obj_path(std::string_view cpp_path) const noexcept {
    return cpp_to_obj_.at(cpp_path);
}

BuildRules::Handle BuildRules::create(const std::string &name) {
    string plugin_name = ocarina::format("vision-hotfix-rules_parser-{}.dll", name);
    auto module = ocarina::RHIContext::instance().obtain_module(plugin_name);
    auto creator = module->function<BuildRules::Creator *>("create");
    auto deleter = module->function<BuildRules::Deleter *>("destroy");
    return Handle{creator(), deleter};
}

}// namespace vision::inline hotfix