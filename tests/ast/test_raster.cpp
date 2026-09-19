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

int main(int argc, char** argv)
{
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
