//
// Created during CUDA→Slang+Vulkan migration.
// Mirrors ir_to_cuda_source.h.
//

#pragma once

#include "generator/ir_module.h"
#include "ast_to_slang_source.h"

namespace ocarina {

class IRToSlangSource final : public AstToSlangSource {
public:
    explicit IRToSlangSource(bool obfuscation) noexcept
        : AstToSlangSource(obfuscation) {}

    void emit(const IRModule &module) noexcept;
};

}// namespace ocarina
