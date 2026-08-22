#include "dsl/dsl.h"

#include "ast/expression.h"
#include "ast/statement.h"

#include <iostream>
#include <utility>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct ResolvedExpression {
    const horizon::ast::Expression *expression;
    const horizon::ast::Statement *before;
};

[[nodiscard]] ResolvedExpression resolve_expression(
    const horizon::ast::ScopeStmt *scope,
    const horizon::ast::Expression *expression,
    const horizon::ast::Statement *before = nullptr,
    unsigned depth = 0u) noexcept {
    if (expression == nullptr || expression->tag() != horizon::ast::Expression::Tag::REF ||
        depth == 32u) {
        return {expression, before};
    }
    const horizon::ast::AssignStmt *reaching_definition = nullptr;
    for (const auto *statement : scope->statements()) {
        if (statement == before) {
            break;
        }
        if (statement->tag() != horizon::ast::Statement::Tag::ASSIGN) {
            continue;
        }
        const auto *assignment = static_cast<const horizon::ast::AssignStmt *>(statement);
        if (assignment->lhs() == expression) {
            reaching_definition = assignment;
        }
    }
    if (reaching_definition == nullptr) {
        return {expression, before};
    }
    return resolve_expression(scope, reaching_definition->rhs(), reaching_definition, depth + 1u);
}

