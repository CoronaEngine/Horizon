#include "dsl/dsl.h"

#include <iostream>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<const horizon::ast::CallExpr *> top_level_calls(
    const horizon::ast::Function &function) {
    using horizon::ast::AssignStmt;
    using horizon::ast::CallExpr;
    using horizon::ast::ExprStmt;
    using horizon::ast::Expression;
    using horizon::ast::Statement;

    std::vector<const CallExpr *> calls;
    for (const Statement *statement : function.body()->statements()) {
        const Expression *expression = nullptr;
        if (statement->tag() == Statement::Tag::ASSIGN) {
            expression = static_cast<const AssignStmt *>(statement)->rhs();
        } else if (statement->tag() == Statement::Tag::EXPR) {
            expression = static_cast<const ExprStmt *>(statement)->expression();
        }
        if (expression != nullptr && expression->tag() == Expression::Tag::CALL) {
            calls.push_back(static_cast<const CallExpr *>(expression));
        }
    }
    return calls;
}

void expect_call(const horizon::ast::CallExpr *call,
                 horizon::ast::CallOp expected_op,
                 std::size_t expected_argument_count,
                 const char *message) {
    expect(call != nullptr, message);
    if (call != nullptr) {
        expect(call->call_op() == expected_op, message);
        expect(call->arguments().size() == expected_argument_count, message);
    }
}

void test_texture_types_are_core_resources() {
    using horizon::core::Type;
    using horizon::dsl::BindlessArray;
    using horizon::dsl::Texture2D;
    using horizon::dsl::Texture3D;

    static_assert(std::is_same_v<BindlessArray, horizon::core::BindlessArray>);
    static_assert(std::is_same_v<Texture2D, horizon::core::Texture2D>);
    static_assert(std::is_same_v<Texture3D, horizon::core::Texture3D>);
    static_assert(horizon::dsl::is_texture_v<Texture2D>);
    static_assert(horizon::dsl::is_texture2d_v<Texture2D>);
    static_assert(horizon::dsl::is_texture3d_v<Texture3D>);

    const Type *bindless_array = Type::of<BindlessArray>();
    const Type *texture2d = Type::of<Texture2D>();
    const Type *texture3d = Type::of<Texture3D>();
    expect(bindless_array->is_bindless_array() && bindless_array->is_resource(),
           "BindlessArray is represented as a Core resource");
    expect(texture2d->tag() == Type::Tag::TEXTURE2D && texture2d->is_texture() &&
               texture2d->is_resource(),
           "Texture2D is represented as a Core texture resource");
    expect(texture3d->tag() == Type::Tag::TEXTURE3D && texture3d->is_texture() &&
               texture3d->is_resource(),
           "Texture3D is represented as a Core texture resource");
}

