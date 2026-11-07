//
// Created by GitHub Copilot on 2026/04/05.
//

#include "dsl/dsl.h"
#include "dsl/api/func.h"
#include "ast/function.h"
#include "ast/expression.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace ocarina;

void test_function_corrector_visitors();
void test_function_corrector_shared_calls();
void test_function_corrector_capture_after_resource();
void test_function_corrector_mixed_output_calls();
void test_function_corrector_sibling_outputs();
void test_function_corrector_parameters();
void probe_function_corrector_shared_capture();
void probe_function_corrector_forbidden_capture();

namespace {

void fail(const std::string &message) {
    std::cerr << "[test_function_corrector] " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const std::string &message) {
    if (!condition) {
        fail(message);
    }
}

template<typename T>
void require_eq(const T &actual, const T &expected, const std::string &message) {
    if (!(actual == expected)) {
        fail(message + " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual));
    }
}

const CallExpr *require_call_expr(const shared_ptr<Function> &function, const std::string &name) {
    require(function != nullptr, name + " function was not captured");
    const CallExpr *call_expr = function->current_call_expr();
    require(call_expr != nullptr, name + " call expression is null");
    return call_expr;
}

std::vector<const Function *> collect_custom_functions(const shared_ptr<Function> &function) {
    require(function != nullptr, "owner function is null");
    std::vector<const Function *> result;
    function->for_each_custom_func([&](const Function *custom_function) {
        result.push_back(custom_function);
    });
    return result;
}

const Function *require_single_custom_function(const shared_ptr<Function> &function, const std::string &name) {
    std::vector<const Function *> custom_functions = collect_custom_functions(function);
    require_eq(custom_functions.size(), static_cast<size_t>(1), name + " custom function count mismatch");
    return custom_functions.front();
}

void test_single_capture_deduplicates_repeated_use() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> outer = 7;
        Lambda capture = [&](Var<int> value) {
            return value + outer + outer;
        };
        [[maybe_unused]] Var<int> result = capture(3);
    };
    kernel_function = kernel.function();

    const Function *callable_function = require_single_custom_function(kernel_function, "single capture");
    require_eq(callable_function->arguments().size(), static_cast<size_t>(0), "single capture explicit arg count mismatch");
    require_eq(callable_function->appended_arguments().size(), static_cast<size_t>(2), "single capture appended arg count mismatch");
    require_eq(callable_function->all_arguments().size(), static_cast<size_t>(2), "single capture all arg count mismatch");

    const CallExpr *call_expr = callable_function->current_call_expr();
    require(call_expr != nullptr, "single capture call expression is null");
    require_eq(call_expr->arguments().size(), static_cast<size_t>(2), "single capture callsite arg count mismatch");
}

void test_multiple_outer_variables_are_captured() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> outer_a = 3;
        Var<int> outer_b = 5;
        Lambda capture = [&](Var<int> value) {
            return value + outer_a + outer_b;
        };
        [[maybe_unused]] Var<int> result = capture(11);
    };
    kernel_function = kernel.function();

    const Function *callable_function = require_single_custom_function(kernel_function, "multi capture");
    require_eq(callable_function->arguments().size(), static_cast<size_t>(0), "multi capture explicit arg count mismatch");
    require_eq(callable_function->appended_arguments().size(), static_cast<size_t>(3), "multi capture appended arg count mismatch");
    require_eq(callable_function->all_arguments().size(), static_cast<size_t>(3), "multi capture all arg count mismatch");

    const CallExpr *call_expr = callable_function->current_call_expr();
    require(call_expr != nullptr, "multi capture call expression is null");
    require_eq(call_expr->arguments().size(), static_cast<size_t>(3), "multi capture callsite arg count mismatch");
}

