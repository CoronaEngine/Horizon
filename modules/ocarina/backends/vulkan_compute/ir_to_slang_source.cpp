//
// Created during CUDA→Slang+Vulkan migration.
// Mirrors ir_to_cuda_source.cpp.
//

#include "ir_to_slang_source.h"
#include "generator/ir_module.h"

namespace ocarina {

void IRToSlangSource::emit(const IRModule &module) noexcept {
    const Function &entry = module.entry_function();
    FUNCTION_GUARD(entry)
    entry.for_each_header([&](string_view header) {
        current_scratch() << "#include \"" << header << "\"\n";
    });
    // Emit structure types first so that callable/kernel bodies can reference them.
    for (const auto *type : module.structures()) {
        visit(type);
    }
    // Emit callable functions in dependency order, then the entry kernel.
    for (const auto &ir_func : module.functions()) {
        _emit_function(ir_func.ast_function());
    }
}

}// namespace ocarina