void test_direct_texture_operations_build_expected_ast() {
    using horizon::ast::CallExpr;
    using horizon::ast::CallOp;
    using horizon::ast::Usage;
    using horizon::ast::Variable;
    using horizon::core::Type;
    using horizon::dsl::Kernel;
    using horizon::dsl::Texture2DVar;
    using horizon::dsl::Texture3DVar;

    Kernel kernel{[](Texture2DVar texture2d, Texture3DVar texture3d) {
        auto sampled2d = texture2d.sample(4u, 0.25f, 0.75f);
        auto sampled3d = texture3d.sample(4u, 0.25f, 0.5f, 0.75f);
        auto read2d = texture2d.read<float>(1, 2);
        auto read3d = texture3d.read<float>(1, 2, 3);
        texture2d.write(1.0f, 1, 2);
        texture3d.write(1.0f, 1, 2, 3);
        (void)sampled2d;
        (void)sampled3d;
        (void)read2d;
        (void)read3d;
    }};

    const auto &function = kernel.function();
    expect(function != nullptr && function->body()->check_context(function.get()),
           "direct texture operations stay in their kernel context");
    if (function == nullptr) {
        return;
    }
    expect(function->arguments().size() == 2u,
           "direct texture kernel records both texture arguments");
    if (function->arguments().size() == 2u) {
        expect(function->arguments()[0].tag() == Variable::Tag::TEXTURE2D &&
                   function->arguments()[0].type() == Type::of<horizon::core::Texture2D>(),
               "Texture2D argument keeps its resource type and variable tag");
        expect(function->arguments()[1].tag() == Variable::Tag::TEXTURE3D &&
                   function->arguments()[1].type() == Type::of<horizon::core::Texture3D>(),
               "Texture3D argument keeps its resource type and variable tag");
        expect(function->arguments()[0].usage() == Usage::READ_WRITE,
               "Texture2D sample, read, and write propagate read-write usage");
        expect(function->arguments()[1].usage() == Usage::READ_WRITE,
               "Texture3D sample, read, and write propagate read-write usage");
    }

    const std::vector<const CallExpr *> calls = top_level_calls(*function);
    expect(calls.size() == 6u,
           "direct texture operations emit six top-level builtin calls");
    if (calls.size() != 6u) {
        return;
    }

    expect_call(calls[0], CallOp::TEX2D_SAMPLE, 3u,
                "Texture2D sample emits TEX2D_SAMPLE with texture and two coordinates");
    expect_call(calls[1], CallOp::TEX3D_SAMPLE, 4u,
                "Texture3D sample emits TEX3D_SAMPLE with texture and three coordinates");
    expect_call(calls[2], CallOp::TEX2D_READ, 3u,
                "Texture2D read emits TEX2D_READ with integer coordinates");
    expect_call(calls[3], CallOp::TEX3D_READ, 4u,
                "Texture3D read emits TEX3D_READ with integer coordinates");
    expect_call(calls[4], CallOp::TEX2D_WRITE, 4u,
                "Texture2D write emits TEX2D_WRITE with value and integer coordinates");
    expect_call(calls[5], CallOp::TEX3D_WRITE, 5u,
                "Texture3D write emits TEX3D_WRITE with value and integer coordinates");

    expect(calls[0]->template_args().size() == 1u && calls[1]->template_args().size() == 1u,
           "direct texture samples each record one template argument");
    expect(calls[2]->template_args().size() == 1u && calls[3]->template_args().size() == 1u,
           "direct texture reads each record one template argument");
    if (calls[0]->template_args().size() != 1u || calls[1]->template_args().size() != 1u ||
        calls[2]->template_args().size() != 1u || calls[3]->template_args().size() != 1u) {
        return;
    }

    const auto *sample2d_channels = std::get_if<horizon::core::uint>(&calls[0]->template_arg(0));
    const auto *sample3d_channels = std::get_if<horizon::core::uint>(&calls[1]->template_arg(0));
    expect(sample2d_channels != nullptr && *sample2d_channels == 4u,
           "Texture2D sample records its channel count");
    expect(sample3d_channels != nullptr && *sample3d_channels == 4u,
           "Texture3D sample records its channel count");

    const auto *read2d_type =
        std::get_if<const Type *>(&calls[2]->template_arg(0));
    const auto *read3d_type =
        std::get_if<const Type *>(&calls[3]->template_arg(0));
    expect(read2d_type != nullptr && *read2d_type == Type::of<float>(),
           "Texture2D read records its output type");
    expect(read3d_type != nullptr && *read3d_type == Type::of<float>(),
           "Texture3D read records its output type");
}

void test_bindless_texture_samples_build_expected_ast() {
    using horizon::ast::CallExpr;
    using horizon::ast::CallOp;
    using horizon::ast::Usage;
    using horizon::ast::Variable;
    using horizon::core::Type;
    using horizon::dsl::BindlessArrayVar;
    using horizon::dsl::Kernel;

    Kernel kernel{[](BindlessArrayVar bindless_array) {
        auto sampled2d = bindless_array.tex2d_var(2u).sample(4u, 0.25f, 0.75f);
        auto sampled3d = bindless_array.tex3d_var(3u).sample(4u, 0.25f, 0.5f, 0.75f);
        (void)sampled2d;
        (void)sampled3d;
    }};

    const auto &function = kernel.function();
    expect(function != nullptr && function->body()->check_context(function.get()),
           "bindless texture samples stay in their kernel context");
    if (function == nullptr) {
        return;
    }
    expect(function->arguments().size() == 1u,
           "bindless texture kernel records one resource argument");
    if (function->arguments().size() == 1u) {
        expect(function->arguments()[0].tag() == Variable::Tag::BINDLESS_ARRAY &&
                   function->arguments()[0].type() == Type::of<horizon::core::BindlessArray>() &&
                   function->arguments()[0].type()->is_resource(),
           "bindless texture kernel records a bindless-array resource argument");
        expect(function->arguments()[0].usage() == Usage::READ,
               "bindless texture samples propagate read usage to the bindless array");
    }

    const std::vector<const CallExpr *> calls = top_level_calls(*function);
    expect(calls.size() == 2u,
           "bindless texture samples emit two top-level builtin calls");
    if (calls.size() != 2u) {
        return;
    }
    expect_call(calls[0], CallOp::BINDLESS_ARRAY_TEX2D_SAMPLE, 4u,
                "bindless Texture2D sample records array, index, and two coordinates");
    expect_call(calls[1], CallOp::BINDLESS_ARRAY_TEX3D_SAMPLE, 5u,
                "bindless Texture3D sample records array, index, and three coordinates");
}

}// namespace

int main() {
    test_texture_types_are_core_resources();
    test_direct_texture_operations_build_expected_ast();
    test_bindless_texture_samples_build_expected_ast();
    return failures == 0 ? 0 : 1;
}
