#include "ast/function.h"
#include "ast/function_requirements.h"
#include "backends/vulkan_compute/ast_to_slang_source.h"
#include "backends/vulkan_compute/slang_compiler.h"
#include "backends/vulkan_compute/trace_requirements.h"
#include "backends/vulkan_compute/vk_shader_parameters.h"
#include "dsl/dsl.h"
#include <future>
#include <iostream>

using namespace ocarina;

namespace {

void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void invoke(const shared_ptr<Function> &callee) {
    auto *f = Function::current();
    f->expr_statement(f->call(nullptr, callee, {}));
}

void requirements() {
    // A discarded trace expression sets the legacy marker but emits no trace.
    const auto marked = Function::define_kernel([] {
        Function::current()->call_builtin(Type::of<bool>(), CallOp::TRACE_OCCLUSION, {});
    });
    check(marked->is_raytracing(), "fixture must have the legacy trace marker");
    check(!analyze_function_requirements(*marked).uses_trace, "discarded trace selected a tracing mode");

    // Both public trace APIs can be reached through two layers of callables.
    auto check_trace = [](auto trace) {
        const auto leaf = Function::define_callable([&] {
            Var<Accel> scene{detail::ArgumentCreation{}};
            Var<Ray> ray{detail::ArgumentCreation{}};
            trace(scene, ray);
        });
        auto forward = [&](const shared_ptr<Function> &callee) {
            auto *f = Function::current();
            auto *scene = f->argument(Type::of<Accel>());
            auto *ray = f->argument(Type::of<Ray>());
            f->expr_statement(f->call(nullptr, callee, {scene, ray}));
        };
        const auto mid = Function::define_callable([&] { forward(leaf); });
        const auto root = Function::define_kernel([&] { forward(mid); });
        const auto r = analyze_function_requirements(*root);
        check(r.uses_trace && native_trace_compatible(r), "nested trace must be eligible for native tracing");
    };
    check_trace([](Var<Accel> &scene, Var<Ray> &ray) { (void)scene.trace_closest(ray); });
    check_trace([](Var<Accel> &scene, Var<Ray> &ray) {
        Float distance;
        (void)scene.trace_closest(ray, distance);
    });
    check_trace([](Var<Accel> &scene, Var<Ray> &ray) { (void)scene.trace_occlusion(ray); });

    const auto leaf = Function::define_callable([] {
        auto *f = Function::current();
        f->set_description("nested_leaf");
        f->expr_statement(f->thread_idx());
        f->expr_statement(f->block_idx());
        f->expr_statement(f->thread_id());
        f->expr_statement(f->call_builtin(nullptr, CallOp::SYNCHRONIZE_BLOCK, {}));
        f->expr_statement(f->call_builtin(Type::of<uint>(), CallOp::WARP_LANE_ID, {}));
        f->expr_statement(f->call(nullptr, "opaque_fn", {}));
        f->add_header("opaque.slang");
    });
    const auto left = Function::define_callable([&] { invoke(leaf); });
    const auto right = Function::define_callable([&] { invoke(leaf); });
    const auto root = Function::define_kernel([&] { invoke(left); invoke(right); });
    const auto r = analyze_function_requirements(*root);
    check(!r.uses_trace && r.requires_compute_workgroups && r.uses_warp &&
          r.has_native_source && !native_trace_compatible(r), "nested stage restrictions were lost");
    for (string_view reason : {"block_idx", "thread_idx", "thread_id", "synchronize_block",
                               "warp operation", "native call 'opaque_fn'", "custom header 'opaque.slang'"}) {
        const auto expected = ocarina::format("callable 'nested_leaf': {}", reason);
        const auto count = std::count(r.diagnostic_sources.begin(), r.diagnostic_sources.end(), expected);
        if (count != 1) {
            std::cerr << reason << " matched " << count << " diagnostics:\n";
            for (const auto &source : r.diagnostic_sources) std::cerr << source << '\n';
            std::exit(1);
        }
    }

    const auto shared = Function::define_kernel([] {
        auto *f = Function::current();
        auto *loop = f->loop();
        f->with(loop->body(), [&] { (void)f->shared(Type::of<uint>()); });
    });
    check(analyze_function_requirements(*shared).requires_compute_workgroups, "nested shared declaration missed");
    const auto explicit_groups = Function::define_kernel([] { Function::current()->require_ray_query_compute_workgroups(); });
    check(!native_trace_compatible(analyze_function_requirements(*explicit_groups)), "explicit workgroup requirement missed");
    const auto dispatch = Function::define_kernel([] {
        auto *f = Function::current();
        f->expr_statement(f->dispatch_idx());
        f->expr_statement(f->dispatch_id());
        f->expr_statement(f->dispatch_dim());
    });
    check(native_trace_compatible(analyze_function_requirements(*dispatch)), "dispatch builtins are valid in raygen");
}

void typed_texture_requirements() {
    const auto kernel = Function::define_kernel([] {
        Var<Texture2D> floats{detail::ArgumentCreation{}};
        Var<Texture2D> uints{detail::ArgumentCreation{}};
        const uint2 pixel = make_uint2(0u, 1u);
        floats.write_native(pixel, floats.read_native<float4>(pixel));
        uints.write_native(pixel, uints.read_native<uint2>(pixel));
    });
    check(native_trace_compatible(analyze_function_requirements(*kernel)),
          "typed texture access incorrectly prohibits native tracing");
    for (auto mode : {ShaderExecutionMode::Compute, ShaderExecutionMode::RayTracingPipeline}) {
        const auto source = AstToSlangSource::generate(*kernel, {{}, mode, {}}, false, false).source;
        check(source.find("oc_tex2d_read_native_float4(") != string::npos &&
              source.find("oc_tex2d_read_native_uint2(") != string::npos &&
              source.find("oc_tex2d_write_native(") != string::npos,
              "typed textures did not lower to the correct Slang helpers without warp32");
    }
}

void subgroup_requirements() {
    const auto arithmetic = Function::define_kernel([] { (void)subgroup_active_sum(1u); });
    const auto ballot = Function::define_kernel([] { (void)subgroup_active_count_bits(Bool{true}); });
    const auto basic = Function::define_kernel([] {
        (void)subgroup_lane_count();
        (void)subgroup_is_first_lane();
    });
    const auto sum_req = analyze_function_requirements(*arithmetic);
    const auto count_req = analyze_function_requirements(*ballot);
    const auto basic_req = analyze_function_requirements(*basic);
    check(sum_req.uses_subgroup && sum_req.uses_subgroup_arithmetic && !sum_req.uses_subgroup_ballot,
          "hardware subgroup sum requires arithmetic support");
    check(count_req.uses_subgroup && !count_req.uses_subgroup_arithmetic && count_req.uses_subgroup_ballot,
          "hardware subgroup count-bits requires ballot support");
    check(basic_req.uses_subgroup && !basic_req.uses_subgroup_arithmetic && !basic_req.uses_subgroup_ballot,
          "hardware subgroup lane operations require only basic support");
    check(!sum_req.uses_warp && !native_trace_compatible(sum_req),
          "hardware subgroups must select compute without imposing the warp32 contract");
    const auto source = AstToSlangSource::generate(*arithmetic, {}, false, false).source;
    check(source.find("WaveActiveSum(") != string::npos, "hardware subgroup sum could not emit without warp32");
}

void per_compile_uniform_layout() {
    // Match the existing NRD type policy without depending on the external NRD project.
    string record = "struct<vision::reblur::edsl::SharedConstants,16,false,false";
    for (uint i = 0; i < 45; ++i) record += ",vector<float,4>";
    for (uint i = 0; i < 27; ++i) record += ",uint";
    const auto *type = Type::from(record + ">");
    const auto kernel = Function::define_kernel([&] {
        auto *f = Function::current();
        auto *constants = f->argument(type);
        f->expr_statement(f->member(Type::of<uint>(), constants, 71));
    });
    kernel->set_description("ordinary-ast-description");
    const string qualified = "NRD-EDSL-REBLUR-PerCompile";
    const string ordinary = "ordinary-compile";
    for (const auto &name : {qualified, ordinary}) {
        const bool uniform = name == qualified;
        const VkShaderParameterLayout layout{*kernel, name};
        check(!layout.is_inline() && layout.uniform_bytes == (uniform ? 832u : 0u),
              "per-call name did not select the expected parameter transport");
        for (auto mode : {ShaderExecutionMode::Compute, ShaderExecutionMode::RayTracingPipeline}) {
            const auto source = AstToSlangSource::generate(*kernel, {{}, mode, name}, false, true).source;
            check((source.find("[[vk::binding(0, 1)]] cbuffer OCReadonlyParameters") != string::npos) == uniform,
                  "Slang source disagrees with the per-call parameter transport");
        }
    }
    check(kernel->description() == "ordinary-ast-description", "parameter policy changed the AST description");
}

void concurrent_emission() {
    const auto callee = Function::define_callable([] {});
    const auto kernel = Function::define_kernel([&] { invoke(callee); });
    kernel->set_description("original");
    const auto first = callee->create_variable(Type::of<uint>(), Variable::Tag::LOCAL).uid();
    auto generate = [&](ShaderCompilationContext context) {
        return AstToSlangSource::generate(*kernel, context, false, true);
    };
    auto query = std::async(std::launch::async, generate, ShaderCompilationContext{
        {TraceMode::RayQuery}, ShaderExecutionMode::ComputeRayQuery, "query-label"});
    auto native = std::async(std::launch::async, generate, ShaderCompilationContext{
        {TraceMode::RayTracingPipeline}, ShaderExecutionMode::RayTracingPipeline, "native-label"});
    const auto q = query.get();
    const auto n = native.get();
    check(n.source.find("[shader(\"raygeneration\")]") != string::npos &&
          n.source.find("DispatchRaysIndex()") != string::npos &&
          n.source.find("SV_DispatchThreadID") == string::npos, "native entry requires raygeneration builtins");
    check(q.source.find("[shader(\"compute\")]") != string::npos &&
          q.source.find("SV_DispatchThreadID") != string::npos, "query entry requires compute builtins");
    check(q.function_name == n.function_name, "same AST produced different function names");
    check(q.source.find("query-label") != string::npos && q.source.find("native-label") == string::npos &&
          n.source.find("native-label") != string::npos && n.source.find("query-label") == string::npos,
          "compilation descriptions leaked across emitted sources");
    check(kernel->description() == "original", "compilation changed the AST description");
    check(callee->create_variable(Type::of<uint>(), Variable::Tag::LOCAL).uid() == first + 1u,
          "source emission appended variables to the callable");
}

}// namespace

int main() {
    requirements();
    typed_texture_requirements();
    subgroup_requirements();
    per_compile_uniform_layout();
    concurrent_emission();
    const auto discarded = Function::define_callable([] {
        auto *f = Function::current();
        f->set_description("unreachable trace and workgroup operations");
        Var<Accel> scene{detail::ArgumentCreation{}};
        Var<Ray> ray{detail::ArgumentCreation{}};
        (void)scene.trace_occlusion(ray);
        f->expr_statement(f->thread_idx());
        f->expr_statement(f->call_builtin(Type::of<uint>(), CallOp::WARP_LANE_ID, {}));
    });
    const auto plain = Function::define_kernel([&] {
        (void)Function::current()->call(nullptr, discarded, {});
    });
    check(!analyze_function_requirements(*plain).uses_trace, "unreachable trace selected a tracing mode");
    const auto source = AstToSlangSource::generate(*plain, {}, false, false).source;
    check(source.find("unreachable trace") == string::npos, "discarded callable was emitted");
}
