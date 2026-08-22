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

[[nodiscard]] const horizon::ast::Statement *find_statement(
    const horizon::ast::ScopeStmt *scope,
    horizon::ast::Statement::Tag tag,
    unsigned occurrence = 0u) noexcept {
    for (const auto *statement : scope->statements()) {
        if (statement->tag() == tag) {
            if (occurrence == 0u) {
                return statement;
            }
            --occurrence;
        }
    }
    return nullptr;
}

[[nodiscard]] unsigned count_statements(const horizon::ast::ScopeStmt *scope,
                                        horizon::ast::Statement::Tag tag) noexcept {
    unsigned count = 0u;
    for (const auto *statement : scope->statements()) {
        if (statement->tag() == tag) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool appears_before(const horizon::ast::ScopeStmt *scope,
                                  const horizon::ast::Statement *first,
                                  const horizon::ast::Statement *second) noexcept {
    bool found_first = false;
    for (const auto *statement : scope->statements()) {
        if (statement == first) {
            found_first = true;
        }
        if (statement == second) {
            return found_first;
        }
    }
    return false;
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

void test_for_range_preserves_bounds_step_and_body_control_flow() {
    using horizon::ast::BinaryExpr;
    using horizon::ast::BinaryOp;
    using horizon::ast::ConditionalExpr;
    using horizon::ast::Expression;
    using horizon::ast::ForStmt;
    using horizon::ast::Statement;
    using horizon::dsl::Break;
    using horizon::dsl::Callable;
    using horizon::dsl::Continue;
    using horizon::dsl::Int;
    using horizon::dsl::for_range;

    const Callable<void(int &)> ranged_loop{[](Int &output) noexcept {
        for_range(2, 8, 2, [&](Int &index, Continue continue_, Break break_) noexcept {
            output = index;
            continue_();
            break_();
        });
    }};
    const auto *function = ranged_loop.function().get();
    const auto *body = function->body();
    const auto *for_node = find_statement(body, Statement::Tag::FOR);

    expect(body->check_context(function),
           "DSL for_range keeps every generated node in the callable context");
    expect(for_node != nullptr,
           "DSL for_range generates a ForStmt");
    if (for_node == nullptr) {
        return;
    }

    const auto *for_statement = static_cast<const ForStmt *>(for_node);
    expect(is_int_literal(resolve_expression(body, for_statement->var(), for_statement).expression, 2),
           "DSL for_range preserves its begin value");
    expect(is_int_literal(resolve_expression(body, for_statement->step(), for_statement).expression, 2),
           "DSL for_range preserves its step value");
    expect(for_statement->condition()->tag() == Expression::Tag::CONDITIONAL,
           "DSL for_range selects comparison direction with a conditional expression");
    if (for_statement->condition()->tag() != Expression::Tag::CONDITIONAL) {
        return;
    }

    const auto *condition = static_cast<const ConditionalExpr *>(for_statement->condition());
    expect(condition->pred()->tag() == Expression::Tag::BINARY &&
               condition->true_()->tag() == Expression::Tag::BINARY &&
               condition->false_()->tag() == Expression::Tag::BINARY,
           "DSL for_range condition contains three binary comparisons");
    if (condition->pred()->tag() != Expression::Tag::BINARY ||
        condition->true_()->tag() != Expression::Tag::BINARY ||
        condition->false_()->tag() != Expression::Tag::BINARY) {
        return;
    }
    const auto *negative_step = static_cast<const BinaryExpr *>(condition->pred());
    const auto *descending = static_cast<const BinaryExpr *>(condition->true_());
    const auto *ascending = static_cast<const BinaryExpr *>(condition->false_());
    expect(negative_step->op() == BinaryOp::LESS &&
               is_int_literal(resolve_expression(body, negative_step->rhs(), for_statement).expression, 0),
           "DSL for_range detects a negative step with step < 0");
    expect(descending->op() == BinaryOp::GREATER &&
               is_int_literal(resolve_expression(body, descending->rhs(), for_statement).expression, 8),
           "DSL descending range uses index > end");
    expect(ascending->op() == BinaryOp::LESS &&
               is_int_literal(resolve_expression(body, ascending->rhs(), for_statement).expression, 8),
           "DSL ascending range uses index < end");

    const auto *for_body = for_statement->body();
    expect(count_statements(for_body, Statement::Tag::ASSIGN) >= 1u,
           "DSL for body retains its assignment");
    expect(count_statements(for_body, Statement::Tag::CONTINUE) == 1u &&
               count_statements(for_body, Statement::Tag::BREAK) == 1u,
           "DSL for body retains one continue and one break statement");
    const auto *continue_statement = find_statement(for_body, Statement::Tag::CONTINUE);
    const auto *break_statement = find_statement(for_body, Statement::Tag::BREAK);
    expect(continue_statement != nullptr && break_statement != nullptr &&
               appears_before(for_body, continue_statement, break_statement),
           "DSL for body preserves continue-before-break construction order");
}

void test_loop_and_while_preserve_nested_control_flow() {
    using horizon::ast::Expression;
    using horizon::ast::IfStmt;
    using horizon::ast::LoopStmt;
    using horizon::ast::Statement;
    using horizon::ast::UnaryExpr;
    using horizon::ast::UnaryOp;
    using horizon::dsl::Bool;
    using horizon::dsl::Break;
    using horizon::dsl::Callable;
    using horizon::dsl::Continue;
    using horizon::dsl::comment;
    using horizon::dsl::loop;

    const Callable<void(bool)> loops{[](Bool condition) noexcept {
        loop([](Continue continue_, Break break_) noexcept {
            continue_();
            break_();
        });
        $while(condition) {
            comment("while body");
        };
    }};
    const auto *function = loops.function().get();
    const auto *body = function->body();
    const auto *plain_loop_node = find_statement(body, Statement::Tag::LOOP, 0u);
    const auto *while_loop_node = find_statement(body, Statement::Tag::LOOP, 1u);

    expect(body->check_context(function),
           "DSL loop builders keep every generated node in the callable context");
    expect(plain_loop_node != nullptr && while_loop_node != nullptr,
           "DSL loop and while_ each generate a LoopStmt");
    if (plain_loop_node == nullptr || while_loop_node == nullptr) {
        return;
    }

    const auto *plain_loop = static_cast<const LoopStmt *>(plain_loop_node);
    expect(count_statements(plain_loop->body(), Statement::Tag::CONTINUE) == 1u &&
               count_statements(plain_loop->body(), Statement::Tag::BREAK) == 1u,
           "DSL loop body preserves continue and break statements");

    const auto *while_loop = static_cast<const LoopStmt *>(while_loop_node);
    const auto *guard_node = find_statement(while_loop->body(), Statement::Tag::IF);
    expect(guard_node != nullptr,
           "DSL while_ lowers its exit condition to an IfStmt");
    expect(find_statement(while_loop->body(), Statement::Tag::COMMENT) != nullptr,
           "DSL while_ keeps its body after the exit guard");
    if (guard_node == nullptr) {
        return;
    }

    const auto *guard = static_cast<const IfStmt *>(guard_node);
    const auto guard_condition = resolve_expression(while_loop->body(), guard->condition(), guard);
    expect(guard_condition.expression != nullptr &&
               guard_condition.expression->tag() == Expression::Tag::UNARY,
           "DSL while_ guard resolves to a unary expression");
    if (guard_condition.expression != nullptr &&
        guard_condition.expression->tag() == Expression::Tag::UNARY) {
        const auto *negation = static_cast<const UnaryExpr *>(guard_condition.expression);
        expect(negation->op() == UnaryOp::NOT,
               "DSL while_ negates its continuation condition");
    }
    expect(count_statements(guard->true_branch(), Statement::Tag::BREAK) == 1u,
           "DSL while_ exit guard breaks when the negated condition is true");
}

void test_switch_preserves_case_default_and_break_scopes() {
    using horizon::ast::Statement;
    using horizon::ast::SwitchCaseStmt;
    using horizon::ast::SwitchDefaultStmt;
    using horizon::ast::SwitchStmt;
    using horizon::dsl::Break;
    using horizon::dsl::Callable;
    using horizon::dsl::Case;
    using horizon::dsl::Default;
    using horizon::dsl::Int;
    using horizon::dsl::comment;
    using horizon::dsl::switch_;

    const Callable<void(int)> select_branch{[](Int selector) noexcept {
        switch_(selector, [](Case case_, Default default_) noexcept {
            case_(1, [](Break break_) noexcept {
                comment("case one");
                break_();
            });
            case_(3, [](Break break_) noexcept {
                comment("case three");
                break_();
            });
            default_([](Break break_) noexcept {
                comment("default");
                break_();
            });
        });
    }};
    const auto *function = select_branch.function().get();
    const auto *body = function->body();
    const auto *switch_node = find_statement(body, Statement::Tag::SWITCH);

    expect(body->check_context(function),
           "DSL switch keeps every generated node in the callable context");
    expect(switch_node != nullptr,
           "DSL switch_ generates a SwitchStmt");
    if (switch_node == nullptr) {
        return;
    }

    const auto *switch_statement = static_cast<const SwitchStmt *>(switch_node);
    const auto *switch_body = switch_statement->body();
    expect(count_statements(switch_body, Statement::Tag::SWITCH_CASE) == 2u &&
               count_statements(switch_body, Statement::Tag::SWITCH_DEFAULT) == 1u,
           "DSL switch preserves two cases and one default branch");
    const auto *first_case_node = find_statement(switch_body, Statement::Tag::SWITCH_CASE, 0u);
    const auto *second_case_node = find_statement(switch_body, Statement::Tag::SWITCH_CASE, 1u);
    const auto *default_node = find_statement(switch_body, Statement::Tag::SWITCH_DEFAULT);
    if (first_case_node == nullptr || second_case_node == nullptr || default_node == nullptr) {
        return;
    }

    const auto *first_case = static_cast<const SwitchCaseStmt *>(first_case_node);
    const auto *second_case = static_cast<const SwitchCaseStmt *>(second_case_node);
    const auto *default_statement = static_cast<const SwitchDefaultStmt *>(default_node);
    expect(is_int_literal(first_case->expression(), 1) &&
               is_int_literal(second_case->expression(), 3),
           "DSL switch cases preserve their literal values and order");
    expect(count_statements(first_case->body(), Statement::Tag::BREAK) == 1u &&
               count_statements(second_case->body(), Statement::Tag::BREAK) == 1u &&
               count_statements(default_statement->body(), Statement::Tag::BREAK) == 1u,
           "DSL switch case and default scopes retain their break statements");
    expect(count_statements(first_case->body(), Statement::Tag::COMMENT) == 1u &&
               count_statements(second_case->body(), Statement::Tag::COMMENT) == 1u &&
               count_statements(default_statement->body(), Statement::Tag::COMMENT) == 1u,
           "DSL switch case and default scopes retain their bodies");
}

void test_elif_generates_a_nested_if_in_the_false_branch() {
    using horizon::ast::IfStmt;
    using horizon::ast::Statement;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;
    using horizon::dsl::comment;
    using horizon::dsl::if_;

    const Callable<void(int)> classify{[](Int value) noexcept {
        if_(value == 0, [] {
            comment("zero");
        }).elif_(value == 1, [] {
            comment("one");
        }).else_([] {
            comment("other");
        });
    }};
    const auto *body = classify.function()->body();
    const auto *outer_node = find_statement(body, Statement::Tag::IF);

    expect(outer_node != nullptr,
           "DSL if_ generates the outer IfStmt");
    if (outer_node == nullptr) {
        return;
    }
    const auto *outer = static_cast<const IfStmt *>(outer_node);
    const auto *nested_node = find_statement(outer->false_branch(), Statement::Tag::IF);
    expect(nested_node != nullptr,
           "DSL elif_ generates a nested IfStmt in the false branch");
    if (nested_node != nullptr) {
        const auto *nested = static_cast<const IfStmt *>(nested_node);
        expect(count_statements(nested->true_branch(), Statement::Tag::COMMENT) == 1u &&
                   count_statements(nested->false_branch(), Statement::Tag::COMMENT) == 1u,
               "DSL elif_ keeps its true branch and final else branch isolated");
    }
}

void test_scope_comment_print_expr_and_void_return_statements() {
    using horizon::ast::CallExpr;
    using horizon::ast::CallOp;
    using horizon::ast::CommentStmt;
    using horizon::ast::ExprStmt;
    using horizon::ast::PrintStmt;
    using horizon::ast::ReturnStmt;
    using horizon::ast::ScopeStmt;
    using horizon::ast::Statement;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;
    using horizon::dsl::comment;
    using horizon::dsl::detail::ScopeStmtBuilder;
    using horizon::dsl::detail::allow_capture_tag;
    using horizon::dsl::print;
    using horizon::dsl::return_;

    const Callable<void(int)> sink{[](Int) noexcept {}};
    const Callable<void(int)> statements{[&sink](Int value) noexcept {
        ScopeStmtBuilder("test scope") + [] {
            comment("inside scope");
        };
        print("value={}", value);
        sink(std::move(value));
        return_();
    }, allow_capture_tag};
    const auto *function = statements.function().get();
    const auto *body = function->body();

    expect(body->check_context(function),
           "miscellaneous DSL statements keep the callable context");
    const auto *scope_node = find_statement(body, Statement::Tag::SCOPE);
    const auto *print_node = find_statement(body, Statement::Tag::PRINT);
    const auto *expr_node = find_statement(body, Statement::Tag::EXPR);
    const auto *return_node = find_statement(body, Statement::Tag::RETURN);
    expect(scope_node != nullptr && print_node != nullptr &&
               expr_node != nullptr && return_node != nullptr,
           "DSL generates scope, print, expression, and return statements");
    if (scope_node == nullptr || print_node == nullptr ||
        expr_node == nullptr || return_node == nullptr) {
        return;
    }

    const auto *scope = static_cast<const ScopeStmt *>(scope_node);
    const auto *nested_comment_node = find_statement(scope, Statement::Tag::COMMENT);
    expect(nested_comment_node != nullptr &&
               static_cast<const CommentStmt *>(nested_comment_node)->string() == "inside scope",
           "DSL scope owns its explicit comment statement");

    const auto *print_statement = static_cast<const PrintStmt *>(print_node);
    expect(print_statement->fmt() == "value={}" && print_statement->args().size() == 1u,
           "DSL print preserves its format and argument count");

    const auto *expression_statement = static_cast<const ExprStmt *>(expr_node);
    expect(expression_statement->expression()->tag() == horizon::ast::Expression::Tag::CALL,
           "void Callable invocation is emitted as an ExprStmt");
    if (expression_statement->expression()->tag() == horizon::ast::Expression::Tag::CALL) {
        const auto *call = static_cast<const CallExpr *>(expression_statement->expression());
        expect(call->call_op() == CallOp::CUSTOM && call->function() == sink.function().get(),
               "DSL ExprStmt retains its custom callee");
    }

    const auto *return_statement = static_cast<const ReturnStmt *>(return_node);
    expect(return_statement->expression() == nullptr,
           "DSL void return generates a ReturnStmt without an expression");
}

}// namespace

int main() {
    test_for_range_preserves_bounds_step_and_body_control_flow();
    test_loop_and_while_preserve_nested_control_flow();
    test_switch_preserves_case_default_and_break_scopes();
    test_elif_generates_a_nested_if_in_the_false_branch();
    test_scope_comment_print_expr_and_void_return_statements();
    return failures == 0 ? 0 : 1;
}
