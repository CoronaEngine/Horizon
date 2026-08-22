#include "dsl/dsl.h"

#include "ast/expression.h"
#include "ast/function.h"
#include "ast/statement.h"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] horizon::ast::vector<const horizon::ast::CallExpr *> find_calls_to(
    const horizon::ast::ScopeStmt *scope,
    const horizon::ast::Function *callee) noexcept {
    horizon::ast::vector<const horizon::ast::CallExpr *> calls;
    for (const auto *statement : scope->statements()) {
        if (statement->tag() != horizon::ast::Statement::Tag::EXPR) {
            continue;
        }
        const auto *expression_statement =
            static_cast<const horizon::ast::ExprStmt *>(statement);
        const auto *expression = expression_statement->expression();
        if (expression->tag() != horizon::ast::Expression::Tag::CALL) {
            continue;
        }
        const auto *call = static_cast<const horizon::ast::CallExpr *>(expression);
        if (call->function() == callee) {
            calls.push_back(call);
        }
    }
    return calls;
}

void test_repeated_outer_expression_maps_to_one_capture_argument() {
    using horizon::ast::Expression;
    using horizon::ast::Function;
    using horizon::ast::Variable;
    using horizon::core::Type;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;
    using horizon::dsl::detail::allow_capture_tag;

    const Expression *outer_expression = nullptr;
    const Function *capturing_function = nullptr;

    const auto kernel = Function::define_kernel([&] {
        Int outer{horizon::dsl::detail::ArgumentCreation{}};
        outer_expression = outer.expression();
        Callable<void()> capture_twice{[&]() noexcept {
            Int sum = outer + outer;
        }, allow_capture_tag};
        capture_twice.function()->set_allow_dsl_capture(true);
        capturing_function = capture_twice.function().get();
        capture_twice();
    });

    const auto *kernel_function = kernel.get();
    const auto calls = find_calls_to(kernel_function->body(), capturing_function);
    expect(kernel_function->body()->check_context(kernel_function),
           "FunctionCorrector keeps the kernel AST context-consistent");
    expect(capturing_function != nullptr &&
               capturing_function->body()->check_context(capturing_function),
           "FunctionCorrector rewrites captured references into the callee context");
    expect(capturing_function != nullptr &&
               capturing_function->appended_arguments().size() == 1u,
           "repeated use of one outer expression creates one implicit formal argument");
    if (capturing_function != nullptr &&
        capturing_function->appended_arguments().size() == 1u) {
        const auto &formal = capturing_function->appended_arguments()[0];
        expect(formal.type() == Type::of<int>() &&
                   formal.tag() == Variable::Tag::REFERENCE,
               "captured scalar is represented by an integer reference argument");
    }
    expect(calls.size() == 1u && calls[0]->arguments().size() == 1u,
           "one implicit actual argument is appended to the capturing call");
    if (calls.size() == 1u && calls[0]->arguments().size() == 1u) {
        expect(calls[0]->argument(0) == outer_expression,
               "the implicit call argument is the original outer expression");
    }
}

void test_multiple_captures_preserve_discovery_order_and_type() {
    using horizon::ast::Expression;
    using horizon::ast::Function;
    using horizon::core::Type;
    using horizon::dsl::Bool;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;
    using horizon::dsl::detail::allow_capture_tag;

    const Expression *first_expression = nullptr;
    const Expression *second_expression = nullptr;
    const Function *capturing_function = nullptr;

    const auto kernel = Function::define_kernel([&] {
        Int first{horizon::dsl::detail::ArgumentCreation{}};
        Bool second{horizon::dsl::detail::ArgumentCreation{}};
        first_expression = first.expression();
        second_expression = second.expression();
        Callable<void()> capture_both{[&]() noexcept {
            Int first_value = first + 1;
            Bool second_value = !second;
        }, allow_capture_tag};
        capture_both.function()->set_allow_dsl_capture(true);
        capturing_function = capture_both.function().get();
        capture_both();
    });

    const auto *kernel_function = kernel.get();
    const auto calls = find_calls_to(kernel_function->body(), capturing_function);
    expect(capturing_function != nullptr &&
               capturing_function->appended_arguments().size() == 2u,
           "two distinct outer expressions create two implicit formal arguments");
    if (capturing_function != nullptr &&
        capturing_function->appended_arguments().size() == 2u) {
        expect(capturing_function->appended_arguments()[0].type() == Type::of<int>() &&
                   capturing_function->appended_arguments()[1].type() == Type::of<bool>(),
               "implicit formal arguments preserve capture discovery order and types");
    }
    expect(calls.size() == 1u && calls[0]->arguments().size() == 2u,
           "capturing call receives both implicit actual arguments");
    if (calls.size() == 1u && calls[0]->arguments().size() == 2u) {
        expect(calls[0]->argument(0) == first_expression &&
                   calls[0]->argument(1) == second_expression,
               "implicit actual arguments preserve capture discovery order");
    }
}

