//
// Created during CUDA→Slang+Vulkan migration.
// Mirrors ast_to_cuda_source.h; inherits AstToCppSource and replaces CUDA/OptiX
// specifics with Slang/Vulkan equivalents. Unlike CUDA it also overrides the
// C++-only constructs AstToCppSource emits (struct alignas, `T v{}` locals,
// references, static_cast, oc_-prefixed literals, matrix `*`).
//

#pragma once

#include "generator/ast_to_cpp_source.h"

namespace ocarina {

class AstToSlangSource : public AstToCppSource {
protected:
    // Set once an emitted function uses THREAD_ID; the oc_thread_id helper is
    // only emitted then, because it relies on WorkgroupSize().
    bool uses_thread_id_{false};

    // Emit the push-constant block, argument-blob accessors and workgroup
    // statics for one kernel. Called once per kernel from _emit_function().
    void _emit_kernel_params(const Function &f) noexcept;
    // Declare every kernel argument as a local loaded from the argument blob.
    void _emit_kernel_argument_loads(const Function &f) noexcept;

    // ── AstToCppSource virtual overrides ──────────────────────────────────
    using AstToCppSource::visit;
    void visit(const BinaryExpr *expr) noexcept override;
    void visit(const CallExpr *expr) noexcept override;
    void visit(const CastExpr *expr) noexcept override;
    void visit(const LiteralExpr *expr) noexcept override;
    void visit(const Type *type) noexcept override;
    void _emit_raytracing_param(const Function &f) noexcept override;
    void _emit_function(const Function &f) noexcept override;
    void _emit_type_name(const Type *type) noexcept override;
    void _emit_arguments(const Function &f) noexcept override;
    void _emit_builtin_var(Variable v) noexcept override;
    void _emit_struct_name(const Type *type) noexcept override;
    void _emit_builtin_vars_define(const Function &f) noexcept override;
    void _emit_variable_define(const Variable &v) noexcept override;
    void _emit_local_var_define(const ScopeStmt *scope) noexcept override;

public:
    explicit AstToSlangSource(bool obfuscation) : AstToCppSource(obfuscation) {}
};

}// namespace ocarina
