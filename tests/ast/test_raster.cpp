#include "ast/function.h"
#include "ast/raster_validation.h"
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif
#include "ast/type_desc.h"

#include <iostream>
#include <stdexcept>

namespace raster_layout_test
{
    struct Inner
    {
        horizon::math::float2 uv;
        unsigned int id;
    };
    struct Outer
    {
        horizon::math::float3 color;
        Inner inner;
    };
}
OC_MAKE_STRUCT_DESC(raster_layout_test::Inner, uv, id)
OC_MAKE_STRUCT_DESC(raster_layout_test::Outer, color, inner)

namespace
{
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    void test_stage_construction_and_exception_recovery()
    {
        using namespace horizon::ast;
        auto fs = Function::define_raster(Function::Tag::Fragment, nullptr, [] {});
        expect(fs->is_fragment() && fs->is_raster() && fs->is_entry_point() && !fs->is_kernel(),
               "fragment is a raster entry, not a compute kernel");
        expect(!fs->is_general_kernel() && !fs->is_vertex(), "fragment classification");
        expect(fs->body()->check_context(fs.get()), "fragment body belongs to its function");
        expect(Function::current() == nullptr, "successful construction restores current function");

        bool threw = false;
        try
        {
            Function::define_raster(Function::Tag::Fragment, nullptr,
                                    [] { throw std::runtime_error("construction probe"); });
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        expect(threw && Function::current() == nullptr, "user exception unwinds function stack");

        auto outer = Function::define_raster(Function::Tag::Fragment, nullptr, [] {
            auto* f = Function::current();
            auto* scope = f->scope();
            f->with(scope, [&] {
                try
                {
                    Function::define_raster(Function::Tag::Fragment, nullptr,
                                            [] { throw std::runtime_error("nested probe"); });
                }
                catch (const std::runtime_error&)
                {
                }
                expect(Function::current() == f, "nested failure restores outer function");
                expect(f->current_scope() == scope, "nested failure preserves outer scope");
            });
            try
            {
                f->with(scope, [] { throw std::runtime_error("scope probe"); });
            }
            catch (const std::runtime_error&)
            {
            }
            expect(f->current_scope() == f->body(), "throwing scope restores scope stack");
        });
        expect(outer->body()->check_context(outer.get()), "recovered outer function remains valid");
        auto next = Function::define_kernel([] {});
        auto callable = Function::define_callable([] {});
        expect(next->is_kernel() && next->is_entry_point() && next->is_general_kernel(),
               "existing compute classification is preserved");
        expect(callable->is_callable() && !callable->is_entry_point(), "callable is not an entry");
        expect(next->body()->check_context(next.get()), "kernel after failed raster is valid");
    }

    void test_invalid_raster_tag_is_diagnosed()
    {
        using namespace horizon::ast;
        for (auto tag : { Function::Tag::Kernel, Function::Tag::Callable })
        {
            bool rejected = false;
            try
            {
                Function::define_raster(tag, nullptr, [] {});
            }
            catch (const RasterValidationError& error)
            {
                rejected = !error.diagnostics().empty() &&
                           error.diagnostics()[0].code == RasterDiagnosticCode::InvalidStage;
            }
            expect(rejected, "raster factory rejects non-raster tag with InvalidStage");
            expect(Function::current() == nullptr, "invalid tag leaves function stack intact");
        }
    }

    template <typename Func>
    void expect_rejection(horizon::ast::RasterDiagnosticCode code, Func&& func, const char* message)
    {
        bool rejected = false;
        try
        {
            func();
        }
        catch (const horizon::ast::RasterValidationError& error)
        {
            for (const auto& diagnostic : error.diagnostics())
            {
                rejected |= diagnostic.code == code;
            }
        }
        expect(rejected, message);
        expect(horizon::ast::Function::current() == nullptr, "validation failure restores current function");
    }

    void test_recursive_interface_layout_and_hash()
    {
        using namespace horizon::ast;
        using horizon::core::Type;
        using horizon::math::float3;
        using horizon::math::float4;
        auto build = [](const Type* input) {
            return Function::define_raster(Function::Tag::Fragment, Type::of<float4>(), [&] {
                auto* f = Function::current();
                (void)f->argument(input);
                f->return_(f->local(Type::of<float4>()));
            });
        };
        auto fs = build(Type::of<float3>());
        const auto& io = fs->shader_interface();
        expect(io.inputs.size() == 1 && io.outputs.size() == 1, "float interface has input and color output");
        if (io.inputs.size() == 1 && io.outputs.size() == 1)
        {
            expect(io.inputs[0].location == 0 && io.inputs[0].member_path.empty() &&
                       io.inputs[0].interpolation == Interpolation::Smooth,
                   "floating varying uses smooth location zero");
            expect(io.outputs[0].location == 0 && io.outputs[0].interpolation == Interpolation::None,
                   "fragment color output has no interpolation");
        }
        auto nested = Function::define_raster(Function::Tag::Fragment, Type::of<raster_layout_test::Outer>(), [] {
            auto* f = Function::current();
            (void)f->argument(Type::of<raster_layout_test::Outer>());
            f->return_(f->local(Type::of<raster_layout_test::Outer>()));
        });
        const auto& layout = nested->shader_interface();
        expect(layout.inputs.size() == 3 && layout.outputs.size() == 3, "nested struct flattens into three leaves and MRT colors");
        if (layout.inputs.size() == 3 && layout.outputs.size() == 3)
        {
            const horizon::core::vector<horizon::core::vector<uint32_t>> paths { { 0 }, { 1, 0 }, { 1, 1 } };
            for (uint32_t i = 0; i < 3; ++i)
            {
                expect(layout.inputs[i].location == i && layout.inputs[i].root_index == 0 &&
                           layout.inputs[i].member_path == paths[i],
                       "nested input keeps full member path and location");
                expect(layout.inputs[i].interpolation == (i == 2 ? Interpolation::Flat : Interpolation::Smooth),
                       "integer varying is flat while float varying is smooth");
                expect(layout.outputs[i].member_path == paths[i] && layout.outputs[i].interpolation == Interpolation::None,
                       "nested MRT output preserves path without interpolation");
            }
        }
        expect(fs->hash() == build(Type::of<float3>())->hash(), "equivalent stage interfaces have stable hashes");
        expect(fs->hash() != build(Type::of<float4>())->hash(), "input dimension affects stage hash");
        expect(build(Type::of<int>())->hash() != build(Type::of<unsigned int>())->hash(), "integer signedness affects hash");
        auto multiple = Function::define_raster(Function::Tag::Fragment, nullptr, [] {
            auto* f = Function::current();
            (void)f->argument(Type::of<float>());
            (void)f->argument(Type::of<raster_layout_test::Inner>());
        });
        expect(multiple->arguments().size() == 2 && multiple->arguments()[1].tag() == Variable::Tag::StageInput,
               "raster preserves structure parameters as stage inputs without kernel splitting");
        const auto& inputs = multiple->shader_interface().inputs;
        expect(inputs.size() == 3, "multiple parameters share consecutive input locations");
        if (inputs.size() == 3)
        {
            expect(inputs[1].root_index == 1 && inputs[1].location == 1 &&
                       inputs[2].root_index == 1 && inputs[2].location == 2,
                   "flattened fields keep the original parameter index");
        }
    }

    void test_interface_rejections_and_declared_returns()
    {
        using namespace horizon::ast;
        using horizon::core::Type;
        const Type* invalid[] = { Type::of<bool>(), Type::of<horizon::math::half>(),
                                  Type::of<horizon::core::array<float, 2>>(), Type::of<horizon::math::float4x4>(),
                                  Type::of<horizon::core::Buffer<float>>(), Type::from("struct<RasterEmpty,1,false,false>"),
                                  Type::of<uint64_t>() };
        for (const auto* type : invalid)
        {
            expect_rejection(RasterDiagnosticCode::InvalidInterfaceType, [&] { Function::define_raster(Function::Tag::Fragment, nullptr, [&] {
                                                                                   (void)Function::current()->argument(type);
                                                                               }); }, "unsupported input type produces InvalidInterfaceType");
        }
        expect_rejection(RasterDiagnosticCode::InvalidReturn, [] { Function::define_raster(Function::Tag::Fragment, Type::of<float>(), [] {
                                                                       auto* f = Function::current();
                                                                       f->return_(f->local(Type::of<int>()));
                                                                       f->return_(f->local(Type::of<float>()));
                                                                   }); }, "earlier mismatched return cannot be hidden by a later matching return");
        expect_rejection(RasterDiagnosticCode::InvalidReturn, [] { Function::define_raster(Function::Tag::Fragment, Type::of<float>(), [] {
                                                                       Function::current()->return_(nullptr);
                                                                   }); }, "void return is rejected in value-returning stage");
        expect_rejection(RasterDiagnosticCode::InvalidReturn, [] { Function::define_raster(Function::Tag::Fragment, nullptr, [] {
                                                                       auto* f = Function::current();
                                                                       f->return_(f->local(Type::of<float>()));
                                                                   }); }, "value return is rejected in void stage");
    }

    void test_interface_precision_policy()
    {
        using namespace horizon::ast;
        using namespace horizon::core;
        struct RestorePolicy
        {
            StoragePrecisionPolicy saved = global_storage_policy();
            ~RestorePolicy() { set_global_storage_policy(saved); }
        } restore;
        set_global_storage_policy({ PrecisionPolicy::ForceF32, true });
        auto fs = Function::define_raster(Function::Tag::Fragment, nullptr, [] {
            (void)Function::current()->argument(Type::of<horizon::math::real3>());
        });
        expect(fs->shader_interface().inputs.size() == 1, "real resolved to float32 is an interface leaf");
        if (!fs->shader_interface().inputs.empty())
        {
            expect(fs->shader_interface().inputs[0].type == Type::of<horizon::math::float3>(),
                   "interface stores resolved float32 type");
        }
        set_global_storage_policy({ PrecisionPolicy::ForceF16, true });
        expect_rejection(RasterDiagnosticCode::InvalidInterfaceType, [] { Function::define_raster(Function::Tag::Fragment, nullptr, [] {
                                                                              (void)Function::current()->argument(Type::of<horizon::math::real3>());
                                                                          }); }, "real resolved to half is rejected");
        auto explicit_float = Function::define_raster(Function::Tag::Fragment, nullptr, [] {
            (void)Function::current()->argument(Type::of<horizon::math::float3>());
        });
        expect(explicit_float->shader_interface().inputs.size() == 1 &&
                   explicit_float->shader_interface().inputs[0].type == Type::of<horizon::math::float3>(),
               "half storage policy does not change explicit float interface");
    }

    void write_position()
    {
        auto* f = horizon::ast::Function::current();
        f->assign(f->vertex_position(), f->literal(horizon::core::Type::of<horizon::math::float4>(),
                                                   horizon::math::make_float4(0.0f, 0.0f, 0.0f, 1.0f)));
    }

    void test_raster_builtins_and_stage_rules()
    {
        using namespace horizon::ast;
        using horizon::core::Type;
        auto fs = Function::define_raster(Function::Tag::Fragment, nullptr, [] {
            auto* f = Function::current();
            expect(f->fragment_coord() == f->fragment_coord(), "builtin expression reused within one function");
            f->discard();
        });
        expect(fs->builtin_vars().size() == 1, "builtin variable is deduplicated");
        expect(fs->body()->statements().back()->tag() == Statement::Tag::Discard, "discard has a dedicated AST statement");
        expect(fs->body()->check_context(fs.get()), "discard retains its function context");
        auto vs = Function::define_raster(Function::Tag::Vertex, nullptr, [] {
            auto* f = Function::current();
            write_position();
            expect(f->vertex_index() == f->vertex_index(), "vertex index deduplicated");
            (void)f->instance_index();
            (void)f->draw_index();
        });
        expect(vs->is_vertex() && vs->builtin_vars().size() == 4 && vs->shader_interface().outputs.empty(),
               "vertex builtins do not occupy ordinary varying locations");
        expect_rejection(RasterDiagnosticCode::MissingVertexPosition, [] { Function::define_raster(Function::Tag::Vertex, nullptr, [] {}); }, "vertex entry must write its position");
        expect_rejection(RasterDiagnosticCode::MissingVertexPosition, [] { Function::define_raster(Function::Tag::Vertex, nullptr, [] {
                                                                               auto* f = Function::current();
                                                                               f->expr_statement(f->vertex_position());
                                                                           }); }, "reading position is not an output write");
        for (auto op : { CallOp::SynchronizeBlock, CallOp::TraceClosest, CallOp::TraceOcclusion })
        {
            expect_rejection(RasterDiagnosticCode::InvalidBuiltinStage, [&] { Function::define_raster(Function::Tag::Fragment, nullptr, [&] {
                                                                                  auto* f = Function::current();
                                                                                  f->expr_statement(f->call_builtin(nullptr, op, {}));
                                                                              }); }, "raster rejects compute synchronization and ray tracing calls");
        }
        expect_rejection(RasterDiagnosticCode::InvalidBuiltinStage, [] { Function::define_raster(Function::Tag::Fragment, nullptr, [] { (void)Function::current()->thread_idx(); }); }, "raster rejects compute builtin variables");
        expect_rejection(RasterDiagnosticCode::PhysicalResourceCapture, [] { Function::define_raster(Function::Tag::Fragment, nullptr, [] {
                                                                                 uint64_t handle = 0;
                                                                                 Function::current()->add_captured_resource(Type::of<horizon::core::Buffer<float>>(),
                                                                                                                            Variable::Tag::Buffer, { &handle, sizeof(handle), alignof(uint64_t), sizeof(uint64_t) });
                                                                             }); }, "raster rejects physical resource capture");
    }

    void test_readonly_roots_and_reference_calls()
    {
        using namespace horizon::ast;
        using horizon::core::Type;
        using horizon::math::float4;
        auto writer = Function::define_callable([] {
            auto* f = Function::current();
            auto* ref = f->reference_argument(Type::of<float4>());
            f->assign(f->swizzle(Type::of<float>(), ref, 0, 1), f->literal(Type::of<float>(), 1.0f));
        });
        auto wrapper = Function::define_callable([&] {
            auto* f = Function::current();
            auto* ref = f->reference_argument(Type::of<float4>());
            f->expr_statement(f->call(nullptr, writer, { ref }));
        });
        expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [&] { Function::define_raster(Function::Tag::Fragment, nullptr, [&] {
                                                                        auto* f = Function::current();
                                                                        f->expr_statement(f->call(nullptr, wrapper, { f->local(Type::of<float4>()) }));
                                                                        f->expr_statement(f->call(nullptr, wrapper, { f->fragment_coord() }));
                                                                    }); }, "each nested reference call checks its own actual root even after a writable call");
        auto valid = Function::define_raster(Function::Tag::Fragment, nullptr, [&] {
            auto* f = Function::current();
            auto* local = f->local(Type::of<float4>());
            f->assign(local, f->fragment_coord());
            f->expr_statement(f->call(nullptr, wrapper, { local }));
        });
        expect(valid->body()->check_context(valid.get()), "copy into a local remains writable after rejected shared callable use");
        for (int path = 0; path < 4; ++path)
        {
            expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [&] { Function::define_raster(Function::Tag::Fragment, nullptr, [&] {
                                                                            auto* f = Function::current();
                                                                            auto* input = f->argument(Type::of<raster_layout_test::Outer>());
                                                                            const Expression* target = f->member(Type::of<horizon::math::float3>(), input, 0);
                                                                            if (path == 1)
                                                                            {
                                                                                target = f->swizzle(Type::of<float>(), target, 0, 1);
                                                                            }
                                                                            if (path == 2)
                                                                            {
                                                                                target = f->subscript(Type::of<float>(), target, f->literal(Type::of<int>(), 0));
                                                                            }
                                                                            if (path == 3)
                                                                            {
                                                                                target = input;
                                                                            }
                                                                            f->assign(target, f->local(target->type()));
                                                                        }); }, "stage input root stays readonly through members, swizzles and subscripts");
        }
        expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [] { Function::define_raster(Function::Tag::Fragment, nullptr, [] {
                                                                       auto* f = Function::current();
                                                                       f->assign(f->front_facing(), f->literal(Type::of<bool>(), true));
                                                                   }); }, "front-facing input is readonly");
        expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [] { Function::define_raster(Function::Tag::Vertex, nullptr, [] {
                                                                       auto* f = Function::current();
                                                                       write_position();
                                                                       f->assign(f->vertex_index(), f->literal(Type::of<unsigned int>(), 0u));
                                                                   }); }, "vertex index input is readonly");
    }

    void test_reachable_callables_and_recursion()
    {
        using namespace horizon::ast;
        auto discard = Function::define_callable([] { Function::current()->discard(); });
        auto build = [&](Function::Tag tag) {
            return Function::define_raster(tag, nullptr, [&] {
                auto* f = Function::current();
                if (tag == Function::Tag::Vertex)
                {
                    write_position();
                }
                f->expr_statement(f->call(nullptr, discard, {}));
            });
        };
        (void)build(Function::Tag::Fragment);
        expect_rejection(RasterDiagnosticCode::InvalidBuiltinStage, [&] { (void)build(Function::Tag::Vertex); }, "shared discard callable is rejected by vertex entry after fragment use");
        (void)build(Function::Tag::Fragment);
        auto position = Function::define_callable([] { write_position(); });
        auto vs = Function::define_raster(Function::Tag::Vertex, nullptr, [&] {
            auto* f = Function::current();
            f->expr_statement(f->call(nullptr, position, {}));
        });
        expect(vs->builtin_vars().empty(), "reachable position write does not copy callee builtins into entry");
        expect_rejection(RasterDiagnosticCode::InvalidBuiltinStage, [&] { Function::define_raster(Function::Tag::Fragment, nullptr, [&] {
                                                                              auto* f = Function::current();
                                                                              f->expr_statement(f->call(nullptr, position, {}));
                                                                          }); }, "position callable cannot become permanently bound to its first vertex entry");
        auto recursive = horizon::core::make_shared<Function>(Function::Tag::Callable);
        Function::push(recursive);
        recursive->with(recursive->body(), [&] {
            // Non-owning edge keeps this intentional cycle from leaking its function.
            horizon::core::shared_ptr<const Function> edge(recursive.get(), [](const Function*) {});
            recursive->expr_statement(recursive->call(nullptr, std::move(edge), {}));
        });
        Function::pop(recursive);
        expect_rejection(RasterDiagnosticCode::RecursiveCall, [&] { Function::define_raster(Function::Tag::Fragment, nullptr, [&] {
                                                                        auto* f = Function::current();
                                                                        f->expr_statement(f->call(nullptr, recursive, {}));
                                                                    }); }, "recursive call graph rejected before corrector traversal");
    }
    void test_low_level_pair_validation()
    {
        using namespace horizon::ast;
        using namespace horizon::math;
        using horizon::core::Type;
        auto make_vertex = [](const Type* type) {
            return Function::define_raster(Function::Tag::Vertex, type, [&] {
                write_position();
                if (type)
                {
                    auto* f = Function::current();
                    f->return_(f->local(type));
                }
            });
        };
        auto make_fragment = [](const Type* type) {
            return Function::define_raster(Function::Tag::Fragment, nullptr, [&] {
                if (type)
                {
                    (void)Function::current()->argument(type);
                }
            });
        };
        auto vs = make_vertex(Type::of<float3>());
        auto fs = make_fragment(Type::of<float3>());
        expect(validate_raster_pair(*vs, *fs).empty(), "matching low-level pair accepted");
        expect(validate_raster_pair(*fs, *vs).front().code == RasterDiagnosticCode::InvalidStage,
               "low-level pairing rejects reversed stages");
        auto mismatch = make_fragment(Type::of<float2>());
        auto diagnostics = validate_raster_pair(*vs, *mismatch);
        bool found_path = false;
        for (const auto& diagnostic : diagnostics)
        {
            found_path |= diagnostic.code == RasterDiagnosticCode::InterfaceMismatch &&
                          diagnostic.message.find("root 0") != std::string::npos;
        }
        expect(found_path, "different resolved leaf types report InterfaceMismatch with root path");
        auto nested = make_vertex(Type::of<raster_layout_test::Outer>());
        expect(!validate_raster_pair(*nested, *fs).empty(), "different leaf count is rejected");
        auto empty_vs = make_vertex(nullptr);
        auto empty_fs = make_fragment(nullptr);
        expect(validate_raster_pair(*empty_vs, *empty_fs).empty(), "low-level void pair accepted");
        expect(!validate_raster_pair(*empty_vs, *fs).empty(), "void output cannot feed a varying input");
    }
    void test_review_readonly_and_return_regressions()
    {
        using namespace horizon::ast;
        using horizon::core::Type;
        auto count = [](const Expression* value) {
            auto* f = Function::current();
            (void)f->for_(value, f->literal(Type::of<bool>(), true), f->literal(Type::of<unsigned>(), 1u));
        };
        expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [&] { Function::define_raster(Function::Fragment, nullptr, [&] { count(Function::current()->argument(Type::of<unsigned>())); }); }, "for-loop implicit increment cannot write a stage input");
        auto counter = Function::define_callable([&] { count(Function::current()->reference_argument(Type::of<unsigned>())); });
        expect_rejection(RasterDiagnosticCode::ReadOnlyWrite, [&] { Function::define_raster(Function::Vertex, nullptr, [&] {
                                                                        write_position();
                                                                        auto* f = Function::current();
                                                                        f->expr_statement(f->call(nullptr, counter, { f->vertex_index() }));
                                                                    }); }, "for-loop through callable reference cannot write index builtin");
        expect_rejection(RasterDiagnosticCode::InvalidInterfaceType, [] { Function::define_raster(Function::Fragment, nullptr, [] {
                                                                              auto* f = Function::current();
                                                                              auto* value = f->reference_argument(Type::of<float>());
                                                                              f->assign(value, f->literal(Type::of<float>(), 1.0f));
                                                                          }); }, "low-level raster signature rejects mutable reference input");
        expect_rejection(RasterDiagnosticCode::InvalidReturn, [] { Function::define_raster(Function::Fragment, Type::of<float>(), [] {}); }, "nonvoid empty entry cannot produce its declared output");
        auto discarded = Function::define_raster(Function::Fragment, Type::of<float>(), [] { Function::current()->discard(); });
        expect(validate_raster_function(*discarded).empty(), "discard-only fragment can terminate without a color return");
        auto callee = Function::define_callable([] { Function::current()->discard(); });
        auto indirect = Function::define_raster(Function::Fragment, Type::of<float>(), [&] {
            auto* f = Function::current();
            f->expr_statement(f->call(nullptr, callee, {}));
        });
        expect(validate_raster_function(*indirect).empty(), "reachable callable discard also terminates fragment invocation");
    }

    void test_hash_queries_during_construction()
    {
        using namespace horizon::ast;
        using horizon::core::Type;
        auto build = [](bool query, bool nested) {
            return Function::define_raster(Function::Fragment, nullptr, [&] {
                auto* f = Function::current();
                if (nested)
                {
                    auto* branch = f->if_(f->literal(Type::of<bool>(), true));
                    if (query)
                    {
                        (void)f->hash();
                        (void)branch->hash();
                    }
                    f->with(branch->true_branch(), [&] { f->discard(); });
                }
                else
                {
                    if (query)
                    {
                        (void)f->hash();
                    }
                    f->discard();
                }
            });
        };
        auto empty = Function::define_raster(Function::Fragment, nullptr, [] {});
        expect(build(true, false)->hash() == build(false, false)->hash(), "final hash ignores early root hash query");
        expect(build(true, false)->hash() != empty->hash(), "discard remains represented after early hash query");
        expect(build(true, true)->hash() == build(false, true)->hash(), "final hash invalidates nested statement and scope caches");
    }
} // namespace