void test_multiple_callsites_replay_capture_arguments() {
    using horizon::ast::Expression;
    using horizon::ast::Function;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;
    using horizon::dsl::detail::allow_capture_tag;

    const Expression *outer_expression = nullptr;
    const Function *capturing_function = nullptr;

    const auto kernel = Function::define_kernel([&] {
        Int outer{horizon::dsl::detail::ArgumentCreation{}};
        outer_expression = outer.expression();
        Callable<void()> capture_once{[&]() noexcept {
            Int value = outer + 1;
        }, allow_capture_tag};
        capture_once.function()->set_allow_dsl_capture(true);
        capturing_function = capture_once.function().get();
        capture_once();
        capture_once();
    });

    const auto *kernel_function = kernel.get();
    const auto calls = find_calls_to(kernel_function->body(), capturing_function);
    expect(capturing_function != nullptr &&
               capturing_function->appended_arguments().size() == 1u,
           "multiple callsites share one implicit formal capture argument");
    expect(calls.size() == 2u,
           "both callsites remain present after FunctionCorrector traversal");
    if (calls.size() == 2u) {
        expect(calls[0]->arguments().size() == 1u &&
                   calls[1]->arguments().size() == 1u,
               "capture replay completes the actual arguments at every callsite");
        if (calls[0]->arguments().size() == 1u &&
            calls[1]->arguments().size() == 1u) {
            expect(calls[0]->argument(0) == outer_expression &&
                       calls[1]->argument(0) == outer_expression,
                   "every callsite receives the original outer expression");
        }
    }
}

void test_nested_calls_propagate_capture_through_each_function() {
    using horizon::ast::Expression;
    using horizon::ast::Function;
    using horizon::dsl::Callable;
    using horizon::dsl::Int;
    using horizon::dsl::detail::allow_capture_tag;

    const Expression *outer_expression = nullptr;
    const Function *middle_function = nullptr;
    const Function *inner_function = nullptr;

    const auto kernel = Function::define_kernel([&] {
        Int outer{horizon::dsl::detail::ArgumentCreation{}};
        outer_expression = outer.expression();
        Callable<void()> inner{[&]() noexcept {
            Int value = outer + 1;
        }, allow_capture_tag};
        inner.function()->set_allow_dsl_capture(true);
        inner_function = inner.function().get();

        Callable<void()> middle{[&]() noexcept {
            inner();
        }, allow_capture_tag};
        middle.function()->set_allow_dsl_capture(true);
        middle_function = middle.function().get();
        middle();
    });

    const auto *kernel_function = kernel.get();
    const auto kernel_calls = find_calls_to(kernel_function->body(), middle_function);
    const auto middle_calls = middle_function == nullptr
                                  ? horizon::ast::vector<const horizon::ast::CallExpr *>{}
                                  : find_calls_to(middle_function->body(), inner_function);
    expect(middle_function != nullptr && inner_function != nullptr &&
               middle_function->appended_arguments().size() == 1u &&
               inner_function->appended_arguments().size() == 1u,
           "nested capture creates one implicit formal argument at each function layer");
    expect(kernel_calls.size() == 1u &&
               kernel_calls[0]->arguments().size() == 1u &&
               kernel_calls[0]->argument(0) == outer_expression,
           "kernel passes the original outer expression into the middle function");
    expect(middle_calls.size() == 1u &&
               middle_calls[0]->arguments().size() == 1u,
           "middle function forwards one capture argument into the inner function");
    if (middle_calls.size() == 1u && middle_calls[0]->arguments().size() == 1u) {
        const auto *forwarded = middle_calls[0]->argument(0);
        expect(forwarded->context() == middle_function &&
                   forwarded->tag() == Expression::Tag::REF,
               "nested capture is forwarded through the middle function's formal reference");
        if (middle_function != nullptr &&
            middle_function->appended_arguments().size() == 1u &&
            forwarded->tag() == Expression::Tag::REF) {
            const auto *forwarded_reference =
                static_cast<const horizon::ast::RefExpr *>(forwarded);
            expect(forwarded_reference->variable().uid() ==
                       middle_function->appended_arguments()[0].uid(),
                   "middle function forwards its captured formal rather than an unrelated reference");
        }
    }
    expect(kernel_function->body()->check_context(kernel_function) &&
               middle_function->body()->check_context(middle_function) &&
               inner_function->body()->check_context(inner_function),
           "nested capture correction leaves every function context-consistent");
}

}// namespace

int main() {
    test_repeated_outer_expression_maps_to_one_capture_argument();
    test_multiple_captures_preserve_discovery_order_and_type();
    test_multiple_callsites_replay_capture_arguments();
    test_nested_calls_propagate_capture_through_each_function();
    return failures == 0 ? 0 : 1;
}