void test_member_expression_capture() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int2> pair = make_int2(4, 9);
        Lambda capture = [&](Var<int> value) {
            return value + pair.x;
        };
        [[maybe_unused]] Var<int> result = capture(2);
    };
    kernel_function = kernel.function();

    const Function *callable_function = require_single_custom_function(kernel_function, "member capture");
    require_eq(callable_function->arguments().size(), static_cast<size_t>(0), "member capture explicit arg count mismatch");
    require_eq(callable_function->appended_arguments().size(), static_cast<size_t>(2), "member capture appended arg count mismatch");
    require(callable_function->appended_arguments()[0].type() == Type::of<int>(), "member capture appended arg type mismatch");

    const CallExpr *call_expr = callable_function->current_call_expr();
    require(call_expr != nullptr, "member capture call expression is null");
    require_eq(call_expr->arguments().size(), static_cast<size_t>(2), "member capture callsite arg count mismatch");
}

void test_subscript_capture() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int[4]> array_value;
        Lambda capture = [&](Var<int> value) {
            return value + array_value[1];
        };
        [[maybe_unused]] Var<int> result = capture(6);
    };
    kernel_function = kernel.function();

    const Function *callable_function = require_single_custom_function(kernel_function, "subscript capture");
    require_eq(callable_function->arguments().size(), static_cast<size_t>(0), "subscript capture explicit arg count mismatch");
    require_eq(callable_function->appended_arguments().size(), static_cast<size_t>(2), "subscript capture appended arg count mismatch");
    require(callable_function->appended_arguments()[0].type() == Type::of<int[4]>(), "subscript capture appended arg type mismatch");

    const CallExpr *call_expr = callable_function->current_call_expr();
    require(call_expr != nullptr, "subscript capture call expression is null");
    require_eq(call_expr->arguments().size(), static_cast<size_t>(2), "subscript capture callsite arg count mismatch");
}

void test_nested_capture_propagates_through_intermediate_callable() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> outer = 13;
        Lambda middle = [&](Var<int> value) {
            Lambda leaf = [&](Var<int> inner_value) {
                return inner_value + outer;
            };
            [[maybe_unused]] Var<int> leaf_result = leaf(value);
            return leaf_result + 1;
        };
        [[maybe_unused]] Var<int> result = middle(8);
    };
    kernel_function = kernel.function();

    const Function *middle_function = require_single_custom_function(kernel_function, "nested middle");
    std::vector<const Function *> nested_functions;
    middle_function->for_each_custom_func([&](const Function *custom_function) {
        nested_functions.push_back(custom_function);
    });
    require_eq(nested_functions.size(), static_cast<size_t>(1), "nested leaf custom function count mismatch");
    const Function *leaf_function = nested_functions.front();

    require_eq(middle_function->arguments().size(), static_cast<size_t>(0), "nested middle explicit arg count mismatch");
    require_eq(leaf_function->arguments().size(), static_cast<size_t>(0), "nested leaf explicit arg count mismatch");
    require_eq(middle_function->appended_arguments().size(), static_cast<size_t>(3), "nested middle appended arg count mismatch");
    require_eq(leaf_function->appended_arguments().size(), static_cast<size_t>(3), "nested leaf appended arg count mismatch");
    require_eq(middle_function->all_arguments().size(), static_cast<size_t>(3), "nested middle all arg count mismatch");
    require_eq(leaf_function->all_arguments().size(), static_cast<size_t>(3), "nested leaf all arg count mismatch");

    const CallExpr *middle_call_expr = middle_function->current_call_expr();
    const CallExpr *leaf_call_expr = leaf_function->current_call_expr();
    require(middle_call_expr != nullptr, "nested middle call expression is null");
    require(leaf_call_expr != nullptr, "nested leaf call expression is null");
    require_eq(middle_call_expr->arguments().size(), static_cast<size_t>(3), "nested middle callsite arg count mismatch");
    require_eq(leaf_call_expr->arguments().size(), static_cast<size_t>(3), "nested leaf callsite arg count mismatch");
}

