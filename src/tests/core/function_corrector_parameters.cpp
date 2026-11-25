#include "ast/expression.h"
#include "ast/function.h"
#include "ast/statement.h"
#include "dsl/dsl.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace function_corrector_fixtures {

struct Record {
    ocarina::uint flag;
};

struct InnerParameters {
    int code;
    ocarina::float2 coords;
    Record record;
};

struct Parameters {
    int mode;
    InnerParameters inner;
    float unused;
};

struct WrappedParameters {
    Parameters parameters;
};

}// namespace function_corrector_fixtures

OC_STRUCT(function_corrector_fixtures, Record, flag) {};
OC_PARAM_STRUCT(function_corrector_fixtures, InnerParameters, code, coords, record) {};
OC_PARAM_STRUCT(function_corrector_fixtures, Parameters, mode, inner, unused) {};
OC_PARAM_STRUCT(function_corrector_fixtures, WrappedParameters, parameters) {};

namespace {

using namespace ocarina;
using function_corrector_fixtures::ParametersVar;
using function_corrector_fixtures::Record;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_usage(Usage actual, Usage expected, const std::string &message) {
    if (actual != expected) {
        throw std::runtime_error(message + ": expected " + std::to_string(to_underlying(expected)) +
                                 ", got " + std::to_string(to_underlying(actual)));
    }
}

void require_ref(const Expression *expression, const Function &owner, const Variable &variable,
                 const char *message) {
    require(expression != nullptr && expression->tag() == Expression::Tag::REF &&
                expression->context() == &owner && expression->type() == variable.type() &&
                static_cast<const RefExpr *>(expression)->variable().uid() == variable.uid(),
            message);
}

const Function &require_single_call(const Function &caller, const Variable &actual, Variable::Tag formal_tag,
                                    Usage expected_usage) {
    const Function *callee = nullptr;
    int custom_functions = 0;
    caller.for_each_custom_func([&](const Function *function) {
        callee = function;
        ++custom_functions;
    });
    require(custom_functions == 1, "caller should register exactly one custom function");
    require(callee->all_call_expr().size() == 1, "callee should have exactly one callsite");
    const CallExpr *call = callee->all_call_expr().front();
    require(call->context() == &caller && call->function() == callee && call->arguments().size() == 1,
            "callsite should connect the caller to the expected unary callee");
    require_ref(call->argument(0), caller, actual, "call argument should retain its caller variable identity");

    const auto formals = callee->arguments();
    require(formals.size() == 1 && callee->appended_arguments().empty(),
            "an explicit argument should not generate captured parameters");
    require(formals[0].tag() == formal_tag && formals[0].type() == actual.type(),
            "callee should preserve the argument type and passing mode");
    const std::string type{actual.type()->description()};
    require_usage(callee->variable_usage(formals[0].uid()), expected_usage, type + " callee formal usage");
    require_usage(caller.variable_usage(actual.uid()), expected_usage, type + " caller argument usage");
    return *callee;
}

void test_scalar_and_reference_usage() {
    auto read_kernel = Function::define_kernel([] {
        auto *kernel = Function::current();
        const auto *type = Type::of<int>();
        const auto *input = kernel->argument(type);
        auto read = Function::define_callable([&] {
            auto *callee = Function::current();
            const auto *value = callee->argument(type);
            callee->return_(callee->binary(type, BinaryOp::ADD, value, callee->literal(type, 1)));
        });
        kernel->expr_statement(kernel->call(type, read, {input}));
    });
    const Function &reader = *read_kernel;
    require(reader.arguments().size() == 1, "read kernel should have one scalar argument");
    require_single_call(reader, reader.arguments()[0], Variable::Tag::LOCAL, Usage::READ);

    const RefExpr *target = nullptr;
    auto update_kernel = Function::define_kernel([&] {
        auto *kernel = Function::current();
        const auto *type = Type::of<int>();
        target = kernel->local(type);
        auto relay = Function::define_callable([&] {
            auto *wrapper = Function::current();
            const auto *value = wrapper->reference_argument(type);
            auto increment = Function::define_callable([&] {
                auto *callee = Function::current();
                const auto *operand = callee->reference_argument(type);
                callee->assign(operand, callee->binary(type, BinaryOp::ADD, operand, callee->literal(type, 1)));
            });
            wrapper->expr_statement(wrapper->call(nullptr, increment, {value}));
        });
        kernel->expr_statement(kernel->call(nullptr, relay, {target}));
    });
    const Function &relay = require_single_call(*update_kernel, target->variable(), Variable::Tag::REFERENCE,
                                                Usage::READ_WRITE);
    require_single_call(relay, relay.arguments()[0], Variable::Tag::REFERENCE, Usage::READ_WRITE);
}

template<Usage access, bool kernel_reads = false>
void test_typed_buffer_usage() {
    Kernel kernel = [](BufferVar<int> buffer) {
        if constexpr (kernel_reads) {
            [[maybe_unused]] auto value = buffer.read(0u, false);
        }
        Callable relay = [](BufferVar<int> forwarded) {
            Callable use_buffer = [](BufferVar<int> data) {
                if constexpr (access != Usage::WRITE) {
                    [[maybe_unused]] auto value = data.read(0u, false);
                }
                if constexpr (access != Usage::READ) {
                    data.write(1u, 7, false);
                }
            };
            use_buffer(BufferVar<int>{forwarded.expression()});
        };
        relay(BufferVar<int>{buffer.expression()});
    };
    const Function &root = *kernel.function();
    const auto arguments = root.arguments();
    require(arguments.size() == 1 && arguments[0].type() == Type::of<Buffer<int>>() &&
                arguments[0].tag() == Variable::Tag::BUFFER,
            "kernel should expose its typed resource as a buffer argument");
    constexpr Usage expected_usage = kernel_reads ? Usage::READ_WRITE : access;
    const Function &relay = require_single_call(root, arguments[0], Variable::Tag::BUFFER, expected_usage);
    require_single_call(relay, relay.arguments()[0], Variable::Tag::BUFFER, expected_usage);
}

const MemberExpr &require_member(const Expression *expression, const Function &owner, bool swizzle,
                                  const char *message) {
    require(expression != nullptr && expression->tag() == Expression::Tag::MEMBER &&
                expression->context() == &owner,
            message);
    const auto &member = *static_cast<const MemberExpr *>(expression);
    require(member.is_swizzle() == swizzle, message);
    return member;
}

void test_argument_declaration_order() {
    std::array<const Expression *, 3> reads{};
    Kernel kernel = [&](Uint count, Float2 pair, Float4 quad) {
        reads = {count.expression(), pair.expression(), quad.expression()};
    };
    const Function &function = *kernel.function();
    const auto arguments = function.arguments();
    const std::array expected_types{Type::of<uint>(), Type::of<float2>(), Type::of<float4>()};
    require(arguments.size() == expected_types.size(), "all explicit arguments should be retained");
    for (size_t index = 0; index < arguments.size(); ++index) {
        require(arguments[index].type() == expected_types[index],
                "DSL argument order must match the shader invocation argument order");
        require_ref(reads[index], function, arguments[index],
                    "each lambda parameter must reference its matching formal argument");
    }
}

void test_nested_parameter_struct() {
    Kernel kernel = [](ParametersVar parameters) {
        [[maybe_unused]] Int mode = parameters.mode;
        [[maybe_unused]] Int code = parameters.inner.code;
        [[maybe_unused]] Float component = parameters.inner.coords.y;
        [[maybe_unused]] Uint flag = parameters.inner.record.flag;
        [[maybe_unused]] Int same_code = parameters.inner.code;
    };
    const Function &function = *kernel.function();
    const auto arguments = function.arguments();
    const std::array expected_types{Type::of<int>(), Type::of<int>(), Type::of<float2>(),
                                    Type::of<Record>(), Type::of<float>()};
    require(arguments.size() == expected_types.size() && function.appended_arguments().empty(),
            "kernel should flatten all parameter leaves and retain the unused leaf");
    for (size_t index = 0; index < arguments.size(); ++index) {
        require(arguments[index].type() == expected_types[index],
                "kernel arguments should follow reflected depth-first leaf order");
        for (size_t prior = 0; prior < index; ++prior) {
            require(arguments[index].uid() != arguments[prior].uid(), "each parameter leaf needs a distinct variable");
        }
    }

    std::vector<const Expression *> values;
    for (const Statement *statement : function.body()->statements()) {
        if (statement->tag() == Statement::Tag::ASSIGN) {
            values.push_back(static_cast<const AssignStmt *>(statement)->rhs());
        }
    }
    require(values.size() == 5, "kernel should contain the five leaf reads");
    require_ref(values[0], function, arguments[0], "outer scalar should reference its flattened argument");
    require_ref(values[1], function, arguments[1], "inner scalar should reference its flattened argument");
    require_ref(values[4], function, arguments[1], "repeated inner scalar use should reuse the same argument");

    const MemberExpr &component = require_member(values[2], function, true, "vector swizzle should survive splitting");
    require(component.swizzle_size() == 1 && component.swizzle_index(0) == 1,
            "splitting should preserve the selected vector component");
    require_ref(component.parent(), function, arguments[2], "swizzle should use the flattened vector argument");

    const MemberExpr &flag = require_member(values[3], function, false, "ordinary struct member should survive splitting");
    require(flag.member_index() == 0, "splitting should preserve the ordinary struct field index");
    require_ref(flag.parent(), function, arguments[3], "ordinary field should use the unsplit record argument");
    require(function.body()->check_context(&function), "rewritten parameter expressions should belong to the kernel");
}

void test_capture_of_parameter_leaf() {
    shared_ptr<Function> callee;
    const ExprStmt *use = nullptr;
    auto kernel = Function::define_kernel([&] {
        auto *caller = Function::current();
        const auto *wrapper = caller->argument(Type::of<function_corrector_fixtures::WrappedParameters>());
        const auto *parameters = caller->member(Type::of<function_corrector_fixtures::Parameters>(), wrapper, 0);
        const auto *inner = caller->member(Type::of<function_corrector_fixtures::InnerParameters>(), parameters, 1);
        const auto *code = caller->member(Type::of<int>(), inner, 0);
        callee = Function::define_callable([&] { use = Function::current()->expr_statement(code); });
        callee->set_allow_dsl_capture(true);
        caller->expr_statement(caller->call(nullptr, callee, {}));
    });
    require(kernel->arguments().size() == 5, "nested parameter leaves should be flattened before dispatch");
    require(callee->appended_arguments().size() == 1, "the leaf should create one capture formal");
    const auto *call = callee->current_call_expr();
    require(call->arguments().size() == 1, "the callsite should pass the captured parameter leaf");
    require_ref(call->argument(0), *kernel, kernel->arguments()[1], "capture should pass the flattened inner scalar");
    require_ref(use->expression(), *callee, callee->appended_arguments().front(), "callee should use the capture formal");
}

}// namespace

void test_function_corrector_parameters() {
    test_argument_declaration_order();
    test_scalar_and_reference_usage();
    test_typed_buffer_usage<ocarina::Usage::READ>();
    test_typed_buffer_usage<ocarina::Usage::WRITE>();
    test_typed_buffer_usage<ocarina::Usage::READ_WRITE>();
    test_typed_buffer_usage<ocarina::Usage::WRITE, true>();
    test_nested_parameter_struct();
    test_capture_of_parameter_leaf();
}
