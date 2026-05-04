//
// Created by Z on 2026/4/21.
//

#pragma once

#include "ir_module.h"

namespace ocarina {

class AstToIR {
private:
    void _lower_function(const Function &function, IRModule &module) const noexcept;

public:
    [[nodiscard]] IRModule lower(const Function &function) const noexcept;
};

}// namespace ocarina