void test_same_capture_is_reused_across_multiple_callsites() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> outer = 21;
        Lambda capture = [&](Var<int> value) {
            return value + outer;
        };
        [[maybe_unused]] Var<int> first = capture(1);
        [[maybe_unused]] Var<int> second = capture(2);
    };
    kernel_function = kernel.function();

    std::vector<const Function *> custom_functions = collect_custom_functions(kernel_function);
    require_eq(custom_functions.size(), static_cast<size_t>(2), "multi callsite custom function count mismatch");
    for (const Function *callable_function : custom_functions) {
        require_eq(callable_function->arguments().size(), static_cast<size_t>(0), "multi callsite explicit arg count mismatch");
        require_eq(callable_function->appended_arguments().size(), static_cast<size_t>(2), "multi callsite appended arg count mismatch");
        const CallExpr *call_expr = callable_function->current_call_expr();
        require(call_expr != nullptr, "multi callsite call expression is null");
        require_eq(call_expr->arguments().size(), static_cast<size_t>(2), "multi callsite arg count mismatch");
    }
}

// ========== capture_from_invoker multi-level tests ==========

void test_capture_3level_lambda() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> outer = 1;
        Lambda L1 = [&](Var<int> a) {
            Lambda L2 = [&](Var<int> b) {
                Lambda L3 = [&](Var<int> c) {
                    return c + outer;
                };
                return L3(b);
            };
            return L2(a);
        };
        [[maybe_unused]] Var<int> result = L1(10);
    };
    kernel_function = kernel.function();

    const Function *L1_func = require_single_custom_function(kernel_function, "3level L1");
    require_eq(L1_func->arguments().size(), static_cast<size_t>(0), "3level L1 explicit");
    require_eq(L1_func->appended_arguments().size(), static_cast<size_t>(4), "3level L1 appended");
    require_eq(L1_func->current_call_expr()->arguments().size(), static_cast<size_t>(4), "3level L1 callsite");

    std::vector<const Function *> L1_children;
    L1_func->for_each_custom_func([&](const Function *f) { L1_children.push_back(f); });
    require_eq(L1_children.size(), static_cast<size_t>(1), "3level L2 count");
    const Function *L2_func = L1_children.front();
    require_eq(L2_func->arguments().size(), static_cast<size_t>(0), "3level L2 explicit");
    require_eq(L2_func->appended_arguments().size(), static_cast<size_t>(4), "3level L2 appended");
    require_eq(L2_func->current_call_expr()->arguments().size(), static_cast<size_t>(4), "3level L2 callsite");

    std::vector<const Function *> L2_children;
    L2_func->for_each_custom_func([&](const Function *f) { L2_children.push_back(f); });
    require_eq(L2_children.size(), static_cast<size_t>(1), "3level L3 count");
    const Function *L3_func = L2_children.front();
    require_eq(L3_func->arguments().size(), static_cast<size_t>(0), "3level L3 explicit");
    require_eq(L3_func->appended_arguments().size(), static_cast<size_t>(3), "3level L3 appended");
    require_eq(L3_func->current_call_expr()->arguments().size(), static_cast<size_t>(3), "3level L3 callsite");
}

// ========== output_from_invoked tests ==========

void test_output_basic() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> *leaked = nullptr;
        Lambda inner = [&]() {
            leaked = new Var<int>(42);
        };
        inner();
        [[maybe_unused]] Var<int> y = *leaked + 1;
        delete leaked;
    };
    kernel_function = kernel.function();

    const Function *inner_func = require_single_custom_function(kernel_function, "output basic inner");
    require_eq(inner_func->arguments().size(), static_cast<size_t>(0), "output basic explicit");
    require_eq(inner_func->appended_arguments().size(), static_cast<size_t>(1), "output basic appended");
    require_eq(inner_func->current_call_expr()->arguments().size(), static_cast<size_t>(1), "output basic callsite");
}

