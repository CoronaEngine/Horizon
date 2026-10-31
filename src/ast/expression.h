//
// Created by Zero on 21/04/2022.
//

#pragma once

#include <utility>

#include "ast_node.h"

namespace ocarina {

class UnaryExpr;
class BinaryExpr;
class MemberExpr;
class SubscriptExpr;
class LiteralExpr;
class RefExpr;
class CallExpr;
class CastExpr;
class ConditionalExpr;

class Function;

struct OC_AST_API ExprVisitor {
    virtual void visit(const UnaryExpr *) = 0;
    virtual void visit(const BinaryExpr *) = 0;
    virtual void visit(const MemberExpr *) = 0;
    virtual void visit(const SubscriptExpr *) = 0;
    virtual void visit(const ConditionalExpr *) = 0;
    virtual void visit(const LiteralExpr *) = 0;
    virtual void visit(const RefExpr *) = 0;
    virtual void visit(const CallExpr *) = 0;
    virtual void visit(const CastExpr *) = 0;
};

class OC_AST_API Expression : public ASTNode, public concepts::Noncopyable, public Hashable {
public:
    enum struct Tag : uint32_t {
        UNARY,
        BINARY,
        MEMBER,
        SUBSCRIPT,
        LITERAL,
        REF,
        CONSTANT,
        CALL,
        CAST,
        CONDITIONAL,
        SAMPLE
    };

private:
    const Type *type_;
    Tag tag_;

protected:
    virtual void _mark(Usage usage) const noexcept {};

public:
    explicit Expression(Tag tag, const Type *type) noexcept : type_{type}, tag_{tag} {}
    virtual ~Expression() noexcept = default;
    [[nodiscard]] auto type() const noexcept { return type_; }
    [[nodiscard]] auto tag() const noexcept { return tag_; }
    [[nodiscard]] bool is_ref() const noexcept { return tag() == Tag::REF; }
    [[nodiscard]] bool is_member() const noexcept { return tag() == Tag::MEMBER; }
    [[nodiscard]] bool is_arithmetic() const noexcept {
        return tag() == Tag::BINARY || tag() == Tag::UNARY;
    }
    virtual void accept(ExprVisitor &) const = 0;
    [[nodiscard]] virtual Usage usage() const noexcept { return Usage::NONE; }
    void mark(Usage usage) const noexcept {
        _mark(usage);
    }
};

#define OC_MAKE_EXPRESSION_COMMON   \
    friend class FunctionCorrector; \
    void accept(ExprVisitor &visitor) const override { visitor.visit(this); }

class OC_AST_API UnaryExpr : public Expression {
private:
    const Expression *operand_;
    UnaryOp op_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    UnaryExpr(const Type *type, UnaryOp op, const Expression *expression)
        : Expression(Tag::UNARY, type), op_(op), operand_(expression) {}
    OC_MAKE_CHECK_CONTEXT(Expression, operand_)
    [[nodiscard]] auto operand() const noexcept { return operand_; }
    [[nodiscard]] auto op() const noexcept { return op_; }
    [[nodiscard]] Usage usage() const noexcept override { return operand_->usage(); }
    OC_MAKE_EXPRESSION_COMMON
};

class OC_AST_API BinaryExpr : public Expression {
private:
    const Expression *lhs_;
    const Expression *rhs_;
    BinaryOp op_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    BinaryExpr(const Type *type, BinaryOp op, const Expression *lhs, const Expression *rhs) noexcept
        : Expression{Tag::BINARY, type}, lhs_{lhs}, rhs_{rhs}, op_{op} {
        lhs_->mark(Usage::READ);
        rhs_->mark(Usage::READ);
    }
    OC_MAKE_CHECK_CONTEXT(Expression, lhs_, rhs_)
    [[nodiscard]] auto lhs() const noexcept { return lhs_; }
    [[nodiscard]] auto rhs() const noexcept { return rhs_; }
    [[nodiscard]] auto op() const noexcept { return op_; }
    OC_MAKE_EXPRESSION_COMMON
};

class OC_AST_API ConditionalExpr : public Expression {
private:
    const Expression *pred_{};
    const Expression *True_{};
    const Expression *False_{};

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    ConditionalExpr(const Type *type, const Expression *pred,
                    const Expression *t, const Expression *f)
        : Expression(Tag::CONDITIONAL, type),
          pred_(pred), True_(t), False_(f) {
        pred_->mark(Usage::READ);
        True_->mark(Usage::READ);
        False_->mark(Usage::READ);
    }
    OC_MAKE_CHECK_CONTEXT(Expression, pred_, True_, False_)
    [[nodiscard]] const Expression *pred() const noexcept { return pred_; }
    [[nodiscard]] const Expression *true_() const noexcept { return True_; }
    [[nodiscard]] const Expression *false_() const noexcept { return False_; }
    OC_MAKE_EXPRESSION_COMMON
};

class OC_AST_API SubscriptExpr : public Expression {
private:
    using IndexVector = vector<const Expression *>;

private:
    const Expression *range_{};
    IndexVector indexes_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;
    void _mark(ocarina::Usage usage) const noexcept override {
        range_->mark(usage);
    }

public:
    SubscriptExpr(const Type *type, const Expression *range, const Expression *index)
        : Expression(Tag::SUBSCRIPT, type), range_(range) {
        indexes_.push_back(index);
    }
    bool check_context(const Function *ctx) const noexcept override {
        return detail::check_context((range_), ctx) && detail::check_context((indexes_), ctx);
    }
    SubscriptExpr(const Type *type, const Expression *range, IndexVector indexes)
        : Expression(Tag::SUBSCRIPT, type), range_(range), indexes_(ocarina::move(indexes)) {
    }

