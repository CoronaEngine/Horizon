#include "dsl/api/raster.h"

#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace raster_dsl_test
{
    struct Varyings
    {
        horizon::math::float3 color;
    };
    struct OtherVaryings
    {
        horizon::math::float3 color;
    };
    struct Inner
    {
        horizon::math::float2 uv;
        unsigned int id;
    };
    struct Nested
    {
        horizon::math::float3 color;
        Inner inner;
    };
}
OC_STRUCT(raster_dsl_test, Varyings, color) {};
OC_STRUCT(raster_dsl_test, OtherVaryings, color) {};
OC_STRUCT(raster_dsl_test, Inner, uv, id) {};
OC_STRUCT(raster_dsl_test, Nested, color, inner) {};

namespace
{
    using namespace horizon::dsl;
    using horizon::ast::RasterDiagnosticCode;
    using horizon::ast::RasterValidationError;
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    template <typename Func>
    void expect_rejection(RasterDiagnosticCode code, Func&& func, const char* message)
    {
        bool rejected = false;
        try
        {
            func();
        }
        catch (const RasterValidationError& error)
        {
            for (const auto& diagnostic : error.diagnostics())
            {
                rejected |= diagnostic.code == code;
            }
        }
        expect(rejected, message);
        expect(Function::current() == nullptr, "failed shader restores current function");
    }

    static_assert(std::is_same_v<decltype(vertex_position()), Float4>);
    static_assert(std::is_same_v<decltype(vertex_index()), Uint>);
    static_assert(std::is_same_v<decltype(instance_index()), Uint>);
    static_assert(std::is_same_v<decltype(draw_index()), Uint>);
    static_assert(std::is_same_v<decltype(fragment_coord()), Float4>);
    static_assert(std::is_same_v<decltype(front_facing()), Bool>);
    static_assert(!std::is_default_constructible_v<VertexShader<void()>>);
    static_assert(!std::is_default_constructible_v<FragmentShader<void()>>);
    static_assert(!std::is_invocable_v<VertexShader<void()>>);
    static_assert(!std::is_constructible_v<FragmentShader<void(float*)>, decltype([](auto) {})>);
    static_assert(!std::is_constructible_v<FragmentShader<void(float&)>, decltype([](auto&&) {})>);
    static_assert(!std::is_constructible_v<FragmentShader<void(float)>, decltype([](Float&) {})>);
    static_assert(!std::is_constructible_v<FragmentShader<float()>, decltype([] { return 1.0f; })>);
    static_assert(!std::is_constructible_v<FragmentShader<int()>, decltype([] { return Float { 1.0f }; })>);
    static_assert(!std::is_constructible_v<FragmentShader<void()>, decltype([] { return Float { 1.0f }; })>);

    using VS = VertexShader<float3(float3)>;
    using FS = FragmentShader<float4(float3)>;
    static_assert(is_raster_pair_compatible_v<VS, FS>);
    static_assert(is_raster_pair_compatible_v<const VS&, FS&&>);
    static_assert(!is_raster_pair_compatible_v<VS, FragmentShader<float4(float2)>>);
    static_assert(!is_raster_pair_compatible_v<FS, VS>);
    static_assert(!is_raster_pair_compatible_v<VS, FragmentShader<float4()>>);
    static_assert(!is_raster_pair_compatible_v<VS, FragmentShader<float4(float3, float3)>>);
    static_assert(!is_raster_pair_compatible_v<VertexShader<void()>, FS>);
    static_assert(is_raster_pair_compatible_v<VertexShader<void()>, FragmentShader<void()>>);
    static_assert(is_raster_pair_compatible_v<VS, FragmentShader<void(float3)>>);
    static_assert(!is_raster_pair_compatible_v<VertexShader<raster_dsl_test::Varyings()>,
                                               FragmentShader<void(raster_dsl_test::OtherVaryings)>>);
    static_assert(!std::is_constructible_v<RasterShader<VS, FragmentShader<float4(float2)>>, VS, FragmentShader<float4(float2)>>);

