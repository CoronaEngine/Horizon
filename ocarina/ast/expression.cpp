//
// Created by Zero on 21/04/2022.
//

#include "expression.h"
#include "function.h"

namespace ocarina {

void VariableExpr::_mark(Usage usage) const noexcept {
    variable_.mark_usage(usage);
}

uint64_t VariableExpr::compute_hash() const noexcept {
    return hash64(variable_.hash(), to_underlying(usage()));
}

Usage VariableExpr::usage() const noexcept {
    return context()->variable_usage(variable_.uid());
}

uint64_t LiteralExpr::compute_hash() const noexcept {
    uint64_t ret = ocarina::visit(
        [&](auto &&arg) {
            return hash64(OC_FORWARD(arg));
        },
        value_);
    ret = hash64(ret, value_.index());
    return ret;
}

uint64_t SubscriptExpr::compute_hash() const noexcept {
    uint64_t ret = range_->hash();
    for_each_index([&](const Expression *index) {
        ret = hash64(index->hash(), ret);
    });
    return ret;
}
uint64_t UnaryExpr::compute_hash() const noexcept {
    return hash64(op_, operand_->hash());
}

uint64_t BinaryExpr::compute_hash() const noexcept {
    auto ret = lhs_->hash();
    ret = hash64(op_, ret);
    ret = hash64(ret, rhs_->hash());
    return ret;
}

uint64_t ConditionalExpr::compute_hash() const noexcept {
    auto ret = pred_->hash();
    ret = hash64(True_, ret);
    ret = hash64(ret, False_->hash());
    return ret;
}

MemberExpr::MemberExpr(const Type *type, const Expression *parent,
                       uint16_t index, uint16_t swizzle_size, Variable variable)
    : VariableExpr(Tag::MEMBER, variable.type(), variable), parent_(parent),
      member_index_(index), swizzle_size_(swizzle_size) {}

int MemberExpr::swizzle_index(int idx) const noexcept {
    int shift = (swizzle_size_ - 1 - idx) * 4;
    auto org = 0xf << (swizzle_size_ - 1) * 4;
    auto mask = org >> (idx * 4);
    auto ret = (mask & member_index_) >> shift;
    return ret;
}

uint64_t MemberExpr::compute_hash() const noexcept {
    return hash64(hash64(member_index_, swizzle_size_),
                  parent_->hash(),
                  variable_.hash());
}

CallExpr::CallExpr(const Type *type, const Function *func,
                   list<const Expression *> &&args)
    : Expression(Tag::CALL, type),
      function_(func),
      arguments_(std::move(args)) {
    const_cast<Function *>(function_)->record_call_expression(this);
}

vector<const Function *> CallExpr::call_chain() const noexcept {
    vector<const Function *> ret;
    const Function *func = context();
    while (func) {
        ret.push_back(func);
        const CallExpr *call_expr = func->current_call_expr();
        func = call_expr ? call_expr->context() : nullptr;
    }
    return ret;
}

void CallExpr::append_argument(const Expression *expression) noexcept {
    arguments_.push_back(expression);
}

template<typename Elm>
[[nodiscard]] auto get_list_iter(const ocarina::list<Elm> &lst, uint index) noexcept {
    uint counter = 0;
    for(auto iter = lst.cbegin(); iter != lst.cend(); ++iter, ++counter) {
        if (counter == index) {
            return iter;
        }
    }
    return lst.cend();
}

const Expression *CallExpr::argument(ocarina::uint index) const noexcept {
    auto ret = get_list_iter(arguments_, index);
    return *ret;
}

const CallExpr::Template &CallExpr::template_arg(ocarina::uint index) const noexcept {
    auto ret = get_list_iter(template_args_, index);
    return *ret;
}

uint64_t CallExpr::compute_hash() const noexcept {
    uint64_t ret = function_ ? function_->hash() : Hash64::default_seed;
    ret = hash64(call_op_, ret);
    ret = hash64(function_name_, ret);
    for (auto _template_arg : template_args_) {
        ret = ocarina::visit(
            [&](auto &&arg) {
                return hash64(OC_FORWARD(arg));
            },
            _template_arg);
        ret = hash64(ret, _template_arg.index());
    }
    for (const auto &arg : arguments_) {
        ret = hash64(ret, arg->hash());
    }
    return ret;
}

}// namespace ocarina