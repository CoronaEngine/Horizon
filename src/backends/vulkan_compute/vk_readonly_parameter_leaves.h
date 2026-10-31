#pragma once

#include "ast/function.h"

namespace ocarina {

// Only enable lazy parameters when the final AST proves that every use is a
// read of a leaf field. Root usage alone misses writes to MemberExpr variables.
struct VkReadOnlyParameterLeaves {
    const Function &function;
    set<uint> candidates;
    bool complete{true};

    explicit VkReadOnlyParameterLeaves(const Function &f) noexcept : function(f) {
        for (const Variable &arg : f.arguments()) {
            if (arg.type()->is_structure() && !arg.type()->is_param_struct())
                candidates.insert(arg.uid());
        }
    }

    static const RefExpr *root(const Expression *expr) noexcept {
        if (!expr) return nullptr;
        switch (expr->tag()) {
            case Expression::Tag::REF: return static_cast<const RefExpr *>(expr);
            case Expression::Tag::MEMBER: return root(static_cast<const MemberExpr *>(expr)->parent());
            case Expression::Tag::SUBSCRIPT: return root(static_cast<const SubscriptExpr *>(expr)->range());
            default: return nullptr;
        }
    }

    void reject(const Expression *expr) noexcept {
        if (const auto *ref = root(expr); ref && ref->context() == &function)
            candidates.erase(ref->variable().uid());
    }

    void expression(const Expression *expr, bool parent_only = false) noexcept {
        if (!expr) return;
        if (const auto *ref = root(expr); ref && ref->context() == &function) {
            // Preserve whole-record snapshots, array value semantics and all
            // local writes. A parent's appearance in a leaf path is not a read
            // of that whole aggregate.
            if (is_write(expr->usage()) || (!parent_only &&
                    (expr->type()->is_structure() || expr->type()->is_array()))) reject(expr);
        }
        switch (expr->tag()) {
            case Expression::Tag::REF:
            case Expression::Tag::LITERAL: break;
            case Expression::Tag::MEMBER:
                expression(static_cast<const MemberExpr *>(expr)->parent(), true); break;
            case Expression::Tag::SUBSCRIPT: {
                const auto *subscript = static_cast<const SubscriptExpr *>(expr);
                // Array-valued parameter paths conservatively keep eager mode;
                // vectors and matrices already have expression constructors.
                expression(subscript->range());
                subscript->for_each_index([&](const Expression *index) { expression(index); });
                break;
            }
            case Expression::Tag::UNARY:
                expression(static_cast<const UnaryExpr *>(expr)->operand()); break;
            case Expression::Tag::BINARY: {
                const auto *binary = static_cast<const BinaryExpr *>(expr);
                expression(binary->lhs()); expression(binary->rhs()); break;
            }
            case Expression::Tag::CAST:
                expression(static_cast<const CastExpr *>(expr)->expression()); break;
            case Expression::Tag::CONDITIONAL: {
                const auto *conditional = static_cast<const ConditionalExpr *>(expr);
                expression(conditional->pred()); expression(conditional->true_());
                expression(conditional->false_()); break;
            }
            case Expression::Tag::CALL: {
                const auto *call = static_cast<const CallExpr *>(expr);
                const auto parameters = call->function() ? call->function()->all_arguments() : vector<Variable>{};
                if (call->function() && parameters.size() != call->arguments().size()) complete = false;
                uint index = 0;
                for (const Expression *arg : call->arguments()) {
                    // __ref must retain an l-value even if its callee happens
                    // to read only. Unknown named calls have no AST signature.
                    if (!call->function_name().empty() ||
                        (index < parameters.size() &&
                         (parameters[index].tag() == Variable::Tag::REFERENCE ||
                          is_write(parameters[index].usage())))) reject(arg);
                    const auto op = call->call_op();
                    if ((op == CallOp::COORDINATE_SYSTEM && index >= 1u) ||
                        (op == CallOp::MAKE_NORMAL_TANGENT && index >= 2u) ||
                        ((op == CallOp::ATOMIC_ADD || op == CallOp::ATOMIC_SUB ||
                          op == CallOp::ATOMIC_EXCH || op == CallOp::ATOMIC_CAS) && index == 0u) ||
                        op == CallOp::GEMM) reject(arg);
                    expression(arg);
                    ++index;
                }
                break;
            }
            default: complete = false; break;
        }
    }

    void statement(const Statement *stmt) noexcept {
        switch (stmt->tag()) {
            case Statement::Tag::SCOPE:
                for (const Statement *child : static_cast<const ScopeStmt *>(stmt)->statements()) statement(child);
                break;
            case Statement::Tag::ASSIGN: {
                const auto *assign = static_cast<const AssignStmt *>(stmt);
                reject(assign->lhs()); expression(assign->lhs()); expression(assign->rhs()); break;
            }
            case Statement::Tag::EXPR:
                expression(static_cast<const ExprStmt *>(stmt)->expression()); break;
            case Statement::Tag::RETURN:
                expression(static_cast<const ReturnStmt *>(stmt)->expression()); break;
            case Statement::Tag::IF: {
                const auto *branch = static_cast<const IfStmt *>(stmt);
                expression(branch->condition()); statement(branch->true_branch()); statement(branch->false_branch()); break;
            }
            case Statement::Tag::FOR: {
                const auto *loop = static_cast<const ForStmt *>(stmt);
                reject(loop->var()); expression(loop->var()); expression(loop->condition());
                expression(loop->step()); statement(loop->body()); break;
            }
            case Statement::Tag::LOOP: statement(static_cast<const LoopStmt *>(stmt)->body()); break;
            case Statement::Tag::SWITCH: {
                const auto *branch = static_cast<const SwitchStmt *>(stmt);
                expression(branch->expression()); statement(branch->body()); break;
            }
            case Statement::Tag::SWITCH_CASE: {
                const auto *branch = static_cast<const SwitchCaseStmt *>(stmt);
                expression(branch->expression()); statement(branch->body()); break;
            }
            case Statement::Tag::SWITCH_DEFAULT:
                statement(static_cast<const SwitchDefaultStmt *>(stmt)->body()); break;
            case Statement::Tag::PRINT:
                for (const Expression *arg : static_cast<const PrintStmt *>(stmt)->args()) expression(arg);
                break;
            case Statement::Tag::BREAK:
            case Statement::Tag::CONTINUE:
            case Statement::Tag::COMMENT: break;
            default: complete = false; break;
        }
    }
};


}// namespace ocarina
