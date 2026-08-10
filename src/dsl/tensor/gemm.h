//
// Created by Z on 22/04/2026.
//

#pragma once

#include "common.h"
#include "dsl/api/func.h"

namespace ocarina {

struct GemmOp {
    uint id{};
    GemmDesc desc;

    [[nodiscard]] bool valid() const noexcept {
        return desc.valid();
    }
};

[[nodiscard]] inline GemmOp gemm(GemmDesc desc) noexcept {
    OC_ASSERT(desc.valid());
    uint id = register_gemm_desc(desc);
    auto expr = Function::current()->call_builtin(nullptr, CallOp::GEMM, {}, {id});
    Function::current()->expr_statement(expr);
    return GemmOp{id, std::move(desc)};
}

}// namespace ocarina