    std::string interface_snapshot(const Function& function)
    {
        std::string result;
        auto append = [&](const auto& slots) {
            result += "[";
            for (const auto& slot : slots)
            {
                result += std::string(slot.type->description()) + ":" + std::to_string(slot.root_index) + ":" +
                          std::to_string(slot.location) + ":" + std::to_string(static_cast<int>(slot.interpolation));
                for (auto member : slot.member_path)
                {
                    result += "." + std::to_string(member);
                }
                result += ";";
            }
            result += "]";
        };
        append(function.shader_interface().inputs);
        append(function.shader_interface().outputs);
        return result;
    }

    void test_pairing_and_ownership()
    {
        int constructions = 0;
        auto make_vertex = [&] {
            return VertexShader { [&](Float3 pos) {
                ++constructions;
                vertex_position() = make_float4(pos, 1.0f);
                return pos;
            } };
        };
        FS fs { [](Float3 color) { auto facing = front_facing(); return make_float4(color, 1.0f); } };
        auto vs = make_vertex();
        const auto f = fs.function();
        static_assert(std::is_const_v<std::remove_reference_t<decltype(*f)>>);
        auto before = interface_snapshot(*f);
        auto hash = f->hash();
        auto builtins = f->builtin_vars().size();
        auto vertex_before = interface_snapshot(*vs.function());
        auto vertex_hash = vs.function()->hash();
        auto vertex_builtins = vs.function()->builtin_vars().size();
        RasterShader first { vs, fs };
        RasterShader second { make_vertex(), fs };
        static_assert(std::is_same_v<decltype(first.vertex()), const VS&>);
        static_assert(std::is_same_v<decltype(first.fragment()), const FS&>);
        expect(constructions == 2 && first.fragment().function() == second.fragment().function(),
               "pairing shares stages without invoking shader lambdas again");
        expect(interface_snapshot(*f) == before && f->hash() == hash && f->builtin_vars().size() == builtins,
               "repeated pairing preserves fragment interface, hash and builtin list");
        expect(interface_snapshot(*vs.function()) == vertex_before && vs.function()->hash() == vertex_hash &&
                   vs.function()->builtin_vars().size() == vertex_builtins,
               "pairing preserves vertex interface, hash and builtin list");
        auto temporary = [&] { return RasterShader { make_vertex(), FragmentShader { [](Float3) {} } }; }();
        expect(temporary.vertex().function()->is_vertex() && temporary.fragment().function()->is_fragment(),
               "pair owns both functions after temporary stages and lambdas are destroyed");
        RasterShader empty { VertexShader { [] { vertex_position() = make_float4(1.0f); } }, FragmentShader { [] {} } };
        expect(empty.fragment().function()->shader_interface().inputs.empty(), "void pair has no varying");
    }

    void test_resolved_precision_pair()
    {
        struct RestorePolicy
        {
            StoragePrecisionPolicy saved = global_storage_policy();
            ~RestorePolicy() { set_global_storage_policy(saved); }
        } restore;
        set_global_storage_policy({ PrecisionPolicy::ForceF32, true });
        VertexShader<real3(real3)> vs { [](Var<real3> pos) {
            vertex_position() = make_float4(1.0f);
            return pos;
        } };
        FragmentShader<void(real3)> fs { [](Var<real3>) {} };
        RasterShader valid { vs, fs };
        expect(valid.fragment().function()->shader_interface().inputs.front().type == Type::of<float3>(),
               "same logical real signature pairs using resolved float32 metadata");
        set_global_storage_policy({ PrecisionPolicy::ForceF16, true });
        expect_rejection(RasterDiagnosticCode::InvalidInterfaceType, [] { FragmentShader<void(real3)> fs { [](Var<real3>) {} }; }, "same C++ signature under unsupported precision is rejected before pairing");
    }

