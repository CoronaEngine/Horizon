//
// Created by Zero on 2024/8/19.
//

#include "compiler.h"

namespace vision::inline hotfix {

fs::path Compiler::cli_path() noexcept {
    return CPP_COMPILER_PATH;
}

Compiler::Handle Compiler::create(const std::string &name) {
    string plugin_name = ocarina::format("vision-hotfix-compiler-{}.dll", name);
    auto module = ocarina::RHIContext::instance().obtain_module(plugin_name);
    auto creator = module->function<Creator *>("create");
    auto deleter = module->function<Deleter *>("destroy");
    return Compiler::Handle{creator(), deleter};
}

Compiler::Handle Compiler::create() {
    return Compiler::create(CPP_COMPILER_TYPE);
}

}// namespace vision::inline hotfix