    template<typename Func>
    void for_each_index(Func &&func) const noexcept {
        for (const Expression *index : indexes_) {
            func(index);
        }
    }
    [[nodiscard]] Usage usage() const noexcept override { return range_->usage(); }
    [[nodiscard]] const Expression *range() const noexcept { return range_; }
    [[nodiscard]] const Expression *index(int i) const noexcept { return indexes_.at(i); }
    OC_MAKE_EXPRESSION_COMMON
};

class OC_AST_API LiteralExpr : public Expression {
public:
    using value_type = basic_literal_t;

private:
    value_type value_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    LiteralExpr(const Type *type, value_type value)
        : Expression(Tag::LITERAL, type), value_(value) {}
    [[nodiscard]] decltype(auto) value() const noexcept { return value_; }
    bool check_context(const Function *ctx) const noexcept override { return true; }
    OC_MAKE_EXPRESSION_COMMON
};

class OC_AST_API CastExpr : public Expression {
private:
    CastOp cast_op_;
    const Expression *expression_;

private:
    void _mark(Usage) const noexcept override {}
    uint64_t compute_hash() const noexcept override {
        return hash64(cast_op_, expression_->hash());
    }

public:
    CastExpr(const Type *type, CastOp op, const Expression *expression) noexcept
        : Expression(Tag::CAST, type), cast_op_(op), expression_(expression) {
        expression_->mark(Usage::READ);
    }
    OC_MAKE_CHECK_CONTEXT(Expression, expression_)
    [[nodiscard]] CastOp cast_op() const noexcept { return cast_op_; }
    [[nodiscard]] const Expression *expression() const noexcept { return expression_; }
    [[nodiscard]] Usage usage() const noexcept override { return expression_->usage(); }
    OC_MAKE_EXPRESSION_COMMON
};

class VariableExpr : public Expression {
protected:
    Variable variable_;

protected:
    void _mark(Usage usage) const noexcept override;
    uint64_t compute_hash() const noexcept override;

public:
    VariableExpr(Tag tag, const Type *type, Variable variable)
        : Expression(tag, type), variable_(std::move(variable)) {}
    OC_MAKE_MEMBER_GETTER(variable, &)
    [[nodiscard]] Usage usage() const noexcept override;
};

class OC_AST_API RefExpr : public VariableExpr {
public:
    explicit RefExpr(const Variable &v) noexcept
        : VariableExpr(Tag::REF, v.type(), v) {}
    OC_MAKE_EXPRESSION_COMMON
};

class OC_AST_API MemberExpr : public VariableExpr {
private:
    const Expression *parent_{nullptr};
    uint16_t member_index_{0};
    uint16_t swizzle_size_{0};

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    MemberExpr(const Type *type, const Expression *parent, uint16_t index,
               uint16_t swizzle_size, Variable variable);
    OC_MAKE_CHECK_CONTEXT(Expression, parent_)
    [[nodiscard]] auto member_index() const noexcept { return member_index_; }
    [[nodiscard]] bool is_swizzle() const noexcept { return swizzle_size_ != 0; }
    [[nodiscard]] int swizzle_size() const noexcept { return swizzle_size_; }
    [[nodiscard]] int swizzle_index(int idx) const noexcept;
    [[nodiscard]] const Expression *parent() const noexcept { return parent_; }
    OC_MAKE_EXPRESSION_COMMON
};

class OC_AST_API CallExpr : public Expression {
public:
    using Template = std::variant<const Type *, uint>;

private:
    ocarina::list<const Expression *> arguments_;
    const Function *function_{};
    CallOp call_op_{CallOp::CUSTOM};
    string_view function_name_{};
    ocarina::list<Template> template_args_;

private:
    void _mark(Usage) const noexcept override {}
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    CallExpr(const Type *type, const Function *func,
             ocarina::list<const Expression *> &&args);
    CallExpr(const Type *type, CallOp op,
             ocarina::list<const Expression *> &&args,
             ocarina::list<Template> &&t_args = {})
        : Expression(Tag::CALL, type), call_op_(op),
          arguments_(std::move(args)), template_args_(ocarina::move(t_args)) {}
    CallExpr(const Type *type, string_view func_name,
             ocarina::list<const Expression *> &&args)
        : Expression(Tag::CALL, type), function_name_(ocarina::move(func_name)),
          arguments_(ocarina::move(args)) {}
    OC_MAKE_CHECK_CONTEXT(Expression, arguments_)
    [[nodiscard]] auto arguments() const noexcept { return arguments_; }
    [[nodiscard]] const Expression *argument(uint index) const noexcept;
    [[nodiscard]] auto template_args() const noexcept { return template_args_; }
    [[nodiscard]] const Template &template_arg(uint index) const noexcept;
    OC_MAKE_MEMBER_GETTER(function_name, &)
    void append_argument(const Expression *expression) noexcept;
    [[nodiscard]] vector<const Function *> call_chain() const noexcept;
    [[nodiscard]] auto call_op() const noexcept { return call_op_; }
    [[nodiscard]] auto function() const noexcept { return function_; }
    OC_MAKE_EXPRESSION_COMMON
};

#undef OC_MAKE_EXPRESSION_COMMON

}// namespace ocarina