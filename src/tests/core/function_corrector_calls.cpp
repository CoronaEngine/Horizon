#include "ast/expression.h"
#include "ast/function.h"
#include "dsl/api/func.h"
#include "dsl/dsl.h"

#include <array>
#include <stdexcept>
#include <utility>

using namespace ocarina;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const RefExpr &require_ref(const Expression *expression, const Function *owner, const char *message) {
    const auto *ref = dynamic_cast<const RefExpr *>(expression);
    require(ref && ref->context() == owner, message);
    return *ref;
}

void require_same_ref(const Expression *actual, const Expression *expected, const char *message) {
    require(expected != nullptr, message);
    const auto *owner = expected->context();
    require(require_ref(actual, owner, message).variable().uid() ==
                require_ref(expected, owner, message).variable().uid(),
            message);
}

void require_arity(const CallExpr *call, const Function *callee, size_t count, const char *message) {
    require(call && call->function() == callee && call->arguments().size() == count &&
                callee->all_arguments().size() == count && call->check_context(call->context()),
            message);
}

void require_output_write(const Function &producer, const Expression *source) {
    const auto formal = producer.appended_arguments().back();
    require(formal.tag() == Variable::Tag::REFERENCE, "producer output must be passed by reference");
    size_t copies = 0;
    for (const auto *statement : producer.body()->statements()) {
        const auto *assign = dynamic_cast<const AssignStmt *>(statement);
        const auto *destination = assign ? dynamic_cast<const RefExpr *>(assign->lhs()) : nullptr;
        if (destination && destination->variable().uid() == formal.uid()) {
            require(destination->context() == &producer, "output formal must belong to the producer");
            require_same_ref(assign->rhs(), source, "output copy must read the producer's original local");
            ++copies;
        }
    }
    require(copies == 1, "producer must copy its output local exactly once");
}

}// namespace

void test_function_corrector_shared_calls() {
    shared_ptr<Function> callee;
    const Expression *capture = nullptr;
    const BinaryExpr *repeated = nullptr;
    const BinaryExpr *sum = nullptr;
    std::array<const Expression *, 4> explicit_actuals{};

    Kernel kernel = [&] {
        Var<int> outer = 7;
        const auto &alias = outer;
        capture = outer.expression();
        Callable shared{[&](Var<int> value) {
            auto *function = Function::current();
            repeated = function->binary(Type::of<int>(), BinaryOp::ADD, outer.expression(), alias.expression());
            sum = function->binary(Type::of<int>(), BinaryOp::ADD, value.expression(), repeated);
            function->expr_statement(sum);
        },
                        detail::allow_capture_tag};
        callee = shared.function();
        callee->set_allow_dsl_capture(true);

        auto invoke = [&](size_t index, int value) {
            Var<int> actual = value;
            explicit_actuals[index] = actual.expression();
            shared(std::move(actual));
        };
        invoke(0, 11);
        invoke(1, 13);
        outline([&] { invoke(2, 17); });
        outline([&] { outline([&] { invoke(3, 19); }); });
    };
    const auto *root = kernel.function().get();
    require(callee && callee->arguments().size() == 1 && callee->appended_arguments().size() == 1,
            "shared callee must have one explicit formal and one deduplicated capture formal");
    const auto explicit_uid = callee->arguments().front().uid();
    const auto capture_uid = callee->appended_arguments().front().uid();
    require(require_ref(sum->lhs(), callee.get(), "callee explicit operand belongs to the callee").variable().uid() ==
                explicit_uid,
            "callee explicit operand must use the explicit formal");
    require(require_ref(repeated->lhs(), callee.get(), "first captured operand belongs to the callee").variable().uid() ==
                capture_uid &&
                require_ref(repeated->rhs(), callee.get(), "second captured operand belongs to the callee").variable().uid() ==
                    capture_uid,
            "both captured operands must use the same formal UID");

    const auto &calls = callee->all_call_expr();
    require(calls.size() == explicit_actuals.size(), "all four invocations must share the same callee");
    for (size_t index = 0; index < calls.size(); ++index) {
        const auto *call = calls[index];
        require_arity(call, callee.get(), 2, "shared callee call has invalid arguments");
        require_same_ref(call->argument(0), explicit_actuals[index], "call must preserve its own explicit argument");
        const Expression *forwarded = call->argument(1);
        const Function *caller = call->context();
        const std::array<size_t, 4> depths{0, 0, 1, 2};
        for (size_t depth = 0; depth < depths[index]; ++depth) {
            require(caller != root && caller->appended_arguments().size() == 1,
                    "each intermediate wrapper must have one capture formal");
            require(require_ref(forwarded, caller, "intermediate capture belongs to its caller").variable().uid() ==
                        caller->appended_arguments().front().uid(),
                    "wrapper must forward its capture formal");
            const auto *wrapper_call = caller->current_call_expr();
            require_arity(wrapper_call, caller, 1, "wrapper call must supply one capture argument");
            forwarded = wrapper_call->argument(0);
            caller = wrapper_call->context();
        }
        require(caller == root, "each capture must be forwarded from the kernel at the expected depth");
        require_same_ref(forwarded, capture, "each call must ultimately capture the original kernel local");
    }
}

