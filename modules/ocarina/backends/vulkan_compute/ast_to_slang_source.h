//
// specifics with Slang/Vulkan equivalents. Unlike DSL it also overrides the
// C++-only constructs AstToCppSource emits (struct alignas, `T v{}` locals,
// references, static_cast, oc_-prefixed literals, matrix `*`).
//

#pragma once

#include "generator/ast_to_cpp_source.h"

namespace ocarina {

class AstToSlangSource : public AstToCppSource {
protected:
    // Set once an emitted function uses THREAD_ID; the oc_thread_id helper is
    // only emitted then, with the compiled workgroup dimensions.
    bool uses_thread_id_{false};
    bool inline_parameters_{false};
    uint32_t uniform_parameter_bytes_{};
    Scratch storage_functions_;
    set<const Type *> storage_types_;
    set<string> value_helpers_;
    // In a storage-reference callable variant, these argument variables hold
    // physical addresses. Each access uses the host ABI instead of copy-in/out.
    set<uint> storage_reference_arguments_;
    // Kernel aggregates proven to be immutable leaf-only reads. Offsets use
    // the unchanged host argument ABI; captured resources remain eager.
    const Function *lazy_parameter_function_{};
    map<uint, size_t> lazy_parameter_offsets_;
    [[nodiscard]] bool _lazy_parameter_offset(const Expression *expr, size_t &offset) noexcept;
    void _emit_converted_value(const Type *target, const Expression *value) noexcept;
    void _emit_custom_call(const CallExpr *expr) noexcept;
    void _emit_matrix_constructor(const CallExpr *expr) noexcept;
    void _emit_default_value(const Type *type) noexcept;
    void _emit_array_operation(const Type *result, const list<const Expression *> &arguments,
                               string_view operation, bool infix = false, bool prefix = false) noexcept;
    void _emit_function_body(const Function &f) noexcept;
    void _emit_storage_functions(const Type *type) noexcept;
    void _emit_storage_fields(const Type *type, size_t offset, const string &value, bool load) noexcept;
    void _emit_buffer_address(const SubscriptExpr *expr) noexcept;
    [[nodiscard]] bool _is_storage_reference(const Expression *expr) const noexcept;
    void _emit_storage_address(const Expression *expr) noexcept;
    void _emit_storage_load(const Expression *expr) noexcept;

    // Emit the push-constant block, argument-blob accessors and workgroup
    // statics for one kernel. Called once per kernel from _emit_function().
    void _emit_kernel_params(const Function &f) noexcept;
    // Declare eager arguments; defer proven read-only aggregate leaf accesses.
    void _emit_kernel_argument_loads(const Function &f) noexcept;
    void _emit_parameter_load(const Type *type, size_t offset) noexcept;

    // ── AstToCppSource virtual overrides ──────────────────────────────────
    using AstToCppSource::visit;
    void visit(const UnaryExpr *expr) noexcept override;
    void visit(const BinaryExpr *expr) noexcept override;
    void visit(const SubscriptExpr *expr) noexcept override;
    void visit(const MemberExpr *expr) noexcept override;
    void visit(const RefExpr *expr) noexcept override;
    void visit(const AssignStmt *stmt) noexcept override;
    void visit(const IfStmt *stmt) noexcept override;
    void visit(const LoopStmt *stmt) noexcept override;
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
    void _emit_statements(span<const Statement *const> statements) noexcept override;

public:
    explicit AstToSlangSource(bool obfuscation) : AstToCppSource(obfuscation) {}
};

}// namespace ocarina
