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
    bool compile(const vision::CompileOptions &options) noexcept override;

    bool link(const vision::LinkOptions &options, const Target &target,
              const string &extension_objs) noexcept override;
    
    [[nodiscard]] fs::path installation_directory() noexcept override {
        return parent_path(cli_path(), 1);
    }
};

LLVMCompiler::LLVMCompiler() {

}

bool LLVMCompiler::compile(const CompileOptions &options) noexcept {
    return false;
}

bool LLVMCompiler::link(const LinkOptions &options, const Target &target,
                        const string &extension_objs) noexcept {
    return false;
}

}// namespace vision::inline hotfix

VS_EXPORT_API vision::hotfix::LLVMCompiler *create() {
    return ocarina::new_with_allocator<vision::hotfix::LLVMCompiler>();
}

VS_EXPORT_API void destroy(vision::hotfix::LLVMCompiler *obj) {
    ocarina::delete_with_allocator(obj);
}

VS_REGISTER_CURRENT_PATH(0, "horizon-hotfix-compiler-llvm.dll")

VS_REGISTER_HOTFIX(vision::hotfix, LLVMCompiler)