void test_function_corrector_capture_after_resource() {
    int resource_handle = 0;
    shared_ptr<Function> callee;
    const RefExpr *outer = nullptr;
    const RefExpr *resource = nullptr;
    const ExprStmt *use = nullptr;
    std::array<const RefExpr *, 2> actuals{};
    auto kernel = Function::define_kernel([&] {
        auto *caller = Function::current();
        auto *int_type = Type::of<int>();
        auto *buffer_type = Type::of<Buffer<int>>();
        MemoryBlock block{&resource_handle, sizeof(resource_handle), alignof(int), sizeof(resource_handle)};
        outer = caller->local(int_type);
        resource = caller->get_captured_resource(buffer_type, Variable::Tag::BUFFER, block).expression();
        callee = Function::define_callable([&] {
            auto *function = Function::current();
            function->expr_statement(function->argument(int_type));
            const auto *buffer = function->get_captured_resource(buffer_type, Variable::Tag::BUFFER, block).expression();
            buffer->mark(Usage::READ);
            function->expr_statement(buffer);
            use = function->expr_statement(outer);
        });
        callee->set_allow_dsl_capture(true);
        for (auto &actual : actuals) {
            actual = caller->local(int_type);
            caller->expr_statement(caller->call(nullptr, callee, {actual, resource}));
        }
    });
    require(callee->arguments().size() == 1 && callee->captured_resources().size() == 1 &&
                callee->appended_arguments().size() == 1,
            "callee must retain its explicit, resource, and capture formals");
    require(require_ref(use->expression(), callee.get(), "capture use must belong to the callee").variable().uid() ==
                callee->appended_arguments().front().uid(),
            "capture use must refer to its appended formal");
    const auto &calls = callee->all_call_expr();
    require(calls.size() == actuals.size(), "both calls must share the resource-bearing callee");
    for (size_t i = 0; i < calls.size(); ++i) {
        require_arity(calls[i], callee.get(), 3, "call must retain all three argument kinds");
        require(calls[i]->context() == kernel.get(), "call must belong to the kernel");
        require_same_ref(calls[i]->argument(0), actuals[i], "explicit argument must keep its first position");
        require_same_ref(calls[i]->argument(1), resource, "captured resource must keep its second position");
        require_same_ref(calls[i]->argument(2), outer, "scalar capture must follow the captured resource");
    }
}

void test_function_corrector_mixed_output_calls() {
    shared_ptr<Function> callee;
    const Expression *capture = nullptr;
    const Expression *produced = nullptr;
    const BinaryExpr *use = nullptr;

    Kernel kernel = [&] {
        Var<float> outer = 7.0f;
        capture = outer.expression();
        Callable producer{[&] {
            [[maybe_unused]] Var<float> copied = outer + 1.0f;
            Var<int> output = 42;
            produced = output.expression();
        },
                          detail::allow_capture_tag};
        callee = producer.function();
        callee->set_allow_dsl_capture(true);
        producer();
        auto *function = Function::current();
        use = function->binary(Type::of<int>(), BinaryOp::ADD, produced, produced);
        function->expr_statement(use);
        producer();
    };
    const auto *root = kernel.function().get();
    require(callee && callee->appended_arguments().size() == 2,
            "producer must have a capture formal and an output formal");
    require(callee->appended_arguments()[0].type() == Type::of<float>() &&
                callee->appended_arguments()[1].type() == Type::of<int>(),
            "capture and output formals must preserve discovery order and types");
    require_output_write(*callee, produced);
    const auto &sink = require_ref(use->lhs(), root, "output consumer must use a kernel local");
    require(sink.variable().tag() == Variable::Tag::LOCAL, "the output sink must be a local");
    require_same_ref(use->rhs(), &sink, "repeated output uses must share one kernel local");

    const auto &calls = callee->all_call_expr();
    require(calls.size() == 2, "both invocations must share the producer");
    for (const auto *call : calls) {
        require_arity(call, callee.get(), 2, "producer call must supply both capture and output arguments");
        require_same_ref(call->argument(0), capture, "producer call must supply its float capture first");
        require_same_ref(call->argument(1), &sink, "producer call must supply its int output sink second");
    }
}

void test_function_corrector_sibling_outputs() {
    shared_ptr<Function> callee;
    const Expression *produced = nullptr;
    const BinaryExpr *use = nullptr;

    Kernel kernel = [&] {
        Callable producer{[&] {
            Var<int> output = 42;
            produced = output.expression();
        },
                          detail::allow_capture_tag};
        callee = producer.function();
        outline([&] {
            producer();
            producer();
        });
        outline([&] { producer(); });
        auto *function = Function::current();
        use = function->binary(Type::of<int>(), BinaryOp::ADD, produced, produced);
        function->expr_statement(use);
    };
    const auto *root = kernel.function().get();
    require(callee && callee->appended_arguments().size() == 1,
            "shared producer must have a single output formal");
    require_output_write(*callee, produced);
    const auto &sink = require_ref(use->lhs(), root, "sibling producer output must resolve to a kernel local");
    require(sink.variable().tag() == Variable::Tag::LOCAL, "the shared output sink must be a local");
    require_same_ref(use->rhs(), &sink, "both reads must use the same output sink");

    const auto &calls = callee->all_call_expr();
    require(calls.size() == 3 && calls[0]->context() == calls[1]->context() &&
                calls[1]->context() != calls[2]->context(),
            "the producer must be shared across repeated and sibling calls");
    for (const auto *call : calls) {
        require_arity(call, callee.get(), 1, "each sibling must supply an output argument to the shared producer");
        const auto *wrapper = call->context();
        require(wrapper->appended_arguments().size() == 1, "each sibling must have one output formal");
        require(require_ref(call->argument(0), wrapper, "producer output argument belongs to its wrapper").variable().uid() ==
                    wrapper->appended_arguments().front().uid(),
                "producer output must be bound to the wrapper output formal");

        const auto *wrapper_call = wrapper->current_call_expr();
        require_arity(wrapper_call, wrapper, 1, "both kernel callsites must supply a wrapper output argument");
        require(wrapper_call->context() == root, "the wrappers must both be called from the kernel");
        require_same_ref(wrapper_call->argument(0), &sink, "both wrappers must forward to the shared kernel sink");
    }
}
