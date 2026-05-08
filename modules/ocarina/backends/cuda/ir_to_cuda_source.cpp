//
// Created by Z on 2026/4/21.
//

#include "ir_to_cuda_source.h"

namespace ocarina {

void IRToCudaSource::emit(const IRModule &module) noexcept {
    OC_ASSERT(!module.empty());
    const Function &entry = module.entry_function();
    FUNCTION_GUARD(entry)

    if (entry.is_kernel()) {
        _emit_comment(entry.description());
        _emit_newline();
        entry.for_each_header([&](string_view fn) {
            current_scratch() << "#include \"" << fn << "\"\n";
        });
    }

    for (const Type *type : module.structures()) {
        AstToCppSource::visit(type);
    }

    if (entry.is_raytracing_kernel()) {
        _emit_raytracing_param(entry);
    }

    for (const IRFunction &ir_function : module.functions()) {
        const Function &function = ir_function.ast_function();
        if (!has_generated(&function) && !function.description().empty()) {
            _emit_comment(function.description());
            _emit_newline();
        }
        _emit_function(function);
        _emit_newline();
    }
}

}// namespace ocarina