void test_output_2level() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> *leaked = nullptr;
        Lambda C1 = [&]() {
            Lambda C2 = [&]() {
                leaked = new Var<int>(99);
            };
            C2();
        };
        C1();
        [[maybe_unused]] Var<int> y = *leaked + 1;
        delete leaked;
    };
    kernel_function = kernel.function();

    const Function *C1_func = require_single_custom_function(kernel_function, "output 2level C1");
    require_eq(C1_func->arguments().size(), static_cast<size_t>(0), "output 2level C1 explicit");
    require_eq(C1_func->appended_arguments().size(), static_cast<size_t>(1), "output 2level C1 appended");
    require_eq(C1_func->current_call_expr()->arguments().size(), static_cast<size_t>(1), "output 2level C1 callsite");

    std::vector<const Function *> C1_children;
    C1_func->for_each_custom_func([&](const Function *f) { C1_children.push_back(f); });
    require_eq(C1_children.size(), static_cast<size_t>(1), "output 2level C2 count");
    const Function *C2_func = C1_children.front();
    require_eq(C2_func->arguments().size(), static_cast<size_t>(0), "output 2level C2 explicit");
    require_eq(C2_func->appended_arguments().size(), static_cast<size_t>(1), "output 2level C2 appended");
    require_eq(C2_func->current_call_expr()->arguments().size(), static_cast<size_t>(1), "output 2level C2 callsite");
}

void test_output_sibling_callable() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> *leaked = nullptr;
        Lambda producer = [&]() {
            leaked = new Var<int>(77);
        };
        producer();

        Lambda consumer = [&](Var<int> value) {
            return value + *leaked;
        };
        [[maybe_unused]] Var<int> result = consumer(5);
        delete leaked;
    };
    kernel_function = kernel.function();

    std::vector<const Function *> custom_functions = collect_custom_functions(kernel_function);
    require_eq(custom_functions.size(), static_cast<size_t>(2), "output sibling func count");
    const Function *producer_func = custom_functions[0];
    const Function *consumer_func = custom_functions[1];

    // producer: output var propagated as appended reference arg
    require_eq(producer_func->arguments().size(), static_cast<size_t>(0), "output sibling producer explicit");
    require_eq(producer_func->appended_arguments().size(), static_cast<size_t>(1), "output sibling producer appended");
    require_eq(producer_func->current_call_expr()->arguments().size(), static_cast<size_t>(1), "output sibling producer callsite");

    // consumer: Lambda wrapper → 0 explicit args, appended = value + producer's output
    require_eq(consumer_func->arguments().size(), static_cast<size_t>(0), "output sibling consumer explicit");
    require_eq(consumer_func->appended_arguments().size(), static_cast<size_t>(2), "output sibling consumer appended");
    require_eq(consumer_func->current_call_expr()->arguments().size(), static_cast<size_t>(2), "output sibling consumer callsite");
}

// ========== Callable no-capture test ==========
// Callable capture tests removed: Callable capture is now forbidden at compile time.
// The Lambda versions above cover the same capture scenarios.

void test_callable_no_capture() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Callable callable = [](Var<int> a, Var<int> b) {
            return a + b;
        };
        [[maybe_unused]] Var<int> result = callable(3, 4);
    };
    kernel_function = kernel.function();

    const Function *callable_function = require_single_custom_function(kernel_function, "callable no capture");
    require_eq(callable_function->arguments().size(), static_cast<size_t>(2), "callable no capture explicit arg count mismatch");
    require_eq(callable_function->appended_arguments().size(), static_cast<size_t>(0), "callable no capture appended arg count mismatch");
    require_eq(callable_function->all_arguments().size(), static_cast<size_t>(2), "callable no capture all arg count mismatch");

    const CallExpr *call_expr = callable_function->current_call_expr();
    require(call_expr != nullptr, "callable no capture call expression is null");
    require_eq(call_expr->arguments().size(), static_cast<size_t>(2), "callable no capture callsite arg count mismatch");
}

// test_callable_dedup_same_variable removed: Callable capture now forbidden.

// Each Lambda invocation defines a new callable and must capture in its own context.
void test_repeated_lambda_wrapper() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> x = 1;
        Lambda shared = [&](Var<int> a) {
            return a + x;
        };
        Lambda L = [&](Var<int> n) {
            return shared(n);
        };
        [[maybe_unused]] Var<int> r1 = L(10);
        [[maybe_unused]] Var<int> r2 = L(20);
    };
    kernel_function = kernel.function();

    auto custom = collect_custom_functions(kernel_function);
    require_eq(custom.size(), size_t{2}, "repeated wrapper count");
}

