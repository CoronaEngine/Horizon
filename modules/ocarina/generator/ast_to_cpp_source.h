//
// Created by Zero on 08/06/2022.
//

#pragma once

#include "source_emitter.h"
#include "ast/expression.h"
#include "ast/statement.h"
#include "core/type.h"

namespace ocarina {

class OC_GENERATOR_API AstToCppSource : public SourceEmitter, protected ExprVisitor, protected StmtVisitor, protected TypeVisitor {
protected:
    ocarina::set<uint64_t> generated_func_;
    ocarina::set<const Type *> generated_struct_;

protected:
    void visit(const BreakStmt *stmt) noexcept override;
    void visit(const ContinueStmt *stmt) noexcept override;
    void visit(const ReturnStmt *stmt) noexcept override;
    void visit(const ScopeStmt *stmt) noexcept override;
    void visit(const IfStmt *stmt) noexcept override;
    void visit(const CommentStmt *stmt) noexcept override;
    void visit(const LoopStmt *stmt) noexcept override;
    void visit(const ExprStmt *stmt) noexcept override;
    void visit(const SwitchStmt *stmt) noexcept override;
    void visit(const SwitchCaseStmt *stmt) noexcept override;
    void visit(const SwitchDefaultStmt *stmt) noexcept override;
    void visit(const AssignStmt *stmt) noexcept override;
    void visit(const ForStmt *stmt) noexcept override;
    void visit(const PrintStmt *stmt) noexcept override;

    void visit(const UnaryExpr *expr) noexcept override;
    void visit(const BinaryExpr *expr) noexcept override;
    void visit(const ocarina::ConditionalExpr *expr) override;
    void visit(const MemberExpr *expr) noexcept override;
    void visit(const SubscriptExpr *expr) noexcept override;
    void visit(const LiteralExpr *expr) noexcept override;
    void visit(const RefExpr *expr) noexcept override;
    void visit(const CallExpr *expr) noexcept override;
    void visit(const CastExpr *expr) noexcept override;

    void visit(const Type *type) noexcept override;

protected:
    [[nodiscard]] bool has_generated(const Function *func) const noexcept;
    void add_generated(const Function *func) noexcept;
    [[nodiscard]] bool has_generated(const Type *type) const noexcept;
    void add_generated(const Type *type) noexcept;
    virtual void _emit_types_define() noexcept;
    virtual void _emit_variable_define(const Variable &v) noexcept;
    virtual void _emit_type_name(const Type *type) noexcept;
    virtual void _emit_raytracing_param(const Function &f) noexcept {}
    virtual void _emit_function(const Function &f) noexcept;
    virtual void _emit_arguments(const Function &f) noexcept;
    virtual void _emit_argument(const Variable &v) noexcept;
    virtual void _emit_body(const Function &f) noexcept;
    void _emit_comment(const string &content) noexcept override;
    virtual void _emit_local_var_define(const ScopeStmt *scope) noexcept;
    virtual void _emit_builtin_vars_define(const Function &f) noexcept;
    virtual void _emit_builtin_var(Variable v) noexcept {}
    virtual void _emit_variable_name(Variable v) noexcept;
    virtual void _emit_statements(ocarina::span<const Statement *const> stmts) noexcept;

public:
    explicit AstToCppSource(bool obfuscation): SourceEmitter(obfuscation) {}
    void emit(const Function &func) noexcept override;
};

}// namespace ocarina