namespace
{
    template <typename F>
    concept HasExtendedRasterBuiltins = requires(F& function) {
        function.primitive_index();
        function.fragment_depth();
        function.fragment_depth_greater_equal();
        function.fragment_depth_less_equal();
        function.sample_index();
        function.sample_mask();
        function.sample_mask_output();
        function.clip_distances(2u);
        function.cull_distances(2u);
        function.render_target_array_index();
        function.viewport_array_index();
        function.stencil_ref();
        function.shading_rate();
    };

    using namespace horizon::ast;

    bool has_diagnostic(const vector<RasterDiagnostic>& diagnostics, RasterDiagnosticCode code)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(),
                           [=](const auto& diagnostic) { return diagnostic.code == code; });
    }

    auto builtin_stage(Function::Tag stage)
    {
        auto function = horizon::core::make_shared<Function>(stage);
        if (stage == Function::Vertex)
        {
            function->assign(function->vertex_position(), function->local(Type::of<float4>()));
        }
        return function;
    }

    void test_extended_builtin_permissions()
    {
        struct Case
        {
            const RefExpr* (Function::*get)() noexcept;
            bool vertex;
            bool fragment;
            bool vertex_write;
            bool fragment_write;
        };
        const Case cases[] = {
            { &Function::primitive_index, false, true, false, false },
            { &Function::fragment_depth, false, true, false, true },
            { &Function::fragment_depth_greater_equal, false, true, false, true },
            { &Function::fragment_depth_less_equal, false, true, false, true },
            { &Function::sample_index, false, true, false, false },
            { &Function::sample_mask, false, true, false, false },
            { &Function::sample_mask_output, false, true, false, true },
            { &Function::render_target_array_index, true, true, true, false },
            { &Function::viewport_array_index, true, true, true, false },
            { &Function::stencil_ref, false, true, false, true },
            { &Function::shading_rate, true, true, true, false },
        };
        for (const auto& test : cases)
        {
            for (auto stage : { Function::Vertex, Function::Fragment, Function::Kernel })
            {
                auto function = builtin_stage(stage);
                const auto* value = ((*function).*test.get)();
                function->assign(value, function->local(value->type()));
                auto diagnostics = stage == Function::Kernel
                                       ? horizon::ast::detail::validate_kernel_raster_usage(*function)
                                       : validate_raster_function(*function);
                const bool allowed = stage == Function::Vertex ? test.vertex : stage == Function::Fragment && test.fragment;
                const bool writable = stage == Function::Vertex ? test.vertex_write : test.fragment_write;
                if (!allowed)
                {
                    expect(has_diagnostic(diagnostics, RasterDiagnosticCode::InvalidBuiltinStage),
                           "system value is rejected outside its supported stage");
                }
                else if (!writable)
                {
                    expect(has_diagnostic(diagnostics, RasterDiagnosticCode::ReadOnlyWrite),
                           "system-provided input cannot be overwritten");
                }
                else
                {
                    expect(diagnostics.empty(), "system output accepts a reachable write in its supported stage");
                }
            }
        }
        auto fs = builtin_stage(Function::Fragment);
        expect(fs->sample_mask() != fs->sample_mask_output(), "coverage input and output have separate identities");
        expect(fs->sample_mask() == fs->sample_mask(), "repeated builtin access reuses the same expression");
        fs->expr_statement(fs->primitive_index());
        fs->expr_statement(fs->sample_index());
        fs->assign(fs->sample_mask_output(), fs->sample_mask());
        expect(validate_raster_function(*fs).empty(), "fragment can read system inputs and forward sample coverage");
        auto unwritten = builtin_stage(Function::Fragment);
        (void)unwritten->fragment_depth();
        expect(has_diagnostic(validate_raster_function(*unwritten), RasterDiagnosticCode::MissingBuiltinWrite),
               "declared system output needs a reachable write");
    }

    void test_system_output_write_requirements()
    {
        auto check_output = [](auto getter, Function::Tag stage) {
            auto entry = builtin_stage(stage);
            expect(validate_raster_function(*entry).empty(), "unused optional system outputs need no declaration or write");
            const auto* output = getter(*entry);
            auto local = entry->local(output->type());
            entry->assign(local, output);
            expect(has_diagnostic(validate_raster_function(*entry), RasterDiagnosticCode::MissingBuiltinWrite),
                   "reading a system output into a local does not satisfy its write requirement");
            auto writer = Function::define_callable([&] {
                auto* function = Function::current();
                const auto* reference = function->reference_argument(output->type());
                function->assign(reference, function->local(reference->type()));
            });
            auto nested_writer = Function::define_callable([&] {
                auto* function = Function::current();
                const auto* reference = function->reference_argument(output->type());
                function->expr_statement(function->call(nullptr, writer, { reference }));
            });
            expect(has_diagnostic(validate_raster_function(*entry), RasterDiagnosticCode::MissingBuiltinWrite),
                   "an uncalled writer cannot satisfy a system output write requirement");
            entry->expr_statement(entry->call(nullptr, nested_writer, { output }));
            expect(validate_raster_function(*entry).empty(), "nested callable reference writes satisfy system outputs");

            auto reader = Function::define_callable([&] {
                auto* function = Function::current();
                function->expr_statement(getter(*function));
            });
            auto indirect = builtin_stage(stage);
            indirect->expr_statement(indirect->call(nullptr, reader, {}));
            expect(has_diagnostic(validate_raster_function(*indirect), RasterDiagnosticCode::MissingBuiltinWrite),
                   "system outputs declared only in a callable still require a write");
            auto direct_writer = Function::define_callable([&] {
                auto* function = Function::current();
                const auto* value = getter(*function);
                function->assign(value, function->local(value->type()));
            });
            indirect->expr_statement(indirect->call(nullptr, direct_writer, {}));
            expect(validate_raster_function(*indirect).empty(), "separate reachable callables share system output writes");
        };
        for (auto getter : { &Function::fragment_depth, &Function::fragment_depth_greater_equal,
                             &Function::fragment_depth_less_equal, &Function::sample_mask_output, &Function::stencil_ref })
        {
            check_output([=](Function& function) { return (function.*getter)(); }, Function::Fragment);
        }
        for (auto getter : { &Function::render_target_array_index, &Function::viewport_array_index, &Function::shading_rate })
        {
            check_output([=](Function& function) { return (function.*getter)(); }, Function::Vertex);
        }
        for (auto getter : { &Function::clip_distances, &Function::cull_distances })
        {
            check_output([=](Function& function) { return (function.*getter)(2); }, Function::Vertex);
        }
    }

    void test_depth_modes_and_call_chain_permissions()
    {
        auto writer = Function::define_callable([] {
            auto* f = Function::current();
            auto* arg = f->reference_argument(Type::of<uint>());
            f->assign(arg, f->literal(Type::of<uint>(), 1u));
        });
        auto fs = builtin_stage(Function::Fragment);
        fs->expr_statement(fs->call(nullptr, writer, { fs->sample_index() }));
        expect(has_diagnostic(validate_raster_function(*fs), RasterDiagnosticCode::ReadOnlyWrite),
               "callable reference writes cannot bypass system input permissions");
        auto conservative = Function::define_callable([] {
            auto* f = Function::current();
            f->assign(f->fragment_depth_greater_equal(), f->literal(Type::of<float>(), 0.5f));
        });
        auto depth = builtin_stage(Function::Fragment);
        depth->assign(depth->fragment_depth_less_equal(), depth->literal(Type::of<float>(), 0.25f));
        depth->expr_statement(depth->call(nullptr, conservative, {}));
        expect(has_diagnostic(validate_raster_function(*depth), RasterDiagnosticCode::InvalidBuiltinConfiguration),
               "depth output modes are exclusive across the reachable call chain");
        auto vs = builtin_stage(Function::Vertex);
        vs->expr_statement(vs->call(nullptr, conservative, {}));
        expect(has_diagnostic(validate_raster_function(*vs), RasterDiagnosticCode::InvalidBuiltinStage),
               "fragment-only builtin remains illegal through a callable");
        const auto modes = { &Function::fragment_depth, &Function::fragment_depth_greater_equal,
                             &Function::fragment_depth_less_equal };
        for (auto first : modes)
        {
            for (auto second : modes)
            {
                auto callable = Function::define_callable([&] {
                    auto* function = Function::current();
                    function->assign((function->*second)(), function->literal(Type::of<float>(), 0.5f));
                });
                auto entry = builtin_stage(Function::Fragment);
                entry->assign(((*entry).*first)(), entry->literal(Type::of<float>(), 0.5f));
                expect(validate_raster_function(*entry).empty(), "uncalled depth modes do not affect an entry");
                entry->expr_statement(entry->call(nullptr, callable, {}));
                auto diagnostics = validate_raster_function(*entry);
                expect(first == second ? diagnostics.empty()
                                       : has_diagnostic(diagnostics, RasterDiagnosticCode::InvalidBuiltinConfiguration),
                       "all depth mode pairs reject conflicts while repeated use of one mode is legal");
            }
        }
    }

    void test_distance_builtin_layouts()
    {
        auto vs = builtin_stage(Function::Vertex);
        auto fs = builtin_stage(Function::Fragment);
        const auto* clip = vs->clip_distances(2);
        const auto* cull = vs->cull_distances(3);
        vs->assign(clip, vs->local(clip->type()));
        vs->assign(cull, vs->local(cull->type()));
        fs->expr_statement(fs->clip_distances(2));
        fs->expr_statement(fs->cull_distances(3));
        expect(validate_raster_pair(*vs, *fs).empty(), "matching clip and cull arrays pair independently of ordinary varyings");
        fs->assign(fs->subscript(Type::of<float>(), fs->clip_distances(2), fs->literal(Type::of<uint>(), 0u)),
                   fs->literal(Type::of<float>(), 1.0f));
        expect(has_diagnostic(validate_raster_function(*fs), RasterDiagnosticCode::ReadOnlyWrite),
               "fragment distance array elements are readonly");
        auto mismatched = builtin_stage(Function::Fragment);
        mismatched->expr_statement(mismatched->clip_distances(4));
        expect(has_diagnostic(validate_raster_pair(*vs, *mismatched), RasterDiagnosticCode::InterfaceMismatch),
               "distance array lengths must match across stages");
        auto absent = builtin_stage(Function::Vertex);
        expect(has_diagnostic(validate_raster_pair(*absent, *mismatched), RasterDiagnosticCode::InterfaceMismatch),
               "fragment distance input requires a vertex producer");
        for (uint count : { 0u, 9u })
        {
            expect_rejection(RasterDiagnosticCode::InvalidBuiltinConfiguration, [&] { (void)vs->clip_distances(count); }, "invalid distance array size is rejected");
        }
        expect_rejection(RasterDiagnosticCode::InvalidBuiltinConfiguration, [&] { (void)vs->clip_distances(3); }, "one function cannot redeclare a distance array with another size");
        auto over_limit = builtin_stage(Function::Vertex);
        const auto* large = over_limit->clip_distances(5);
        const auto* other = over_limit->cull_distances(4);
        over_limit->assign(large, over_limit->local(large->type()));
        over_limit->assign(other, over_limit->local(other->type()));
        expect(has_diagnostic(validate_raster_function(*over_limit), RasterDiagnosticCode::InvalidBuiltinConfiguration),
               "combined clip and cull distance count cannot exceed eight");
    }

    void test_distance_builtin_boundaries()
    {
        for (auto getter : { &Function::clip_distances, &Function::cull_distances })
        {
            for (uint count : { 1u, 8u })
            {
                auto vs = builtin_stage(Function::Vertex);
                const auto* value = ((*vs).*getter)(count);
                expect(value->type()->is_array() && value->type()->element() == Type::of<float>() &&
                           value->type()->dimension() == count,
                       "distance boundary sizes create float arrays with the requested length");
                expect(value == ((*vs).*getter)(count), "repeated distance access reuses the array expression");
                const auto builtin_count = vs->builtin_vars().size();
                expect_rejection(RasterDiagnosticCode::InvalidBuiltinConfiguration, [&] { (void)((*vs).*getter)(count == 1 ? 8 : 1); }, "both distance kinds reject a different length for the same builtin");
                expect(value == ((*vs).*getter)(count) && vs->builtin_vars().size() == builtin_count,
                       "a conflicting array declaration leaves the original builtin intact");
                expect(has_diagnostic(validate_raster_function(*vs), RasterDiagnosticCode::MissingBuiltinWrite),
                       "a declared distance output requires a write");
                vs->assign(value, vs->local(value->type()));
                auto fs = builtin_stage(Function::Fragment);
                const auto* input = ((*fs).*getter)(count);
                fs->expr_statement(input);
                expect(validate_raster_pair(*vs, *fs).empty(), "distance arrays of length one and eight pair successfully");
                expect(validate_raster_pair(*vs, *builtin_stage(Function::Fragment)).empty(),
                       "unused vertex distance outputs are allowed");
                fs->assign(input, fs->local(input->type()));
                expect(has_diagnostic(validate_raster_function(*fs), RasterDiagnosticCode::ReadOnlyWrite),
                       "fragment distance arrays cannot be overwritten");
            }
            for (uint count : { 0u, 9u })
            {
                auto vs = builtin_stage(Function::Vertex);
                expect_rejection(RasterDiagnosticCode::InvalidBuiltinConfiguration, [&] { (void)((*vs).*getter)(count); }, "both distance kinds reject invalid lengths");
                expect(vs->builtin_vars().size() == 1, "invalid distance lengths do not register a builtin");
            }
            auto kernel = builtin_stage(Function::Kernel);
            kernel->expr_statement(((*kernel).*getter)(1));
            expect(has_diagnostic(horizon::ast::detail::validate_kernel_raster_usage(*kernel),
                                  RasterDiagnosticCode::InvalidBuiltinStage),
                   "both distance kinds are rejected from a kernel");
            for (auto policy : { PrecisionPolicy::ForceF16, PrecisionPolicy::ForceF32 })
            {
                auto vs = builtin_stage(Function::Vertex);
                vs->set_storage_policy({ policy, true });
                const auto* value = ((*vs).*getter)(2);
                expect(value->type() == Type::of<array<float, 2>>() && value == ((*vs).*getter)(2),
                       "distance arrays retain explicit float elements and reuse their type under both precision policies");
            }
        }
    }

    void test_distance_builtin_call_chains()
    {
        auto make_distance = [](auto getter, uint count, bool write) {
            return Function::define_callable([&] {
                auto* function = Function::current();
                const auto* value = (function->*getter)(count);
                if (write)
                {
                    function->assign(value, function->local(value->type()));
                }
                else
                {
                    function->expr_statement(value);
                }
            });
        };
        for (auto stage : { Function::Vertex, Function::Fragment })
        {
            for (auto getter : { &Function::clip_distances, &Function::cull_distances })
            {
                auto first = make_distance(getter, 2, stage == Function::Vertex);
                auto same = make_distance(getter, 2, stage == Function::Vertex);
                auto different = make_distance(getter, 3, stage == Function::Vertex);
                auto entry = builtin_stage(stage);
                entry->expr_statement(entry->call(nullptr, first, {}));
                entry->expr_statement(entry->call(nullptr, same, {}));
                expect(validate_raster_function(*entry).empty(), "consistent distance arrays can be shared by callables");
                entry->expr_statement(entry->call(nullptr, different, {}));
                expect(has_diagnostic(validate_raster_function(*entry), RasterDiagnosticCode::InvalidBuiltinConfiguration),
                       "distance length conflicts across callables are rejected");
            }
            for (uint cull_count : { 4u, 5u })
            {
                auto clip = make_distance(&Function::clip_distances, 4, stage == Function::Vertex);
                auto cull = make_distance(&Function::cull_distances, cull_count, stage == Function::Vertex);
                auto nested = Function::define_callable([&] {
                    auto* function = Function::current();
                    function->expr_statement(function->call(nullptr, clip, {}));
                    function->expr_statement(function->call(nullptr, clip, {}));
                    function->expr_statement(function->call(nullptr, cull, {}));
                });
                auto entry = builtin_stage(stage);
                entry->expr_statement(entry->call(nullptr, nested, {}));
                auto diagnostics = validate_raster_function(*entry);
                expect(cull_count == 4 ? diagnostics.empty()
                                       : has_diagnostic(diagnostics, RasterDiagnosticCode::InvalidBuiltinConfiguration),
                       "nested distance arrays count once per kind and enforce the combined limit of eight");
            }
        }
        auto writer = make_distance(&Function::clip_distances, 8, true);
        auto reader = make_distance(&Function::clip_distances, 8, false);
        auto vs = builtin_stage(Function::Vertex);
        auto fs = builtin_stage(Function::Fragment);
        vs->expr_statement(vs->call(nullptr, writer, {}));
        fs->expr_statement(fs->call(nullptr, reader, {}));
        expect(validate_raster_pair(*vs, *fs).empty(), "distance interfaces declared only in callables pair successfully");
    }

    void test_derivative_validation()
    {
        for (auto op : { CallOp::Ddx, CallOp::Ddy, CallOp::Fwidth })
        {
            auto derivative = Function::define_callable([&] {
                auto* f = Function::current();
                f->expr_statement(f->call_builtin(Type::of<float2>(), op, { f->local(Type::of<float2>()) }));
            });
            for (auto stage : { Function::Vertex, Function::Fragment, Function::Kernel })
            {
                auto function = builtin_stage(stage);
                function->expr_statement(function->call(nullptr, derivative, {}));
                auto diagnostics = stage == Function::Kernel
                                       ? horizon::ast::detail::validate_kernel_raster_usage(*function)
                                       : validate_raster_function(*function);
                expect(stage == Function::Fragment ? diagnostics.empty()
                                                   : has_diagnostic(diagnostics, RasterDiagnosticCode::InvalidBuiltinStage),
                       "derivative callables are legal only from fragment entries");
            }
            auto integer = builtin_stage(Function::Fragment);
            integer->expr_statement(integer->call_builtin(Type::of<int>(), op, { integer->local(Type::of<int>()) }));
            expect(has_diagnostic(validate_raster_function(*integer), RasterDiagnosticCode::InvalidBuiltinType),
                   "low-level derivative rejects integer operands");
            auto shape = builtin_stage(Function::Fragment);
            shape->expr_statement(shape->call_builtin(Type::of<float>(), op, { shape->local(Type::of<float2>()) }));
            expect(has_diagnostic(validate_raster_function(*shape), RasterDiagnosticCode::InvalidBuiltinType),
                   "derivative result preserves the operand shape");
            auto arity = builtin_stage(Function::Fragment);
            arity->expr_statement(arity->call_builtin(Type::of<float>(), op, {}));
            expect(has_diagnostic(validate_raster_function(*arity), RasterDiagnosticCode::InvalidBuiltinType),
                   "derivative requires exactly one operand");
        }
    }

    void test_derivative_invalid_ast_inputs()
    {
        for (auto op : { CallOp::Ddx, CallOp::Ddy, CallOp::Fwidth })
        {
            for (const auto* type : { Type::of<bool>(), Type::of<uint>(), Type::of<uint2>(), Type::of<bool3>(),
                                      Type::of<float4x4>(), Type::of<array<float, 2>>(), Type::of<raster_layout_test::Inner>() })
            {
                auto function = builtin_stage(Function::Fragment);
                function->expr_statement(function->call_builtin(type, op, { function->local(type) }));
                expect(has_diagnostic(validate_raster_function(*function), RasterDiagnosticCode::InvalidBuiltinType),
                       "low-level derivatives reject nonfloating and aggregate operands");
            }
            auto extra_operand = builtin_stage(Function::Fragment);
            const auto* operand = extra_operand->local(Type::of<float>());
            extra_operand->expr_statement(extra_operand->call_builtin(Type::of<float>(), op, { operand, operand }));
            expect(has_diagnostic(validate_raster_function(*extra_operand), RasterDiagnosticCode::InvalidBuiltinType),
                   "derivatives reject multiple operands even when their types match");
            auto null_operand = builtin_stage(Function::Fragment);
            null_operand->expr_statement(null_operand->call_builtin(Type::of<float>(), op, { nullptr }));
            expect(has_diagnostic(validate_raster_function(*null_operand), RasterDiagnosticCode::InvalidBuiltinType),
                   "derivatives reject a null operand without dereferencing it");
            auto void_operand = builtin_stage(Function::Fragment);
            const auto* void_value = void_operand->call(nullptr, "void_probe", {});
            void_operand->expr_statement(void_operand->call_builtin(Type::of<float>(), op, { void_value }));
            expect(has_diagnostic(validate_raster_function(*void_operand), RasterDiagnosticCode::InvalidBuiltinType),
                   "derivatives reject operands without a value type");

            for (CallExpr::Template argument : { CallExpr::Template { Type::of<float>() }, CallExpr::Template { 1u } })
            {
                auto function = builtin_stage(Function::Fragment);
                function->expr_statement(function->call_builtin(Type::of<float>(), op,
                                                                { function->local(Type::of<float>()) }, { argument }));
                expect(has_diagnostic(validate_raster_function(*function), RasterDiagnosticCode::InvalidBuiltinType),
                       "derivatives reject both type and value template arguments");
            }
            struct Types
            {
                const Type* operand;
                const Type* result;
            };
            const Types mismatches[] {
                { Type::of<float>(), Type::of<half>() },
                { Type::of<half>(), Type::of<float>() },
                { Type::of<float2>(), Type::of<float3>() },
                { Type::of<float>(), nullptr },
            };
            for (const auto& types : mismatches)
            {
                auto function = builtin_stage(Function::Fragment);
                function->expr_statement(function->call_builtin(types.result, op, { function->local(types.operand) }));
                expect(has_diagnostic(validate_raster_function(*function), RasterDiagnosticCode::InvalidBuiltinType),
                       "derivative results must preserve precision, dimensions, and a nonvoid value type");
            }
            auto malformed = Function::define_callable([&] {
                auto* function = Function::current();
                function->expr_statement(function->call_builtin(Type::of<bool>(), op, { function->local(Type::of<bool>()) }));
            });
            auto nested = Function::define_callable([&] {
                auto* function = Function::current();
                function->expr_statement(function->call(nullptr, malformed, {}));
            });
            auto indirect = builtin_stage(Function::Fragment);
            indirect->expr_statement(indirect->call(nullptr, nested, {}));
            expect(has_diagnostic(validate_raster_function(*indirect), RasterDiagnosticCode::InvalidBuiltinType),
                   "nested callables cannot hide an invalid derivative operand type");
        }
    }
}