    void test_stage_signatures_and_layout()
    {
        int constructions = 0;
        VertexShader vs { [&](Float3 pos, const Float3& color) {
            ++constructions;
            vertex_position() = make_float4(pos, 1.0f);
            Var<raster_dsl_test::Varyings> out;
            out.color = color;
            return out;
        } };
        FragmentShader fs { [](Var<raster_dsl_test::Varyings> in) { return make_float4(in.color, 1.0f); } };
        static_assert(std::is_same_v<typename decltype(vs)::signature, raster_dsl_test::Varyings(float3, float3)>);
        static_assert(std::is_same_v<typename decltype(fs)::return_type, float4>);
        static_assert(std::is_same_v<decltype(vs.function()), shared_ptr<const Function>>);
        expect(vs.function()->is_vertex() && fs.function()->is_fragment(), "deduced stages have correct entry tags");
        expect(vs.function()->arguments().size() == 2 && vs.function()->shader_interface().inputs.size() == 2,
               "lambda attributes become ordered stage inputs");
        auto copied = vs;
        RasterShader sample { vs, fs };
        const auto vertex_function = sample.vertex().function();
        const auto fragment_function = sample.fragment().function();
        expect(vertex_function == vs.function() && fragment_function == fs.function(), "documented structured sample pairs");
        expect(constructions == 1 && copied.function() == vs.function(), "stage copies share a single constructed AST");

        VertexShader<float3(float3)> generic { [](auto pos) {
            vertex_position() = make_float4(pos, 1.0f);
            return pos;
        } };
        expect(generic.function()->return_type() == Type::of<float3>(), "explicit signature supports generic lambda");
        VertexShader<void()> no_attributes { [] { vertex_position() = make_float4(0.0f, 0.0f, 0.0f, 1.0f); } };
        FragmentShader<void()> no_color { [] { discard(); } };
        expect(no_attributes.function()->shader_interface().inputs.empty() && no_color.function()->shader_interface().outputs.empty(),
               "void stages have no ordinary input or output slots");
        VertexShader procedural { [] {
            Uint id = vertex_index();
            vertex_position() = make_float4(cast<float>(id), 0.0f, 0.0f, 1.0f);
            return Float3 { 1.0f };
        } };
        expect(procedural.function()->shader_interface().outputs.size() == 1, "attribute-free vertex shader can return a varying");
        FragmentShader mrt { [](Var<raster_dsl_test::Nested> in) { return in; } };
        expect(mrt.function()->shader_interface().inputs.size() == 3 && mrt.function()->shader_interface().outputs.size() == 3,
               "nested varying and MRT leaves are preserved");
    }

