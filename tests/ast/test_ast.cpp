#include "ast/function.h"
#include "ast/layout_resolver.h"
#include "ast/type_desc.h"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_description(const horizon::core::Type *type,
                        horizon::core::string_view expected,
                        const char *message) {
    expect(type != nullptr, message);
    if (type != nullptr) {
        if (type->description() != expected) {
            std::cerr << "FAIL: " << message << " (expected '" << expected
                      << "', actual '" << type->description() << "')\n";
            ++failures;
        }
    }
}

void test_layout_resolver_precision_and_composite_types() {
    using horizon::ast::LayoutResolver;
    using horizon::core::Buffer;
    using horizon::core::PrecisionPolicy;
    using horizon::core::StoragePrecisionPolicy;
    using horizon::core::Type;
    using horizon::core::array;
    using horizon::math::real;
    using horizon::math::real3;
    using horizon::math::real3x2;

    const LayoutResolver disallow_real{};
    expect(disallow_real.resolve(Type::of<real>()) == nullptr,
           "real storage resolution is rejected when real is disallowed");
    expect(disallow_real.resolve(Type::of<real3>()) == nullptr,
           "real vector storage resolution is rejected when real is disallowed");

    const LayoutResolver f16{StoragePrecisionPolicy{
        .policy = PrecisionPolicy::force_f16,
        .allow_real_in_storage = true}};
    expect_description(f16.resolve(Type::of<real>()), "half",
                       "real resolves to half with f16 storage policy");
    expect_description(f16.resolve(Type::of<real3>()), "vector<half,3>",
                       "real3 resolves to half vector with f16 storage policy");
    expect_description(f16.resolve(Type::of<real3x2>()), "matrix<half,2,3>",
                       "real matrix resolves to half matrix with f16 storage policy");
    expect_description(f16.resolve(Type::of<array<real, 2>>()), "array<half,2>",
                       "real array resolves element precision with f16 storage policy");
    expect_description(f16.resolve(Type::of<Buffer<real>>()), "buffer<half>",
                       "real buffer resolves element precision with f16 storage policy");

    const LayoutResolver f32{StoragePrecisionPolicy{
        .policy = PrecisionPolicy::force_f32,
        .allow_real_in_storage = true}};
    expect_description(f32.resolve(Type::of<real3x2>()), "matrix<float,2,3>",
                       "real matrix resolves to float matrix with f32 storage policy");
}

void test_function_builds_a_context_consistent_statement_tree() {
    using horizon::ast::BinaryExpr;
    using horizon::ast::BinaryOp;
    using horizon::ast::Expression;
    using horizon::ast::Function;
    using horizon::ast::IfStmt;
    using horizon::ast::LiteralExpr;
    using horizon::ast::RefExpr;
    using horizon::ast::Statement;
    using horizon::ast::Usage;
    using horizon::core::Type;
    using horizon::math::basic_literal_t;

    const RefExpr *local = nullptr;
    const LiteralExpr *literal = nullptr;
    const BinaryExpr *sum = nullptr;
    IfStmt *if_statement = nullptr;

    const auto function = Function::define_kernel([&] {
        auto *current = Function::current();
        local = current->local(Type::of<float>());
        literal = current->literal(Type::of<float>(), basic_literal_t{1.0f});
        sum = current->binary(Type::of<float>(), BinaryOp::ADD, local, literal);
        current->assign(local, sum);

        const auto *condition = current->literal(Type::of<bool>(), basic_literal_t{true});
        if_statement = current->if_(condition);
        current->with(if_statement->true_branch(), [&] {
            current->comment("inside true branch");
        });
        current->return_(sum);
    });

    expect(Function::current() == nullptr, "function construction restores the current function stack");
    expect(function->is_kernel(), "define_kernel creates a kernel function");
    expect(function->body()->check_context(function.get()),
           "every generated AST node belongs to its function context");
    expect(function->return_type() == Type::of<float>(), "return type follows the return expression type");
    expect(function->body()->local_vars().size() == 1u, "local variable is registered in function body scope");
    expect(function->body()->size() == 3u, "assignment, if, and return are appended to function body");
    expect(function->body()->statements()[0]->tag() == Statement::Tag::ASSIGN,
           "assignment is the first statement in function body");
    expect(function->body()->statements()[1]->tag() == Statement::Tag::IF,
           "if statement is appended to function body");
    expect(function->body()->statements()[2]->tag() == Statement::Tag::RETURN,
           "return statement is appended to function body");
    expect(if_statement->true_branch()->size() == 1u,
           "statements created through with() are appended to the selected scope");
    expect(local->tag() == Expression::Tag::REF, "local variable is represented by a reference expression");
    expect(literal->tag() == Expression::Tag::LITERAL, "literal builder creates a literal expression");
    expect(sum->tag() == Expression::Tag::BINARY && sum->lhs() == local && sum->rhs() == literal,
           "binary builder preserves operands and expression tag");
    expect(local->variable().usage() == Usage::READ_WRITE,
           "assignment and expression use propagate read-write usage to local variable");
}