int main(int argc, char** argv)
{
    expect(HasExtendedRasterBuiltins<horizon::ast::Function>, "extended raster builtin expressions are available");
    if (argc > 1 && std::string_view(argv[1]) == "--kernel-discard")
    {
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        auto discard = horizon::ast::Function::define_callable([] { horizon::ast::Function::current()->discard(); });
        horizon::ast::Function::define_kernel([&] {
            auto* f = horizon::ast::Function::current();
            f->expr_statement(f->call(nullptr, discard, {}));
        });
        return 0;
    }
    test_stage_construction_and_exception_recovery();
    test_extended_builtin_permissions();
    test_system_output_write_requirements();
    test_depth_modes_and_call_chain_permissions();
    test_distance_builtin_layouts();
    test_distance_builtin_boundaries();
    test_distance_builtin_call_chains();
    test_derivative_validation();
    test_derivative_invalid_ast_inputs();
    test_invalid_raster_tag_is_diagnosed();
    test_recursive_interface_layout_and_hash();
    test_interface_rejections_and_declared_returns();
    test_interface_precision_policy();
    test_raster_builtins_and_stage_rules();
    test_readonly_roots_and_reference_calls();
    test_reachable_callables_and_recursion();
    test_low_level_pair_validation();
    test_review_readonly_and_return_regressions();
    test_hash_queries_during_construction();
    return failures == 0 ? 0 : 1;
}
