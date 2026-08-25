#include "dsl/dsl.h"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool has_terminating_bounds_check(const horizon::dsl::Kernel<void()> &kernel) {
    using horizon::ast::IfStmt;
    using horizon::ast::Statement;

    const auto &function = kernel.function();
    if (function == nullptr || !function->body()->check_context(function.get())) {
        return false;
    }

    const IfStmt *bounds_check = nullptr;
    for (const Statement *statement : function->body()->statements()) {
        if (statement->tag() == Statement::Tag::If) {
            bounds_check = static_cast<const IfStmt *>(statement);
            break;
        }
    }
    if (bounds_check == nullptr) {
        return false;
    }

    for (const Statement *statement : bounds_check->true_branch()->statements()) {
        if (statement->tag() == Statement::Tag::Return) {
            return true;
        }
    }
    return false;
}

void test_custom_callable_builds_call_expression() {
    using horizon::ast::AssignStmt;
    using horizon::ast::CallExpr;
    using horizon::ast::CallOp;
    using horizon::ast::Expression;
    using horizon::ast::Statement;
    using horizon::core::Type;
    using horizon::dsl::Callable;
    using horizon::dsl::Float;
    using horizon::dsl::Int;
    using horizon::dsl::Kernel;

    Callable<float(float, int)> select_first{[](Float value, Int index) {
        (void)index;
        return value;
    }};
    Kernel kernel{[&](Float value, Int index) {
        Float result = select_first(value, index);
        (void)result;
    }};

    const auto &function = kernel.function();
    expect(function != nullptr && function->body()->check_context(function.get()),
           "custom callable invocation stays in the kernel context");
    if (function == nullptr) {
        return;
    }

    const CallExpr *call = nullptr;
    for (const Statement *statement : function->body()->statements()) {
        if (statement->tag() != Statement::Tag::Assign) {
            continue;
        }
        const Expression *rhs = static_cast<const AssignStmt *>(statement)->rhs();
        if (rhs->tag() == Expression::Tag::Call) {
            call = static_cast<const CallExpr *>(rhs);
            break;
        }
    }

    expect(call != nullptr, "custom callable invocation emits a CallExpr");
    if (call == nullptr) {
        return;
    }
    expect(call->call_op() == CallOp::Custom,
           "custom callable invocation uses CallOp::Custom");
    expect(call->function() == select_first.function().get(),
           "custom CallExpr retains its target function");
    expect(call->type() == Type::of<float>(),
           "custom CallExpr retains the callable return type");
    expect(call->arguments().size() == 2u,
           "custom CallExpr retains both invocation arguments");
    if (call->arguments().size() == 2u) {
        expect(call->argument(0)->type() == Type::of<float>() &&
                   call->argument(1)->type() == Type::of<int>(),
               "custom CallExpr preserves invocation argument order");
    }

    std::size_t used_function_count = 0u;
    const horizon::ast::Function *used_function = nullptr;
    function->for_each_custom_func([&](const horizon::ast::Function *candidate) {
        ++used_function_count;
        used_function = candidate;
    });
    expect(used_function_count == 1u && used_function == select_first.function().get(),
           "kernel registers the invoked callable as a used custom function");
}

}// namespace

int main() {
    using horizon::dsl::Kernel;
    using horizon::dsl::Uint;

    Kernel static_size_kernel{[] {
        Uint index = 2u;
        const auto corrected = horizon::dsl::detail::correct_index(
            index, 1u, "static size boundary test", "test traceback");
        (void)corrected;
    }};

    Kernel dynamic_size_kernel{[] {
        Uint index = 2u;
        Uint size = 1u;
        const auto corrected = horizon::dsl::detail::correct_index(
            index, size, "dynamic size boundary test", "test traceback");
        (void)corrected;
    }};

    if (!has_terminating_bounds_check(static_size_kernel) ||
        !has_terminating_bounds_check(dynamic_size_kernel)) {
        std::cerr << "FAIL: correct_index did not emit a valid terminating bounds check\n";
        ++failures;
    }
    test_custom_callable_builds_call_expression();
    return failures == 0 ? 0 : 1;
}