    void test_builtin_aliases_and_local_copies()
    {
        FragmentShader valid { [](Float4 input) {
            Float4 coord = fragment_coord();
            Float4 local = coord;
            local.x = 0.0f;
            auto* coord_ref = static_cast<const RefExpr*>(coord.expression());
            auto* local_ref = static_cast<const RefExpr*>(local.expression());
            expect(coord_ref->variable().tag() == Variable::Tag::FragmentCoord && local_ref->variable().tag() == Variable::Tag::Local,
                   "getter aliases builtin while lvalue copy materializes a local");
            Float4 input_copy = input;
            input_copy.x = 0.0f;
            return local;
        } };
        for (int operation = 0; operation < 5; ++operation)
        {
            expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [&] { FragmentShader rejected { [&] {
                                                                            Float4 coord = fragment_coord();
                                                                            if (operation == 0)
                                                                            {
                                                                                coord = make_float4(0.0f);
                                                                            }
                                                                            if (operation == 1)
                                                                            {
                                                                                coord.set(make_float4(0.0f));
                                                                            }
                                                                            if (operation == 2)
                                                                            {
                                                                                coord.x = 0.0f;
                                                                            }
                                                                            if (operation == 3)
                                                                            {
                                                                                auto alias = std::move(coord);
                                                                                alias.y = 0.0f;
                                                                            }
                                                                            if (operation == 4)
                                                                            {
                                                                                auto alias = fragment_coord();
                                                                                alias.z = 0.0f;
                                                                            }
                                                                        } }; }, "builtin write, set, member, move and auto aliases stay readonly");
        }
        expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [] { FragmentShader rejected { [](Float3 input) { input.x = 0.0f; } }; }, "lambda stage input is readonly");
        expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [] { FragmentShader rejected { [] {
                                                                       Callable writer { [](Float4& value) { value.x = 0.0f; } };
                                                                       auto coord = fragment_coord();
                                                                       writer(coord);
                                                                   } }; }, "callable reference cannot write builtin alias");
    }

    void test_failure_recovery_and_no_current()
    {
        bool threw = false;
        try
        {
            FragmentShader fs { [] { throw std::runtime_error("shader probe"); } };
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        expect(threw && Function::current() == nullptr, "user lambda exception leaves construction state clean");
        FragmentShader next { [] { return Float4 { 1.0f }; } };
        expect(next.function()->body()->check_context(next.function().get()), "subsequent shader remains valid");
        expect_rejection(RasterDiagnosticCode::InvalidBuiltinStage, [] { FragmentShader wrong { [] { (void)vertex_index(); } }; }, "builtin checks actual entry stage");
        for (int api = 0; api < 7; ++api)
        {
            bool no_current = false;
            try
            {
                switch (api)
                {
                case 0:
                    (void)vertex_position();
                    break;
                case 1:
                    (void)vertex_index();
                    break;
                case 2:
                    (void)instance_index();
                    break;
                case 3:
                    (void)draw_index();
                    break;
                case 4:
                    (void)fragment_coord();
                    break;
                case 5:
                    (void)front_facing();
                    break;
                case 6:
                    discard();
                    break;
                }
            }
            catch (const std::logic_error&)
            {
                no_current = true;
            }
            expect(no_current, "raster builtin requires current function");
        }
    }
    void test_control_flow_exception_recovery()
    {
        auto previous = std::set_terminate([] { std::cerr << "unexpected terminate in raster control flow\n"; std::_Exit(42); });
        for (int operation = 0; operation < 12; ++operation)
        {
            bool caught = false;
            try
            {
                FragmentShader fs { [&] {
                    auto fail = [] { throw std::runtime_error("branch"); };
                    switch (operation)
                    {
                    case 0:
                        if_(Bool { true }, fail);
                        break;
                    case 1:
                        if_(Bool { true }, [] {}).else_(fail);
                        break;
                    case 2:
                        if_(Bool { true }, [] {}).elif_(Bool { false }, fail);
                        break;
                    case 3:
                        loop([&](Continue, Break) { fail(); });
                        break;
                    case 4:
                        for_range(3, [&](Int, Continue, Break) { fail(); });
                        break;
                    case 5:
                        switch_(Int { 1 }).case_(1, [&](Break) { fail(); });
                        break;
                    case 6:
                        switch_(Int { 1 }).default_([&](Break) { fail(); });
                        break;
                    case 7:
                        while_(Bool { true }, fail);
                        break;
                    case 8:
                        $if(Bool { true })
                        {
                            fail();
                        };
                        break;
                    case 9:
                        $for(i, 3)
                        {
                            fail();
                        };
                        break;
                    case 10:
                        $scope
                        {
                            fail();
                        };
                        break;
                    case 11:
                        $switch(Int { 1 })
                        {
                            $case(1)
                            {
                                fail();
                            };
                        };
                        break;
                    }
                } };
            }
            catch (const std::runtime_error&)
            {
                caught = true;
            }
            expect(caught && Function::current() == nullptr, "control-flow callback exception restores construction stacks");
            FragmentShader next { [] { return Float4 { 1.0f }; } };
            expect(next.function()->body()->check_context(next.function().get()), "construction after control-flow exception succeeds");
        }
        std::set_terminate(previous);
    }
} // namespace

int main()
{
    test_stage_signatures_and_layout();
    test_builtin_aliases_and_local_copies();
    test_failure_recovery_and_no_current();
    test_pairing_and_ownership();
    test_resolved_precision_pair();
    test_control_flow_exception_recovery();
    return failures == 0 ? 0 : 1;
}