void test_repeated_lambda_capture() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Float x = 1;
        Lambda C = [&](Var<int> a) {
            return a + x;
        };
        [[maybe_unused]] auto r1 = C(10);
        [[maybe_unused]] auto r2 = C(20);
    };
    kernel_function = kernel.function();

    auto custom = collect_custom_functions(kernel_function);
    require_eq(custom.size(), size_t{2}, "repeated capture wrapper count");
}

void test_nested_lambda_wrapper() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        Var<int> x = 1;
        Lambda mid = [&](Var<int> a) {
            return a + x;
        };
        Lambda outer = [&](Var<int> n) {
            Lambda inner = [&](Var<int> m) {
                return mid(m);
            };
            return inner(n);
        };
        [[maybe_unused]] Var<int> r1 = outer(10);
        [[maybe_unused]] Var<int> r2 = outer(20);
    };
    kernel_function = kernel.function();

    auto custom = collect_custom_functions(kernel_function);
    require_eq(custom.size(), size_t{2}, "nested wrapper count");
}

void test_repeated_lambda_output() {
    shared_ptr<Function> kernel_function;

    [[maybe_unused]] Kernel kernel = [&] {
        std::unique_ptr<Var<int>> leaked;
        Lambda producer = [&]() {
            leaked = std::make_unique<Var<int>>(77);
        };
        producer();
        producer();
        $outline{
            producer();
            producer();
        };
        [[maybe_unused]] Var<int> y = *leaked + 1;
    };
    kernel_function = kernel.function();

    auto custom = collect_custom_functions(kernel_function);
    require_eq(custom.size(), size_t{3}, "output wrapper count");
}

}// namespace

int main(int argc, char **argv) {
    std::string_view selected = argc == 2 ? argv[1] : "";
    if (selected == "--probe-shared-capture") {
        probe_function_corrector_shared_capture();
        return 0;
    }
    if (selected == "--probe-forbidden-capture") {
        probe_function_corrector_forbidden_capture();
        return 0;
    }
    struct TestCase {
        std::string_view name;
        void (*run)();
    };
    constexpr TestCase cases[] = {
        {"single-capture", test_single_capture_deduplicates_repeated_use},
        {"multiple-captures", test_multiple_outer_variables_are_captured},
        {"member-capture", test_member_expression_capture},
        {"subscript-capture", test_subscript_capture},
        {"nested-capture", test_nested_capture_propagates_through_intermediate_callable},
        {"lambda-calls", test_same_capture_is_reused_across_multiple_callsites},
        {"three-level-lambda", test_capture_3level_lambda},
        {"output", test_output_basic},
        {"nested-output", test_output_2level},
        {"sibling-capture", test_output_sibling_callable},
        {"no-capture", test_callable_no_capture},
        {"repeated-lambda-wrapper", test_repeated_lambda_wrapper},
        {"repeated-lambda-capture", test_repeated_lambda_capture},
        {"nested-lambda-wrapper", test_nested_lambda_wrapper},
        {"repeated-lambda-output", test_repeated_lambda_output},
        {"visitors", test_function_corrector_visitors},
        {"shared-calls", test_function_corrector_shared_calls},
        {"capture-after-resource", test_function_corrector_capture_after_resource},
        {"mixed-output-calls", test_function_corrector_mixed_output_calls},
        {"sibling-outputs", test_function_corrector_sibling_outputs},
        {"parameters", test_function_corrector_parameters},
    };
    if (argc > 2) {
        fail("usage: test-core-function-corrector [case]");
    }
    unsigned int count = 0;
    for (const auto &test : cases) {
        if (argc == 2 && selected != test.name) {
            continue;
        }
        std::cout << "running " << test.name << std::endl;
        try {
            test.run();
        } catch (const std::exception &error) {
            fail(std::string{test.name} + ": " + error.what());
        }
        ++count;
    }
    if (count == 0) {
        fail("unknown case: " + std::string{selected});
    }
    std::cout << count << " function corrector cases passed" << std::endl;
}
