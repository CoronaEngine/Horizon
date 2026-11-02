//
// Created by Z on 2026/4/21.
//

#pragma once

#include "generator/ir_module.h"
#include "ast_to_cuda_source.h"

namespace ocarina {

class IRToCudaSource final : public AstToCudaSource {
public:
    explicit IRToCudaSource(bool obfuscation) noexcept
        : AstToCudaSource(obfuscation) {}

    void emit(const IRModule &module) noexcept;
};

}// namespace ocarina