void test_expression_builders_preserve_operands_and_usage() {
    using horizon::ast::BinaryExpr;
    using horizon::ast::BinaryOp;
    using horizon::ast::CastExpr;
    using horizon::ast::CastOp;
    using horizon::ast::Function;
    using horizon::ast::MemberExpr;
    using horizon::ast::SubscriptExpr;
    using horizon::ast::Usage;
    using horizon::core::Type;
    using horizon::math::basic_literal_t;

    const BinaryExpr *sum = nullptr;
    const CastExpr *cast = nullptr;
    const SubscriptExpr *subscript = nullptr;
    const MemberExpr *swizzle = nullptr;
    const horizon::ast::RefExpr *lhs = nullptr;
    const horizon::ast::RefExpr *rhs = nullptr;
    const horizon::ast::RefExpr *vector = nullptr;

    const auto function = Function::define_callable([&] {
        auto *current = Function::current();
        lhs = current->local(Type::of<float>());
        rhs = current->local(Type::of<float>());
        sum = current->binary(Type::of<float>(), BinaryOp::ADD, lhs, rhs);
        cast = current->cast(Type::of<int>(), CastOp::STATIC, sum);

        vector = current->local(Type::of<horizon::math::float4>());
        const auto *index = current->literal(Type::of<int>(), basic_literal_t{1});
        subscript = current->subscript(Type::of<float>(), vector, index);
        swizzle = current->swizzle(Type::of<horizon::math::float2>(), vector, 0x01u, 2u);
    });

    expect(function->body()->check_context(function.get()),
           "callable expression tree keeps a single function context");
    expect(sum->lhs() == lhs && sum->rhs() == rhs && sum->op() == BinaryOp::ADD,
           "binary expression exposes the original operands and operator");
    expect(lhs->variable().usage() == Usage::READ && rhs->variable().usage() == Usage::READ,
           "binary construction marks both variable operands as read");
    expect(cast->expression() == sum && cast->cast_op() == CastOp::STATIC,
           "cast expression preserves operand and cast operation");
    expect(subscript->range() == vector && subscript->index(0) != nullptr,
           "subscript expression preserves range and index");
    subscript->mark(Usage::READ);
    expect(vector->variable().usage() == Usage::READ,
           "marking a subscript forwards usage to its range expression");
    expect(swizzle->is_swizzle() && swizzle->parent() == vector && swizzle->swizzle_size() == 2,
           "swizzle expression retains parent and swizzle metadata");
    expect(swizzle->swizzle_index(0) == 0 && swizzle->swizzle_index(1) == 1,
           "swizzle mask exposes component order");
}

void test_control_flow_builders_keep_nested_scopes_isolated() {
    using horizon::ast::ForStmt;
    using horizon::ast::Function;
    using horizon::ast::IfStmt;
    using horizon::ast::LoopStmt;
    using horizon::ast::ScopeStmt;
    using horizon::ast::Statement;
    using horizon::core::Type;
    using horizon::math::basic_literal_t;

    IfStmt *if_statement = nullptr;
    LoopStmt *loop_statement = nullptr;
    ForStmt *for_statement = nullptr;
    ScopeStmt *nested_scope = nullptr;

    const auto function = Function::define_callable([&] {
        auto *current = Function::current();
        const auto *index = current->local(Type::of<int>());
        const auto *condition = current->literal(Type::of<bool>(), basic_literal_t{true});

        if_statement = current->if_(condition);
        current->with(if_statement->true_branch(), [&] {
            nested_scope = current->scope();
            current->with(nested_scope, [&] {
                current->comment("nested true branch");
            });
        });
        current->with(if_statement->false_branch(), [&] {
            current->comment("false branch");
        });

        loop_statement = current->loop();
        current->with(loop_statement->body(), [&] {
            current->continue_();
            current->break_();
        });

        for_statement = current->for_(index, condition, index);
        current->with(for_statement->body(), [&] {
            current->comment("for body");
        });
    });

    expect(function->body()->check_context(function.get()),
           "nested control-flow nodes retain the callable context");
    expect(function->body()->size() == 3u,
           "control-flow builders append only their root statements to the function body");
    expect(function->body()->statements()[0]->tag() == Statement::Tag::IF &&
               function->body()->statements()[1]->tag() == Statement::Tag::LOOP &&
               function->body()->statements()[2]->tag() == Statement::Tag::FOR,
           "if, loop, and for statements preserve construction order");
    expect(if_statement->true_branch()->size() == 1u && if_statement->false_branch()->size() == 1u,
           "if branches own statements built through their selected scopes");
    expect(nested_scope->size() == 1u && nested_scope->statements()[0]->tag() == Statement::Tag::COMMENT,
           "a nested scope keeps its statement out of the enclosing branch");
    expect(loop_statement->body()->size() == 2u &&
               loop_statement->body()->statements()[0]->tag() == Statement::Tag::CONTINUE &&
               loop_statement->body()->statements()[1]->tag() == Statement::Tag::BREAK,
           "continue and break are attached to the loop body in order");
    expect(for_statement->var() == for_statement->step() && for_statement->condition() == if_statement->condition(),
           "for statement preserves initializer, condition, and step expressions");
    expect(for_statement->body()->size() == 1u && for_statement->body()->statements()[0]->tag() == Statement::Tag::COMMENT,
           "for body owns statements built through its selected scope");
}

