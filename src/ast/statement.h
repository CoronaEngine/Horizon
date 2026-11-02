//
// Created by Zero on 21/04/2022.
//

#pragma once

#include "ast_node.h"

namespace ocarina {

class ScopeStmt;
class BreakStmt;
class ContinueStmt;
class ReturnStmt;
class ExprStmt;
class AssignStmt;
class IfStmt;
class CommentStmt;
class SwitchStmt;
class SwitchCaseStmt;
class SwitchDefaultStmt;
class LoopStmt;
class ForStmt;
class PrintStmt;

class Expression;
class RefExpr;
class LiteralExpr;

struct StmtVisitor {
    virtual void visit(const BreakStmt *) = 0;
    virtual void visit(const ContinueStmt *) = 0;
    virtual void visit(const ReturnStmt *) = 0;
    virtual void visit(const ScopeStmt *) = 0;
    virtual void visit(const IfStmt *) = 0;
    virtual void visit(const LoopStmt *) = 0;
    virtual void visit(const ExprStmt *) = 0;
    virtual void visit(const SwitchStmt *) = 0;
    virtual void visit(const SwitchCaseStmt *) = 0;
    virtual void visit(const SwitchDefaultStmt *) = 0;
    virtual void visit(const AssignStmt *) = 0;
    virtual void visit(const ForStmt *) = 0;
    virtual void visit(const CommentStmt *) = 0;
    virtual void visit(const PrintStmt *) = 0;
};

#define OC_MAKE_STATEMENT_COMMON    \
    friend class FunctionCorrector; \
    void accept(StmtVisitor &visitor) const override { visitor.visit(this); }

class OC_AST_API Statement : public ASTNode, public concepts::Noncopyable, public Hashable {
public:
    enum struct Tag : uint32_t {
        SCOPE,
        BREAK,
        CONTINUE,
        RETURN,
        IF,
        LOOP,
        EXPR,
        SWITCH,
        SWITCH_CASE,
        SWITCH_DEFAULT,
        ASSIGN,
        COMMENT,
        FOR,
        PRINT,
        WARNING
    };

private:
    const Tag tag_;

public:
    explicit Statement(Tag tag) noexcept : tag_{tag} {}
    [[nodiscard]] auto tag() const noexcept { return tag_; }
    virtual void accept(StmtVisitor &) const = 0;
    virtual ~Statement() noexcept = default;
};

class OC_AST_API ScopeStmt : public Statement {
private:
    ocarina::vector<Variable> local_vars_;
    ocarina::vector<const Statement *> statements_;
    bool is_func_body_{};

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit ScopeStmt(bool is_func_body = false) noexcept
        : is_func_body_(is_func_body), Statement(Tag::SCOPE) {}
    bool check_context(const Function *ctx) const noexcept override {
        return detail::check_context((statements_), ctx);
    }
    [[nodiscard]] ocarina::span<const Variable> local_vars() const noexcept { return local_vars_; }
    [[nodiscard]] bool is_func_body() const noexcept { return is_func_body_; }
    [[nodiscard]] ocarina::span<const Statement *const> statements() const noexcept { return statements_; }
    [[nodiscard]] bool empty() const noexcept { return statements_.empty(); }
    [[nodiscard]] auto size() const noexcept { return statements_.size(); }
    void add_stmt(const Statement *stmt) noexcept;
    void add_var(const Variable &variable) noexcept;
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API BreakStmt : public Statement {
private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override {
        return Hash64::default_seed;
    }

public:
    BreakStmt() noexcept : Statement{Tag::BREAK} {}
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API ContinueStmt : public Statement {
private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override {
        return Hash64::default_seed;
    }

public:
    ContinueStmt() noexcept : Statement(Tag::CONTINUE) {}
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API ReturnStmt : public Statement {
private:
    const Expression *expression_{nullptr};

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit ReturnStmt(const Expression *expr = nullptr) noexcept
        : Statement(Tag::RETURN), expression_(expr) {}
    OC_MAKE_CHECK_CONTEXT(Statement, expression_)
    [[nodiscard]] const Expression *expression() const noexcept { return expression_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API ExprStmt : public Statement {
private:
    const Expression *expression_{nullptr};

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit ExprStmt(const Expression *expr = nullptr) noexcept
        : Statement(Tag::EXPR), expression_(expr) {}
    OC_MAKE_CHECK_CONTEXT(Statement, expression_)
    const Expression *expression() const { return expression_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API AssignStmt : public Statement {
private:
    const Expression *lhs_{nullptr};
    const Expression *rhs_{nullptr};

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit AssignStmt(const Expression *lhs, const Expression *rhs)
        : Statement(Tag::ASSIGN), lhs_(lhs), rhs_(rhs) {}
    OC_MAKE_CHECK_CONTEXT(Statement, lhs_, rhs_)
    [[nodiscard]] auto lhs() const noexcept { return lhs_; }
    [[nodiscard]] auto rhs() const noexcept { return rhs_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API IfStmt : public Statement {
private:
    const Expression *condition_{nullptr};
    ScopeStmt true_branch_{};
    ScopeStmt false_branch_{};

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit IfStmt(const Expression *condition) : Statement(Tag::IF), condition_(condition) {}
    OC_MAKE_CHECK_CONTEXT(Statement, condition_, true_branch_, false_branch_)
    [[nodiscard]] const Expression *condition() const noexcept { return condition_; }
    [[nodiscard]] const ScopeStmt *true_branch() const noexcept { return &true_branch_; }
    [[nodiscard]] const ScopeStmt *false_branch() const noexcept { return &false_branch_; }
    [[nodiscard]] ScopeStmt *true_branch() noexcept { return &true_branch_; }
    [[nodiscard]] ScopeStmt *false_branch() noexcept { return &false_branch_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API CommentStmt : public Statement {
private:
    std::string string_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit CommentStmt(const std::string &str)
        : Statement(Tag::COMMENT), string_(str) {}
    [[nodiscard]] std::string string() const noexcept { return string_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API SwitchStmt : public Statement {
private:
    const Expression *expression_;
    ScopeStmt body_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit SwitchStmt(const Expression *expr)
        : Statement(Tag::SWITCH), expression_(expr) {}
    OC_MAKE_CHECK_CONTEXT(Statement, expression_, body_)
    [[nodiscard]] auto expression() const noexcept { return expression_; }
    [[nodiscard]] auto body() const noexcept { return &body_; }
    [[nodiscard]] auto body() noexcept { return &body_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API SwitchCaseStmt : public Statement {
private:
    const LiteralExpr *expr_;
    ScopeStmt body_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit SwitchCaseStmt(const Expression *expression);
    OC_MAKE_CHECK_CONTEXT(Statement, expr_, body_)
    [[nodiscard]] auto expression() const noexcept { return expr_; }
    [[nodiscard]] auto body() const noexcept { return &body_; }
    [[nodiscard]] auto body() noexcept { return &body_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API SwitchDefaultStmt : public Statement {
private:
    ScopeStmt body_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    SwitchDefaultStmt() : Statement(Tag::SWITCH_DEFAULT) {}
    OC_MAKE_CHECK_CONTEXT(Statement, body_)
    [[nodiscard]] auto body() const noexcept { return &body_; }
    [[nodiscard]] auto body() noexcept { return &body_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API ForStmt : public Statement {
private:
    const Expression *var_{};
    const Expression *condition_{};
    const Expression *step_{};
    ScopeStmt body_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    ForStmt(const Expression *var, const Expression *cond, const Expression *step)
        : Statement(Tag::FOR), var_(var), condition_(cond), step_(step) {}
    OC_MAKE_CHECK_CONTEXT(Statement, var_, condition_, step_, body_)
    [[nodiscard]] auto var() const noexcept { return var_; }
    [[nodiscard]] auto condition() const noexcept { return condition_; }
    [[nodiscard]] auto step() const noexcept { return step_; }
    [[nodiscard]] auto body() const noexcept { return &body_; }
    [[nodiscard]] auto body() noexcept { return &body_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API LoopStmt : public Statement {
private:
    ScopeStmt body_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    LoopStmt() : Statement(Tag::LOOP) {}
    OC_MAKE_CHECK_CONTEXT(Statement, body_)
    [[nodiscard]] auto body() const noexcept { return &body_; }
    [[nodiscard]] auto body() noexcept { return &body_; }
    OC_MAKE_STATEMENT_COMMON
};

class OC_AST_API PrintStmt : public Statement {
private:
    ocarina::string fmt_;
    ocarina::vector<const Expression *> args_;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept override;

public:
    explicit PrintStmt(string fmt, const vector<const Expression *> &args)
        : Statement(Tag::PRINT), fmt_(fmt), args_(args) {}
    OC_MAKE_CHECK_CONTEXT(Statement, args_)
    [[nodiscard]] ocarina::string fmt() const noexcept { return fmt_; }
    [[nodiscard]] span<const Expression *const> args() const noexcept { return args_; }
    OC_MAKE_STATEMENT_COMMON
};

}// namespace ocarina