//
// Created by Zero on 2023/12/3.
//

#include "function_corrector.h"

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
    auto bit_or = [](Usage lhs, Usage rhs) {
        return Usage(to_underlying(lhs) | to_underlying(rhs));
    };
    const Expression *old_expr = expression;
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
///      then append the expression as an actual argument to that CallExpr.
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
    /// Recursively process the expression in the invoker's context (may trigger upper-level captures)
    visit_expr(old_expr, const_cast<Function *>(call_expr->context()));
    /// Append the expression as an actual argument, corresponding to cur_func's new formal parameter
    call_expr->append_argument(old_expr);
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

/// Process a function call expression. This is the core entry point for the multi-callsite fix.
///
/// Problem:
///   When the same Function (e.g. a Callable) is called multiple times, the first call
///   traverses normally and discovers all captures/outputs, appending formal params to the
///   Function and actual args to the CallExpr. But on the second call, the function body
///   has already been corrected (captures/outputs replaced with parameter references), so
///   traverse won't rediscover them, leaving the second CallExpr with too few arguments
///   and causing correct_usage to crash on argument count mismatch.
///
/// Fix strategy (two steps):
///   1. Capture args: replay the original outer expressions from captured_outer_exprs_
///      via visit_expr to re-run the capture chain in the current invoker's context.
///      Each callsite generates expressions belonging to its own context.
///      (Essential for Lambda, where different wrapper expressions are not interchangeable.)
///   2. Output args: copy directly from the first fully-corrected CallExpr.
///      Output args are created by mapping_output_argument/mapping_local_variable on the
///      invoker or kernel, independent of wrapper context, so they can be safely copied.
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

    /// === Multi-callsite completion logic ===
    /// If the Function was already corrected by a previous callsite,
    /// the Function's formal param count > this CallExpr's actual arg count.
    const Function *func = expr->function_;
    size_t expected = func->all_arguments().size();
    size_t actual = expr->arguments().size();
    if (actual < expected) {
        /// Step 1: Replay capture arguments.
        /// Retrieve the original outer expressions recorded during first correction,
        /// re-run visit_expr -> capture_from_invoker in the current invoker's context
        /// to generate expressions belonging to the correct context.
        /// For Lambda, each wrapper follows its own capture path, producing expressions
        /// tied to its own context, preventing cross-wrapper pointer check_context failures.
        auto it = captured_outer_exprs_.find(func);
        if (it != captured_outer_exprs_.end()) {
            for (const Expression *original_outer : it->second) {
                const Expression *propagated = original_outer;
                visit_expr(propagated);
                expr->append_argument(propagated);
            }
        }
        /// Step 2: Fill in output arguments.
        /// After capture args are filled, remaining missing args are output args
        /// (produced by output_from_invoked). Their expressions (RefExpr) are created
        /// by mapping_output_argument/mapping_local_variable on the invoker or kernel,
        /// with context belonging to the caller rather than the callee Function,
        /// so they can be safely copied from the first fully-corrected CallExpr.
        actual = expr->arguments().size();
        if (actual < expected) {
            for (const CallExpr *first_ce : func->all_call_expr()) {
                if (first_ce != expr && first_ce->arguments().size() == expected) {
                    for (size_t i = actual; i < expected; ++i) {
                        expr->append_argument(first_ce->argument(static_cast<uint>(i)));
                    }
                    break;
                }
            }
        }
    }
    detail::correct_usage(expr);
}

/// Propagate an output variable from an invoked function to the outside.
///
/// Scenario: when a variable created inside a function body (e.g. new Var<int>) needs
/// to be used externally, it must be passed up layer by layer along the call chain,
/// ultimately creating a local variable in the kernel to receive it.
///
/// Multi-callsite note:
///   output_from_invoked uses current_call_expr() (i.e. all_call_expr_.back()).
///   When a Function is called multiple times, the first call correctly handles output.
///   The second CallExpr's output args are filled by step 2 in visit(CallExpr)
///   (copy from the first corrected CallExpr). Output expressions belong to the
///   invoker/kernel context and can be safely copied across callsites.
void FunctionCorrector::output_from_invoked(const Expression *&expression, Function *cur_func) noexcept {
    auto context = const_cast<Function *>(expression->context());
    Function *invoked = context;
    Function *invoker = nullptr;
    bool in_path = false;
    /// Create an output formal parameter in the invoked function for this expression
    context->append_output_argument(expression, nullptr);
    const Expression *org_expr = expression;

    /// Walk up the call chain layer by layer, propagating the output variable to the kernel.
    /// At each layer: create a formal parameter / local variable mapping in the invoker
    /// and append it as an actual argument to that layer's CallExpr.
    while (true) {
        CallExpr *call_expr = const_cast<CallExpr *>(invoked->current_call_expr());
        invoker = const_cast<Function *>(call_expr->context());
        if (invoked == cur_func || invoker == cur_func) {
            in_path = true;
        }
        bool contain;
        const RefExpr *ref_expr;
        if (invoker == kernel()) {
            /// Reached kernel layer: create a local variable to receive the output
            ref_expr = invoker->mapping_local_variable(org_expr, &contain);
            if (!contain) {
                call_expr->append_argument(ref_expr);
            }
            break;
        } else {
            /// Intermediate layer: create an output parameter mapping, continue upward
            ref_expr = invoker->mapping_output_argument(org_expr, &contain);
            if (!contain) {
                call_expr->append_argument(ref_expr);
            }
        }
        invoked = invoker;
    }
    /// The while loop above only processed the single call chain rooted at current_call_expr().
    /// The same function may be invoked by multiple CallExprs (e.g. direct calls + calls inside
    /// $outline), and those other CallExprs also need the output argument appended.
    /// Since the while loop has already created the mappings on each invoker,
    /// we just look up the existing mapping and append it to any unprocessed CallExprs.
    {
        Function *level = context;
        while (level != kernel()) {
            size_t expected_args = level->all_arguments().size();
            for (const CallExpr *ce : level->all_call_expr()) {
                if (ce->arguments().size() >= expected_args) {
                    continue;
                }
                CallExpr *extra_ce = const_cast<CallExpr *>(ce);
                Function *ce_invoker = const_cast<Function *>(extra_ce->context());
                bool contain;
                const RefExpr *ref;
                if (ce_invoker == kernel()) {
                    ref = ce_invoker->mapping_local_variable(org_expr, &contain);
                } else {
                    ref = ce_invoker->mapping_output_argument(org_expr, &contain);
                }
                extra_ce->append_argument(ref);
            }
            auto *primary_ce = level->current_call_expr();
            level = const_cast<Function *>(primary_ce->context());
        }
    }
    if (in_path) {
        if (cur_func == kernel()) {
            expression = cur_func->outer_to_local(org_expr);
        } else {
            expression = cur_func->outer_to_argument(org_expr);
        }
    } else {
        const RefExpr *kernel_expr = kernel()->outer_to_local(expression);
        expression = kernel_expr;
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