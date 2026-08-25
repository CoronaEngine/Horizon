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

template<typename T>
concept can_sample_2d = requires(const T &texture) {
    texture.sample(4u, 0.25f, 0.75f);
};

template<typename T>
concept can_read_2d = requires(const T &texture) {
    texture.template read<float>(1, 2);
};

template<typename T>
concept can_write_2d = requires(T &texture) {
    texture.write(1.0f, 1, 2);
};

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
    using horizon::core::PrecisionPolicy;
    using horizon::core::StoragePrecisionPolicy;
    using horizon::dsl::BindlessArray;
    using horizon::dsl::RWTexture2DView;
    using horizon::dsl::RWTexture2DViewVar;
    using horizon::dsl::RWTexture3DView;
    using horizon::dsl::RWTexture3DViewVar;
    using horizon::dsl::Texture2DView;
    using horizon::dsl::Texture2DViewVar;
    using horizon::dsl::Texture3DView;
    using horizon::dsl::Texture3DViewVar;
    using horizon::dsl::Var;
    using horizon::math::float2;
    using horizon::math::float4;
    using horizon::math::real4;

    static_assert(std::is_same_v<BindlessArray, horizon::core::BindlessArray>);
    static_assert(std::is_same_v<Texture2DView<float4>, horizon::core::Texture2DView<float4>>);
    static_assert(std::is_same_v<Texture3DView<float2>, horizon::core::Texture3DView<float2>>);
    static_assert(std::is_same_v<RWTexture2DView<float4>, horizon::core::RWTexture2DView<float4>>);
    static_assert(std::is_same_v<RWTexture3DView<float2>, horizon::core::RWTexture3DView<float2>>);
    static_assert(std::is_same_v<Texture2DViewVar<float4>, Var<Texture2DView<float4>>>);
    static_assert(std::is_same_v<Texture3DViewVar<float2>, Var<Texture3DView<float2>>>);
    static_assert(std::is_same_v<RWTexture2DViewVar<float4>, Var<RWTexture2DView<float4>>>);
    static_assert(std::is_same_v<RWTexture3DViewVar<float2>, Var<RWTexture3DView<float2>>>);
    static_assert(std::is_same_v<horizon::dsl::Texture2D<float4>, Texture2DView<float4>>);
    static_assert(std::is_same_v<horizon::dsl::Texture3D<float2>, Texture3DView<float2>>);
    static_assert(std::is_same_v<horizon::dsl::Texture2DVar<float4>, Texture2DViewVar<float4>>);
    static_assert(std::is_same_v<horizon::dsl::Texture3DVar<float2>, Texture3DViewVar<float2>>);
    static_assert(horizon::dsl::is_texture_v<Texture2DView<float4>>);
    static_assert(horizon::dsl::is_texture_v<RWTexture2DView<float4>>);
    static_assert(horizon::dsl::is_texture2d_v<Texture2DView<float4>>);
    static_assert(horizon::dsl::is_texture2d_v<RWTexture2DView<float4>>);
    static_assert(horizon::dsl::is_texture3d_v<Texture3DView<float2>>);
    static_assert(horizon::dsl::is_texture3d_v<RWTexture3DView<float2>>);
    static_assert(can_sample_2d<Texture2DViewVar<float4>>);
    static_assert(!can_read_2d<Texture2DViewVar<float4>>);
    static_assert(!can_write_2d<Texture2DViewVar<float4>>);
    static_assert(!can_sample_2d<RWTexture2DViewVar<float4>>);
    static_assert(can_read_2d<RWTexture2DViewVar<float4>>);
    static_assert(can_write_2d<RWTexture2DViewVar<float4>>);

    const Type *bindless_array = Type::of<BindlessArray>();
    const Type *texture2d = Type::of<Texture2DView<float4>>();
    const Type *texture3d = Type::of<Texture3DView<float2>>();
    const Type *rw_texture2d = Type::of<RWTexture2DView<float4>>();
    const Type *rw_texture3d = Type::of<RWTexture3DView<float2>>();
    expect(bindless_array->is_bindless_array() && bindless_array->is_resource(),
           "BindlessArray is represented as a Core resource");
    expect(texture2d->tag() == Type::Tag::TEXTURE2D && texture2d->is_texture() &&
               texture2d->is_resource() && texture2d->element() == Type::of<float4>() &&
               texture2d->description() == "texture2d<vector<float,4>>",
           "Texture2D preserves its element type in the Core resource type");
    expect(texture3d->tag() == Type::Tag::TEXTURE3D && texture3d->is_texture() &&
               texture3d->is_resource() && texture3d->element() == Type::of<float2>() &&
               texture3d->description() == "texture3d<vector<float,2>>",
           "Texture3D preserves its element type in the Core resource type");
    expect(rw_texture2d->tag() == Type::Tag::RW_TEXTURE2D && rw_texture2d->is_texture() &&
               rw_texture2d->is_resource() && rw_texture2d->element() == Type::of<float4>() &&
               rw_texture2d->description() == "rwtexture2d<vector<float,4>>",
           "RWTexture2D preserves its element type in the Core resource type");
    expect(rw_texture3d->tag() == Type::Tag::RW_TEXTURE3D && rw_texture3d->is_texture() &&
               rw_texture3d->is_resource() && rw_texture3d->element() == Type::of<float2>() &&
               rw_texture3d->description() == "rwtexture3d<vector<float,2>>",
           "RWTexture3D preserves its element type in the Core resource type");
    expect(texture2d != rw_texture2d && texture2d->hash() != rw_texture2d->hash(),
           "sampled and read-write texture views have distinct Core type identities");
    expect(!texture2d->is_read_write_texture() && rw_texture2d->is_read_write_texture() &&
               rw_texture3d->is_read_write_texture(),
           "only RW texture views are classified as read-write textures");
    expect(Type::from(texture2d->description()) == texture2d &&
               Type::from(rw_texture2d->description()) == rw_texture2d,
           "sampled and read-write texture views round-trip through the Core registry");

    const StoragePrecisionPolicy f16{
        .policy = PrecisionPolicy::force_f16,
        .allow_real_in_storage = true};
    const StoragePrecisionPolicy f32{
        .policy = PrecisionPolicy::force_f32,
        .allow_real_in_storage = true};
    expect(Type::resolve(Type::of<Texture2DView<real4>>(), f16)->description() ==
               "texture2d<vector<half,4>>",
           "sampled texture views resolve dynamic element precision");
    expect(Type::resolve(Type::of<RWTexture3DView<real4>>(), f32)->description() ==
               "rwtexture3d<vector<float,4>>",
           "read-write texture views resolve dynamic element precision");
}