[[nodiscard]] const horizon::ast::AssignStmt *find_assignment_to(
    const horizon::ast::ScopeStmt *scope,
    const horizon::ast::Expression *target) noexcept {
    for (const auto *statement : scope->statements()) {
        if (statement->tag() == horizon::ast::Statement::Tag::ASSIGN) {
            const auto *assignment = static_cast<const horizon::ast::AssignStmt *>(statement);
            if (assignment->lhs() == target) {
                return assignment;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] const horizon::ast::ReturnStmt *find_return(
    const horizon::ast::ScopeStmt *scope) noexcept {
    for (const auto *statement : scope->statements()) {
        if (statement->tag() == horizon::ast::Statement::Tag::RETURN) {
            return static_cast<const horizon::ast::ReturnStmt *>(statement);
        }
    }
    return nullptr;
}

[[nodiscard]] const horizon::ast::IfStmt *find_if(
    const horizon::ast::ScopeStmt *scope) noexcept {
    for (const auto *statement : scope->statements()) {
        if (statement->tag() == horizon::ast::Statement::Tag::IF) {
            return static_cast<const horizon::ast::IfStmt *>(statement);
        }
    }
    return nullptr;
}

[[nodiscard]] bool is_int_literal(const horizon::ast::Expression *expression,
                                  int expected) noexcept {
    if (expression == nullptr || expression->tag() != horizon::ast::Expression::Tag::LITERAL ||
        expression->type() != horizon::core::Type::of<int>()) {
        return false;
    }
    const auto *literal = static_cast<const horizon::ast::LiteralExpr *>(expression);
    const auto value = literal->value();
    return std::holds_alternative<int>(value) && std::get<int>(value) == expected;
}

void test_arithmetic_operators_generate_the_expected_expression_tree() {
    using horizon::ast::BinaryExpr;
    using horizon::ast::BinaryOp;
    using horizon::ast::Expression;
    using horizon::core::Type;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;

    const Callable<int(int, int)> arithmetic{[](Int lhs, Int rhs) noexcept {
        return lhs + rhs * 2;
    }};
    const auto *function = arithmetic.function().get();
    const auto *body = function->body();

    expect(body->check_context(function),
           "DSL arithmetic keeps every generated node in the callable context");
    expect(function->arguments().size() == 2u &&
               function->arguments()[0].type() == Type::of<int>() &&
               function->arguments()[1].type() == Type::of<int>(),
           "DSL callable preserves both integer parameters");

    const auto *return_statement = find_return(body);
    expect(return_statement != nullptr,
           "DSL arithmetic generates a return statement");
    if (return_statement == nullptr) {
        return;
    }

    const auto root = resolve_expression(body, return_statement->expression(), return_statement);
    expect(root.expression != nullptr && root.expression->tag() == Expression::Tag::BINARY,
           "DSL arithmetic resolves to a binary expression");
    if (root.expression == nullptr || root.expression->tag() != Expression::Tag::BINARY) {
        return;
    }

    const auto *addition = static_cast<const BinaryExpr *>(root.expression);
    expect(addition->op() == BinaryOp::ADD && addition->type() == Type::of<int>(),
           "DSL arithmetic keeps addition at the semantic expression root");
    const auto lhs = resolve_expression(body, addition->lhs(), root.before);
    const auto rhs = resolve_expression(body, addition->rhs(), root.before);
    expect(lhs.expression != nullptr && lhs.expression->tag() == Expression::Tag::REF,
           "DSL addition keeps the left parameter as its lhs");
    expect(rhs.expression != nullptr && rhs.expression->tag() == Expression::Tag::BINARY,
           "DSL arithmetic evaluates multiplication before addition");
    if (rhs.expression == nullptr || rhs.expression->tag() != Expression::Tag::BINARY) {
        return;
    }

    const auto *multiplication = static_cast<const BinaryExpr *>(rhs.expression);
    expect(multiplication->op() == BinaryOp::MUL && multiplication->type() == Type::of<int>(),
           "DSL multiplication maps to BinaryOp::MUL with an integer result");
    const auto multiplication_lhs = resolve_expression(body, multiplication->lhs(), rhs.before);
    const auto multiplication_rhs = resolve_expression(body, multiplication->rhs(), rhs.before);
    expect(multiplication_lhs.expression != nullptr &&
               multiplication_lhs.expression->tag() == Expression::Tag::REF &&
               is_int_literal(multiplication_rhs.expression, 2),
           "DSL multiplication preserves the right parameter and literal two");
}

void test_if_else_generates_condition_and_branch_assignments() {
    using horizon::ast::BinaryExpr;
    using horizon::ast::BinaryOp;
    using horizon::ast::Expression;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;
    using horizon::dsl::if_;

    const Callable<void(int &)> choose_assignment{[](Int &value) noexcept {
        if_(value > 0, [&] {
            value += 1;
        }).else_([&] {
            value = 0;
        });
    }};
    const auto *function = choose_assignment.function().get();
    const auto *body = function->body();

    expect(body->check_context(function),
           "DSL if/else keeps every generated node in the callable context");
    const auto *if_statement = find_if(body);
    expect(if_statement != nullptr,
           "DSL if_/else_ generates an if statement");
    if (if_statement == nullptr) {
        return;
    }

    const auto condition_expression = resolve_expression(body, if_statement->condition(), if_statement);
    expect(condition_expression.expression != nullptr &&
               condition_expression.expression->tag() == Expression::Tag::BINARY,
           "DSL comparison creates a binary if condition");
    if (condition_expression.expression == nullptr ||
        condition_expression.expression->tag() != Expression::Tag::BINARY) {
        return;
    }

    const auto *condition = static_cast<const BinaryExpr *>(condition_expression.expression);
    const auto target = resolve_expression(body, condition->lhs(), condition_expression.before);
    const auto condition_rhs = resolve_expression(body, condition->rhs(), condition_expression.before);
    expect(condition->op() == BinaryOp::GREATER &&
               is_int_literal(condition_rhs.expression, 0),
           "DSL greater-than condition preserves its operation and zero literal");
    expect(target.expression != nullptr && target.expression->tag() == Expression::Tag::REF,
           "DSL if condition reads the reference parameter");
    if (target.expression == nullptr) {
        return;
    }

    const auto *true_assignment = find_assignment_to(if_statement->true_branch(), target.expression);
    const auto *false_assignment = find_assignment_to(if_statement->false_branch(), target.expression);
    expect(true_assignment != nullptr && false_assignment != nullptr,
           "DSL true and false branches assign the same reference");
    if (true_assignment == nullptr || false_assignment == nullptr) {
        return;
    }

    const auto increment_expression =
        resolve_expression(if_statement->true_branch(), true_assignment->rhs(), true_assignment);
    expect(increment_expression.expression != nullptr &&
               increment_expression.expression->tag() == Expression::Tag::BINARY,
           "DSL += generates a binary expression in the true branch");
    if (increment_expression.expression != nullptr &&
        increment_expression.expression->tag() == Expression::Tag::BINARY) {
        const auto *increment = static_cast<const BinaryExpr *>(increment_expression.expression);
        const auto increment_lhs = resolve_expression(
            if_statement->true_branch(), increment->lhs(), increment_expression.before);
        const auto increment_rhs = resolve_expression(
            if_statement->true_branch(), increment->rhs(), increment_expression.before);
        expect(increment->op() == BinaryOp::ADD &&
                   increment_lhs.expression == target.expression &&
                   is_int_literal(increment_rhs.expression, 1),
               "DSL += reads the target and adds literal one");
    }
    const auto false_value = resolve_expression(
        if_statement->false_branch(), false_assignment->rhs(), false_assignment);
    expect(is_int_literal(false_value.expression, 0),
           "DSL else branch assigns literal zero");
}

void test_callable_invocation_generates_a_custom_call_expression() {
    using horizon::ast::BinaryExpr;
    using horizon::ast::BinaryOp;
    using horizon::ast::CallExpr;
    using horizon::ast::CallOp;
    using horizon::ast::Expression;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;
    using horizon::dsl::detail::allow_capture_tag;

    const Callable<int(int)> square{[](Int value) noexcept {
        return value * value;
    }};
    const Callable<int(int)> caller{[&square](Int value) noexcept {
        return square(std::move(value)) + 1;
    }, allow_capture_tag};

    const auto *function = caller.function().get();
    const auto *body = function->body();
    expect(body->check_context(function),
           "DSL custom call keeps generated caller nodes in the caller context");

    const auto *return_statement = find_return(body);
    expect(return_statement != nullptr,
           "DSL caller generates a return statement");
    if (return_statement == nullptr) {
        return;
    }

    const auto root = resolve_expression(body, return_statement->expression(), return_statement);
    expect(root.expression != nullptr && root.expression->tag() == Expression::Tag::BINARY,
           "DSL caller returns a binary expression");
    if (root.expression == nullptr || root.expression->tag() != Expression::Tag::BINARY) {
        return;
    }

    const auto *addition = static_cast<const BinaryExpr *>(root.expression);
    const auto call_expression = resolve_expression(body, addition->lhs(), root.before);
    const auto addition_rhs = resolve_expression(body, addition->rhs(), root.before);
    expect(addition->op() == BinaryOp::ADD &&
               is_int_literal(addition_rhs.expression, 1),
           "DSL caller adds literal one to the call result");
    expect(call_expression.expression != nullptr &&
               call_expression.expression->tag() == Expression::Tag::CALL,
           "DSL Callable invocation generates a call expression");
    if (call_expression.expression == nullptr ||
        call_expression.expression->tag() != Expression::Tag::CALL) {
        return;
    }

    const auto *call = static_cast<const CallExpr *>(call_expression.expression);
    expect(call->call_op() == CallOp::CUSTOM,
           "DSL Callable invocation maps to a custom call operation");
    expect(call->function() == square.function().get(),
           "DSL custom call retains the referenced callee function");
    const auto call_argument = call->arguments().size() == 1u
                                   ? resolve_expression(body, call->argument(0), call_expression.before)
                                   : ResolvedExpression{nullptr, nullptr};
    expect(call_argument.expression != nullptr &&
               call_argument.expression->tag() == Expression::Tag::REF,
           "DSL custom call preserves its single caller argument");
}

}// namespace

int main() {
    test_arithmetic_operators_generate_the_expected_expression_tree();
    test_if_else_generates_condition_and_branch_assignments();
    test_callable_invocation_generates_a_custom_call_expression();
    return failures == 0 ? 0 : 1;
}
