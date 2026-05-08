//
// Created by Zero on 2023/12/3.
//

#pragma once

#include "function.h"

namespace ocarina {

class FunctionCorrector : public ExprVisitor, public StmtVisitor {
private:
    /// Function call stack: from kernel (bottom) to the innermost function being corrected (top).
    ocarina::deque<Function *> function_stack_;
    /// CallExpr stack corresponding to function_stack_ with an offset of -1:
    /// call_expr_stack_[i-1] is the CallExpr that invokes function_stack_[i].
    ocarina::deque<CallExpr *> call_expr_stack_;
    /// Records the original outer expressions captured during first correction of each function.
    /// Used for multi-callsite scenarios: when the same Function is invoked by a second CallExpr,
    /// traverse won't rediscover already-processed captures, so these recorded original expressions
    /// are replayed via visit_expr in the current invoker's context to re-run the capture chain.
    /// This is critical for Lambda scenarios — each Lambda invocation creates a different wrapper
    /// Function, and Expression pointers are bound to their respective contexts and cannot be
    /// copied across wrappers.
    ocarina::map<const Function *, ocarina::vector<const Expression *>> captured_outer_exprs_;
    enum Stage {
        /// process capture variable
        ProcessCapture,
        /// Split parameter structure into separate elements
        SplitParamStruct
    };
    Stage stage_{ProcessCapture};

private:
    void visit(const AssignStmt *stmt) override;
    void visit(const BreakStmt *stmt) override {}
    void visit(const CommentStmt *stmt) override {}
    void visit(const ContinueStmt *stmt) override {}
    void visit(const ExprStmt *stmt) override;
    void visit(const ForStmt *stmt) override;
    void visit(const IfStmt *stmt) override;
    void visit(const LoopStmt *stmt) override;
    void visit(const ReturnStmt *stmt) override;
    void visit(const ScopeStmt *stmt) override;
    void visit(const SwitchCaseStmt *stmt) override;
    void visit(const SwitchStmt *stmt) override;
    void visit(const SwitchDefaultStmt *stmt) override;
    void visit(const PrintStmt *stmt) override {}

    void visit(const BinaryExpr *expr) override;
    void visit(const CallExpr *expr) override;
    void visit(const CastExpr *expr) override;
    void visit(const ConditionalExpr *expr) override;
    void visit(const LiteralExpr *expr) override {}
    void visit(const MemberExpr *expr) override;
    void visit(const RefExpr *expr) override {}
    void visit(const SubscriptExpr *expr) override;
    void visit(const UnaryExpr *expr) override;

    [[nodiscard]] Function *current_function() const noexcept { return function_stack_.back(); }
    [[nodiscard]] CallExpr *current_call_expr() const noexcept { return call_expr_stack_.back(); }
    [[nodiscard]] Function *kernel() const noexcept { return function_stack_.front(); }

    void traverse(Function &function) noexcept;
    [[nodiscard]] CallExpr *find_call_expr_for(const Function *func) const noexcept;
    void process_capture(const Expression *&expression, Function *cur_func) noexcept;
    void visit_expr(const Expression *const &expression, Function *cur_func = nullptr) noexcept;

    void process_member_expr(const Expression *&expression,Function *cur_func) noexcept;
    void process_param_struct(const Expression *&expression) noexcept;

    [[nodiscard]] bool is_from_invoker(const Expression *expression) noexcept;
    void capture_from_invoker(const Expression *&expression, Function *cur_func) noexcept;
    void output_from_invoked(const Expression *&expression, Function *cur_func) noexcept;

public:
    explicit FunctionCorrector() = default;
    //todo
    void apply(Function *function, int counter = 0) noexcept;
};

}// namespace ocarina