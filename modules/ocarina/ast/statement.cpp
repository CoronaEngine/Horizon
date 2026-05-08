//
// Created by Zero on 21/04/2022.
//

#include "statement.h"
#include "expression.h"

namespace ocarina {

uint64_t ScopeStmt::compute_hash() const noexcept {
    auto h = Hash64::default_seed;
    for (auto &v : local_vars_) { h = hash64(v.hash(), h); }
    for (auto &&s : statements_) { h = hash64(s->hash(), h); }
    return h;
}

void ScopeStmt::add_var(const ocarina::Variable &variable) noexcept {
    local_vars_.push_back(variable);
}

void ScopeStmt::add_stmt(const Statement *stmt) noexcept {
    statements_.push_back(stmt);
}

uint64_t ReturnStmt::compute_hash() const noexcept {
    return hash64(expression_ == nullptr ? 0ull : expression_->hash());
}
uint64_t ExprStmt::compute_hash() const noexcept {
    return hash64(expression_ == nullptr ? 0ull : expression_->hash());
}

uint64_t AssignStmt::compute_hash() const noexcept {
    auto hl = lhs_->hash();
    auto hr = rhs_->hash();
    return hash64(hl, hr);
}
uint64_t IfStmt::compute_hash() const noexcept {
    auto ret = condition_->hash();
    ret = hash64(ret, true_branch()->hash());
    return hash64(ret, false_branch()->hash());
}
uint64_t CommentStmt::compute_hash() const noexcept {
    return hash64(string_);
}

uint64_t SwitchStmt::compute_hash() const noexcept {
    auto ret = expression_->hash();
    return hash64(ret, body_.hash());
}

SwitchCaseStmt::SwitchCaseStmt(const ocarina::Expression *expression)
    : Statement(Tag::SWITCH_CASE),
      expr_(dynamic_cast<const LiteralExpr *>(expression)) {
    OC_ASSERT(expr_ != nullptr);
}

uint64_t SwitchCaseStmt::compute_hash() const noexcept {
    auto ret = expr_->hash();
    return hash64(ret, body_.hash());
}

uint64_t SwitchDefaultStmt::compute_hash() const noexcept {
    return body_.hash();
}

uint64_t ForStmt::compute_hash() const noexcept {
    auto ret = var_->hash();
    ret = hash64(ret, condition_->hash());
    ret = hash64(ret, step_->hash());
    return hash64(ret, body_.hash());
}

uint64_t LoopStmt::compute_hash() const noexcept {
    return body_.hash();
}

uint64_t PrintStmt::compute_hash() const noexcept {
    uint64_t ret = hash64(fmt_);
    for (const Expression *expr : args_) {
        ret = hash64(ret, expr->hash());
    }
    return ret;
}

}// namespace ocarina
