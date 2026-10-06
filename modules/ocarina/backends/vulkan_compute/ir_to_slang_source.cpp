//
// Created during CUDA→Slang+Vulkan migration.
// Mirrors ir_to_cuda_source.cpp.
//

#include "ir_to_slang_source.h"
#include "generator/ir_module.h"

namespace ocarina {

void IRToSlangSource::emit(const IRModule &module) noexcept {
    // Emit structure types first so that callable/kernel bodies can reference them.
    for (const auto *type : module.structures()) {
        visit(type);
    }
    // Emit callable functions in dependency order, then the entry kernel.
    for (const auto &ir_func : module.functions()) {
        _emit_function(ir_func.ast_function());
    }
    _emit_function(module.entry_function());
}

}// namespace ocarina
