//
// Created by Zero on 14/01/2023.
//

#include "../types/rtx.h"
#include "../api/stmt_builder.h"
#include "../diagnostics/printer.h"

namespace ocarina::detail {

Var<bool> Ref<Accel>::trace_occlusion(const Var<Ray> &ray) const noexcept {
    const CallExpr *expr = Function::current()->call_builtin(Type::of<bool>(),
                                                             CallOp::TRACE_OCCLUSION,
                                                             {expression(), OC_EXPR(ray)});
    return eval<bool>(expr);
}

Var<TriangleHit> Ref<Accel>::trace_closest(const Var<Ray> &ray) const noexcept {
    const CallExpr *expr = Function::current()->call_builtin(Type::of<TriangleHit>(),
                                                             CallOp::TRACE_CLOSEST,
                                                             {expression(), OC_EXPR(ray)});
    return eval<TriangleHit>(expr);
}

}// namespace ocarina::detail