void test_direct_texture_operations_build_expected_ast() {
    using horizon::ast::CallExpr;
    using horizon::ast::CallOp;
    using horizon::ast::Usage;
    using horizon::ast::Variable;
    using horizon::core::Type;
    using horizon::dsl::Kernel;
    using horizon::dsl::RWTexture2DViewVar;
    using horizon::dsl::RWTexture3DViewVar;
    using horizon::dsl::Texture2DViewVar;
    using horizon::dsl::Texture3DViewVar;
    using horizon::math::float4;

    Kernel kernel{[](Texture2DViewVar<float4> texture2d,
                     Texture3DViewVar<float4> texture3d,
                     RWTexture2DViewVar<float4> rw_texture2d,
                     RWTexture3DViewVar<float4> rw_texture3d) {
        auto sampled2d = texture2d.sample(4u, 0.25f, 0.75f);
        auto sampled3d = texture3d.sample(4u, 0.25f, 0.5f, 0.75f);
        auto read2d = rw_texture2d.read<float>(1, 2);
        auto read3d = rw_texture3d.read<float>(1, 2, 3);
        rw_texture2d.write(1.0f, 1, 2);
        rw_texture3d.write(1.0f, 1, 2, 3);
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
    expect(function->arguments().size() == 4u,
           "direct texture kernel records sampled and read-write texture arguments");
    if (function->arguments().size() == 4u) {
        expect(function->arguments()[0].tag() == Variable::Tag::TEXTURE2D &&
                   function->arguments()[0].type() == Type::of<horizon::core::Texture2DView<float4>>(),
               "Texture2DView argument keeps its resource type and variable tag");
        expect(function->arguments()[1].tag() == Variable::Tag::TEXTURE3D &&
                   function->arguments()[1].type() == Type::of<horizon::core::Texture3DView<float4>>(),
               "Texture3DView argument keeps its resource type and variable tag");
        expect(function->arguments()[2].tag() == Variable::Tag::TEXTURE2D &&
                   function->arguments()[2].type() == Type::of<horizon::core::RWTexture2DView<float4>>(),
               "RWTexture2DView argument keeps its resource type and variable tag");
        expect(function->arguments()[3].tag() == Variable::Tag::TEXTURE3D &&
                   function->arguments()[3].type() == Type::of<horizon::core::RWTexture3DView<float4>>(),
               "RWTexture3DView argument keeps its resource type and variable tag");
        expect(function->arguments()[0].usage() == Usage::READ,
               "Texture2DView sample propagates read usage");
        expect(function->arguments()[1].usage() == Usage::READ,
               "Texture3DView sample propagates read usage");
        expect(function->arguments()[2].usage() == Usage::READ_WRITE,
               "RWTexture2DView read and write propagate read-write usage");
        expect(function->arguments()[3].usage() == Usage::READ_WRITE,
               "RWTexture3DView read and write propagate read-write usage");
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
