//
// Created by Zero on 2024/8/19.
//

#include "hotfix/compiler.h"
#include "hotfix/hotfix.h"

namespace vision::inline hotfix {


class LLVMCompiler : public Compiler,
                     public RuntimeObject {
public:
    LLVMCompiler();
    void compile(const vision::CompileOptions &options) noexcept override;

    void link(const vision::LinkOptions &options, const Target &target,
              const string &extension_objs, const CmdProcess::callback_t &callback) noexcept override;
    
    [[nodiscard]] fs::path installation_directory() noexcept override {
        return parent_path(cli_path(), 1);
    }
};

LLVMCompiler::LLVMCompiler() {

}

void LLVMCompiler::compile(const CompileOptions &options) noexcept {
}

void LLVMCompiler::link(const LinkOptions &options, const Target &target,
                        const string &extension_objs, const CmdProcess::callback_t &callback) noexcept {
}

}// namespace vision::inline hotfix

VS_EXPORT_API vision::hotfix::LLVMCompiler *create() {
    return ocarina::new_with_allocator<vision::hotfix::LLVMCompiler>();
}

VS_EXPORT_API void destroy(vision::hotfix::LLVMCompiler *obj) {
    ocarina::delete_with_allocator(obj);
}

VS_REGISTER_CURRENT_PATH(0, "vision-hotfix-compiler-llvm.dll")

VS_REGISTER_HOTFIX(vision::hotfix, LLVMCompiler)