void test_function_signature_builders_preserve_parameter_categories() {
    using horizon::ast::Function;
    using horizon::ast::RefExpr;
    using horizon::ast::Variable;
    using horizon::core::Buffer;
    using horizon::core::Type;

    const RefExpr *value_argument = nullptr;
    const RefExpr *buffer_argument = nullptr;
    const RefExpr *reference_argument = nullptr;
    const RefExpr *kernel_buffer_argument = nullptr;
    const RefExpr *thread_index = nullptr;

    const auto callable = Function::define_callable([&] {
        auto *current = Function::current();
        value_argument = current->argument(Type::of<float>());
        buffer_argument = current->argument(Type::of<Buffer<float>>());
        reference_argument = current->reference_argument(Type::of<int>());
        current->return_(value_argument);
    });

    const auto kernel = Function::define_kernel([&] {
        auto *current = Function::current();
        kernel_buffer_argument = current->argument(Type::of<Buffer<float>>());
        thread_index = current->thread_idx();
    });

    expect(callable->is_callable() && callable->return_type() == Type::of<float>(),
           "callable return type follows its returned parameter");
    expect(callable->arguments().size() == 3u,
           "callable records every value, resource, and reference parameter");
    expect(value_argument->variable().tag() == Variable::Tag::LOCAL &&
               buffer_argument->variable().tag() == Variable::Tag::BUFFER &&
               reference_argument->variable().tag() == Variable::Tag::REFERENCE,
           "function parameters preserve their value, resource, and reference categories");
    expect(callable->arguments()[0].type() == Type::of<float>() &&
               callable->arguments()[1].type() == Type::of<Buffer<float>>() &&
               callable->arguments()[2].type() == Type::of<int>(),
           "function parameter order and declared types are preserved");
    expect(kernel->is_kernel() && kernel->arguments().size() == 1u,
           "kernel records resource parameters without becoming a callable");
    expect(kernel_buffer_argument->variable().tag() == Variable::Tag::BUFFER &&
               thread_index->variable().tag() == Variable::Tag::THREAD_IDX,
           "kernel resource parameters and builtins retain their dedicated categories");
}

void test_type_parser_preserves_composite_metadata_and_rejects_zero_arrays() {
    using horizon::core::Buffer;
    using horizon::core::Type;
    using horizon::core::array;
    using horizon::core::tuple;

    const auto *array_type = Type::of<array<float, 3>>();
    const auto *buffer_type = Type::of<Buffer<int>>();
    const auto *tuple_type = Type::of<tuple<float, int>>();
    const auto *zero_length_array = Type::from("array<float,0>");

    expect(array_type->is_array() && array_type->dimension() == 3u && array_type->element() == Type::of<float>(),
           "array type retains its element type and positive dimension");
    expect(buffer_type->is_buffer() && buffer_type->is_resource() && buffer_type->element() == Type::of<int>(),
           "buffer type is recognized as a resource with its element type");
    expect(tuple_type->is_structure() && tuple_type->members().size() == 2u &&
               tuple_type->members()[0] == Type::of<float>() && tuple_type->members()[1] == Type::of<int>(),
           "tuple type exposes every parsed structure member in order");
    expect(!zero_length_array->is_valid(),
           "zero-length arrays are parsed but rejected by type validation");
}

}// namespace

int main() {
    test_layout_resolver_precision_and_composite_types();
    test_function_builds_a_context_consistent_statement_tree();
    test_expression_builders_preserve_operands_and_usage();
    test_control_flow_builders_keep_nested_scopes_isolated();
    test_function_signature_builders_preserve_parameter_categories();
    test_type_parser_preserves_composite_metadata_and_rejects_zero_arrays();
    return failures == 0 ? 0 : 1;
}
