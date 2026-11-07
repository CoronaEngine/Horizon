//
// Created by Zero on 2023/12/3.
//

#include "function_corrector.h"

#include <iterator>

namespace ocarina {

void FunctionCorrector::traverse(Function &function) noexcept {
    visit(function.body());
    function.correct_used_structures();
    if (function.is_callable()) {
        bool valid = function.check_context();
        OC_ERROR_IF_NOT(valid, "FunctionCorrector error: invalid function ", function.description().c_str());
    }
}

/// Find the CallExpr corresponding to func in function_stack_.
/// function_stack_[i] is invoked by call_expr_stack_[i-1], so return call_expr_stack_[i-1].
CallExpr *FunctionCorrector::find_call_expr_for(const Function *func) const noexcept {
    for (int i = function_stack_.size() - 1; i >= 0; --i) {
        if (function_stack_.at(i) == func) {
            return call_expr_stack_.at(i - 1);
        }
    }
    OC_ERROR("Cannot find call expr for function");
    return nullptr;
}

uint FunctionCorrector::hidden_argument_index(const CallExpr &call, const Expression *source) noexcept {
    const Function &callee = *call.function();
    // Function::all_arguments lists bound resources after explicit parameters.
    const auto prefix = callee.arguments().size() + callee.captured_resources().size();
    return static_cast<uint>(prefix) + callee.expr_to_argument_index_.at(source);
}

void FunctionCorrector::set_hidden_argument(CallExpr &call, uint index, const Expression *argument) noexcept {
    // An output can be discovered before this call's captures are replayed.
    if (call.arguments_.size() <= index) {
        call.arguments_.resize(index + 1, nullptr);
    }
    *std::next(call.arguments_.begin(), index) = argument;
}

void FunctionCorrector::apply(Function *function, int counter) noexcept {
    function_stack_.push_back(function);
    traverse(*current_function());
    if (current_function()->is_kernel()) {
        /// Split parameter structure into separate elements
        stage_ = SplitParamStruct;
        current_function()->splitting_arguments();
        traverse(*current_function());
        stage_ = ProcessCapture;
    }
    if (function->is_kernel()) {
        bool valid = function->check_context();
        OC_ERROR_IF_NOT(valid, "FunctionCorrector error: invalid function ", function->description().c_str());
    }
    function_stack_.pop_back();
}

bool FunctionCorrector::is_from_invoker(const Expression *expression) noexcept {
    return std::find(function_stack_.begin(), function_stack_.end(),
                     expression->context()) != function_stack_.end();
}

void FunctionCorrector::process_capture(const Expression *&expression, Function *cur_func) noexcept {
    if (expression->context() == cur_func) {
        return;
    }
    OC_ERROR_IF(expression->tag() == Expression::Tag::REF &&
                    static_cast<const RefExpr *>(expression)->variable().tag() == Variable::Tag::SHARED,
                "Shared arrays cannot be captured by callables");
    if (is_from_invoker(expression)) {
        capture_from_invoker(expression, cur_func);
    } else {
        output_from_invoked(expression, cur_func);
    }
}

void FunctionCorrector::visit_expr(const Expression *const &expression, Function *cur_func) noexcept {
    cur_func = cur_func == nullptr ? current_function() : cur_func;
    if (expression == nullptr) {
        return;
    }

    switch (expression->tag()) {
        case Expression::Tag::REF: {
            static_cast<const VariableExpr *>(expression)->variable().mark_used();
            process_capture(const_cast<const Expression *&>(expression), cur_func);
            break;
        }
        case Expression::Tag::MEMBER: {
            static_cast<const VariableExpr *>(expression)->variable().mark_used();
            process_member_expr(const_cast<const Expression *&>(expression), cur_func);
            break;
        }
        default: {
            expression->accept(*this);
            break;
        }
    }
}

/// Capture an outer variable from the invoker into the current function (cur_func).
///
/// Workflow:
///   1. Check whether cur_func has already mapped this outer expression (deduplication).
///   2. If not yet mapped, record the original outer expression in captured_outer_exprs_
///      so it can be replayed when the same function is invoked by a second CallExpr.
///   3. Find the CallExpr that calls cur_func, recursively process the expression
///      in the invoker's context (visit_expr may trigger further captures up the chain),
///      then bind the expression at the corresponding argument index of that CallExpr.
void FunctionCorrector::capture_from_invoker(const Expression *&expression, Function *cur_func) noexcept {
    bool contain;
    const Expression *old_expr = expression;
    /// Create a formal parameter mapping for the outer expression in cur_func
    /// and return the mapped expression. contain == true means it was already captured.
    expression = cur_func->mapping_captured_argument(expression, &contain);
    if (contain) {
        return;
    }
    /// Record the original outer expression for replay in multi-callsite scenarios
    captured_outer_exprs_[cur_func].push_back(old_expr);
    /// Find the CallExpr that invokes cur_func
    CallExpr *call_expr = find_call_expr_for(cur_func);
    const uint index = hidden_argument_index(*call_expr, old_expr);
    /// Recursively process the expression in the invoker's context (may trigger upper-level captures)
    visit_expr(old_expr, const_cast<Function *>(call_expr->context()));
    set_hidden_argument(*call_expr, index, old_expr);
}

namespace detail {

void correct_usage(const CallExpr *expr) noexcept {
    vector<Variable> formal_arguments = expr->function()->all_arguments();
    OC_ERROR_IF(expr->arguments().size() != formal_arguments.size());

    auto bit_or = [](Usage lhs, Usage rhs) {
        return Usage(to_underlying(lhs) | to_underlying(rhs));
    };

    for (int i = 0; i < formal_arguments.size(); ++i) {
        Variable formal_arg = formal_arguments[i];
        Usage &formal_arg_usage = const_cast<Usage &>(expr->function()->variable_usage(formal_arg.uid()));

        const Expression *act_arg = expr->argument(i);
        OC_ERROR_IF_NOT(act_arg, "Missing corrected argument ", i);
        Usage act_arg_usage = act_arg->usage();

        Usage combined = bit_or(formal_arg_usage, act_arg_usage);
        if (act_arg->type()->is_resource()) {
            formal_arg_usage = combined;
            act_arg->mark(combined);
        } else {
            act_arg->mark(combined);
        }
    }
}

}// namespace detail

void FunctionCorrector::visit(const CallExpr *const_expr) {
    CallExpr *expr = const_cast<CallExpr *>(const_expr);
    /// First, recursively process existing actual argument expressions
    for (const Expression *const &arg : expr->arguments_) {
        visit_expr(arg);
    }
    if (!expr->function_) {
        return;
    }
    /// Push the current CallExpr onto the stack and enter the callee for correction
    call_expr_stack_.push_back(expr);
    apply(const_cast<Function *>(expr->function_));
    call_expr_stack_.pop_back();

    // The body no longer refers to its original captures after the first visit.
    // Replay them in this caller's context and preserve the formal argument order.
    auto captures = captured_outer_exprs_.find(expr->function());
    if (captures != captured_outer_exprs_.end()) {
        for (const Expression *original_outer : captures->second) {
            const uint index = hidden_argument_index(*expr, original_outer);
            if (index >= expr->arguments_.size() || !expr->argument(index)) {
                const Expression *propagated = original_outer;
                visit_expr(propagated);
                set_hidden_argument(*expr, index, propagated);
            }
        }
    }
    detail::correct_usage(expr);
}

// Follow every incoming call edge. Each caller needs its own output reference,
// and all paths end at the same kernel local for this source expression.
void FunctionCorrector::propagate_output(Function *callee, const Expression *source) noexcept {
    for (const CallExpr *const_call : callee->all_call_expr()) {
        auto *call = const_cast<CallExpr *>(const_call);
        auto *caller = const_cast<Function *>(call->context());
        const uint index = hidden_argument_index(*call, source);
        if (caller == kernel()) {
            set_hidden_argument(*call, index, caller->mapping_local_variable(source, nullptr));
        } else {
            bool already_mapped;
            set_hidden_argument(*call, index, caller->mapping_output_argument(source, &already_mapped));
            if (!already_mapped) {
                propagate_output(caller, source);
            }
        }
    }
}

void FunctionCorrector::output_from_invoked(const Expression *&expression, Function *cur_func) noexcept {
    auto *callee = const_cast<Function *>(expression->context());
    bool already_mapped;
    callee->append_output_argument(expression, &already_mapped);
    if (!already_mapped) {
        propagate_output(callee, expression);
    }
    if (cur_func == kernel()) {
        expression = kernel()->outer_to_local(expression);
    } else if (cur_func->expr_to_argument_index_.contains(expression)) {
        expression = cur_func->outer_to_argument(expression);
    } else {
        expression = kernel()->outer_to_local(expression);
        capture_from_invoker(expression, cur_func);
    }
}

void FunctionCorrector::visit(const ScopeStmt *scope) {
    for (const Statement *stmt : scope->statements()) {
        stmt->accept(*this);
    }
}

void FunctionCorrector::visit(const AssignStmt *stmt) {
    visit_expr(stmt->lhs_);
    OC_ERROR_IF(stmt->lhs()->type()->is_resource());
    stmt->lhs()->mark(Usage::WRITE);
    visit_expr(stmt->rhs_);
}

void FunctionCorrector::visit(const ExprStmt *stmt) {
    visit_expr(stmt->expression_);
}

void FunctionCorrector::visit(const ForStmt *stmt) {
    visit_expr(stmt->var_);
    visit_expr(stmt->condition_);
    visit_expr(stmt->step_);
    stmt->body()->accept(*this);
}

void FunctionCorrector::visit(const IfStmt *stmt) {
    visit_expr(stmt->condition_);
    stmt->true_branch()->accept(*this);
    stmt->false_branch()->accept(*this);
}

void FunctionCorrector::visit(const LoopStmt *stmt) {
    stmt->body()->accept(*this);
}

void FunctionCorrector::visit(const ReturnStmt *stmt) {
    visit_expr(stmt->expression_);
}

void FunctionCorrector::visit(const SwitchCaseStmt *stmt) {
    visit_expr(stmt->expr_);
    stmt->body()->accept(*this);
}

void FunctionCorrector::visit(const SwitchStmt *stmt) {
    visit_expr(stmt->expression_);
    stmt->body()->accept(*this);
}

void FunctionCorrector::visit(const SwitchDefaultStmt *stmt) {
    stmt->body()->accept(*this);
}

void FunctionCorrector::visit(const BinaryExpr *expr) {
    visit_expr(expr->lhs_);
    visit_expr(expr->rhs_);
}

void FunctionCorrector::visit(const CastExpr *expr) {
    visit_expr(expr->expression_);
}

void FunctionCorrector::visit(const ConditionalExpr *expr) {
    visit_expr(expr->pred_);
    visit_expr(expr->True_);
    visit_expr(expr->False_);
}

void FunctionCorrector::visit(const MemberExpr *expr) {
    OC_ERROR_IF(stage_ == ProcessCapture);
}

void FunctionCorrector::process_member_expr(const Expression *&expression, Function *cur_func) noexcept {
    auto member_expr = dynamic_cast<const MemberExpr *>(expression);
    switch (stage_) {
        case ProcessCapture:
            if (member_expr->context() == cur_func) {
                visit_expr(member_expr->parent_, cur_func);
            } else {
                process_capture(expression, cur_func);
            }
            break;
        case SplitParamStruct:
            if (member_expr->parent()->type()->is_param_struct()) {
                process_param_struct(expression);
            } else {
                visit_expr(member_expr->parent_);
            }
            break;
        default:
            break;
    }
}

void FunctionCorrector::process_param_struct(const Expression *&expression) noexcept {
    const MemberExpr *member_expr = dynamic_cast<const MemberExpr *>(expression);
    const Expression *parent = member_expr->parent();
    vector<int> path;
    path.push_back(member_expr->member_index());
    do {
        const MemberExpr *member_parent = dynamic_cast<const MemberExpr *>(parent);
        if (member_parent) {
            parent = member_parent->parent();
            path.push_back(member_parent->member_index());
        }
    } while (parent->is_member());
    const RefExpr *ref_expr = dynamic_cast<const RefExpr *>(parent);
    path.push_back(ref_expr->variable().uid());
    std::reverse(path.begin(), path.end());
    kernel()->replace_param_struct_member(path, expression);
}

void FunctionCorrector::visit(const SubscriptExpr *expr) {
    for (const Expression *const &index : expr->indexes_) {
        visit_expr(index);
    }
    visit_expr(expr->range_);
}

void FunctionCorrector::visit(const UnaryExpr *expr) {
    visit_expr(expr->operand_);
}

}// namespace ocarina
