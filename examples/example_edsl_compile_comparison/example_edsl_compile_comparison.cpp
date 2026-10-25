// Compile-time comparison for EDSL shader variants.
//
// CPU if: a native C++ if only emits the selected branch into the AST. Every
// new CPU branch therefore needs a fresh RasterizerPipeline: AST generation,
// Slang compilation and Vulkan pipeline creation all run again.
//
// $IF: all branch modules are loaded during the initial compile. Changing the
// condition only needs combine (session/module reuse) + pipeline rebuild.

#include <Codegen/BuiltinVariate.h>
#include <Codegen/ControlFlows.h>
#include <Codegen/CustomLibrary.h>
#include <Codegen/TypeAlias.h>
#include <horizon.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <span>

using namespace EmbeddedShader;
using namespace ktm;

namespace
{
constexpr uint32_t benchmark_size = 64;

struct BenchCpuVertex
{
    std::array<float, 3> pos;
};

double elapsed_ms(std::chrono::steady_clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}
} // namespace

// PFR cannot reflect local/anonymous-namespace types; these must be at global scope.
struct BenchEdslVertex
{
    Float3 pos;
};

struct BenchVaryings
{
    Float3 color;
};

void run_example_edsl_compile_comparison()
{
    using Clock = std::chrono::steady_clock;

    horizon::HardwareImage output_image(horizon::HardwareImageDesc::texture_2d(
        benchmark_size, benchmark_size, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment,
        "example_edsl_compile_comparison.output"));
    output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    Texture2D<fvec4> final_output = output_image;

    const std::array<BenchCpuVertex, 3> vertices {{
        { { -1.0f, -1.0f, 0.0f } },
        { { 3.0f, -1.0f, 0.0f } },
        { { -1.0f, 3.0f, 0.0f } },
    }};
    const std::array<uint16_t, 3> indices { 0, 1, 2 };

    horizon::HardwareBuffer vertex_buffer =
        horizon::HardwareBuffer::vertex(vertices, "example_edsl_compile_comparison.vb");
    horizon::HardwareBuffer index_buffer =
        horizon::HardwareBuffer::index(indices, "example_edsl_compile_comparison.ib");

    horizon::DrawIndexedIndirectCommand indirect_command {};
    indirect_command.index_count = static_cast<uint32_t>(indices.size());
    indirect_command.instance_count = 1;
    horizon::HardwareBuffer indirect_buffer = horizon::HardwareBuffer::from_bytes(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&indirect_command),
                                   sizeof(indirect_command)),
        static_cast<uint32_t>(sizeof(indirect_command)),
        horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
        "example_edsl_compile_comparison.indirect");

    auto vertex_shader = [](Aggregate<BenchEdslVertex> vertex) {
        position() = Float4(vertex->pos, Float(1.0));
        Aggregate<BenchVaryings> out;
        out->color = vertex->pos;
        return out;
    };

    bool cpu_red = true;
    auto fragment_cpu = [&](Aggregate<BenchVaryings> in) {
        Float3 color;
        // Native C++ if: only the selected branch reaches the AST.
        if (cpu_red)
            color = Float3(Float(1.0), Float(0.0), Float(0.0));
        else
            color = Float3(Float(0.0), Float(1.0), Float(0.0));
        final_output << Float4(color, Float(1.0));
    };

    bool if_red = true;
    auto fragment_if = [&](Aggregate<BenchVaryings> in) {
        Float3 color;
        $IF(if_red)
        {
            color = Float3(Float(1.0), Float(0.0), Float(0.0));
        }
        $ELSE
        {
            color = Float3(Float(0.0), Float(1.0), Float(0.0));
        };
        final_output << Float4(color, Float(1.0));
    };

    horizon::RasterizerPipelineDesc desc;
    desc.depth_test_enabled = false;
    desc.depth_write_enabled = false;
    desc.blend_enabled = false;

    horizon::HardwareExecutor executor;
    auto draw_once = [&](auto& pipeline) {
        pipeline.clear_records();
        horizon::DrawIndexedIndirectParams params;
        params.draw_count = 1;
        pipeline.record_indirect(index_buffer, vertex_buffer, indirect_buffer, params);
        auto receipt = executor << pipeline.extent(benchmark_size, benchmark_size) << horizon::commit();
        executor.wait(receipt);
    };

    // Warm up the Vulkan device/command pool and Slang global session once;
    // otherwise the first measured variant would include one-time setup.
    {
        horizon::RasterizerPipeline warmup(vertex_shader, fragment_cpu, desc);
        draw_once(warmup);
    }

    // Full compile: one native C++ branch per pipeline.
    auto t0 = Clock::now();
    horizon::RasterizerPipeline cpu_variant_a(vertex_shader, fragment_cpu, desc);
    draw_once(cpu_variant_a);
    auto t1 = Clock::now();
    std::printf("[compile-comparison] cpu-if full variant A (AST -> Vulkan pipeline): %.3f ms\n",
                elapsed_ms(t1 - t0));

    cpu_red = false;
    t0 = Clock::now();
    horizon::RasterizerPipeline cpu_variant_b(vertex_shader, fragment_cpu, desc);
    draw_once(cpu_variant_b);
    t1 = Clock::now();
    std::printf("[compile-comparison] cpu-if full variant B (AST -> Vulkan pipeline): %.3f ms\n",
                elapsed_ms(t1 - t0));

    // $IF initial compile, then incremental combine for the second variant.
    if_red = true;
    t0 = Clock::now();
    horizon::RasterizerPipeline if_pipeline(vertex_shader, fragment_if, desc);
    draw_once(if_pipeline);
    t1 = Clock::now();
    std::printf("[compile-comparison] $IF full initial (AST -> Vulkan pipeline): %.3f ms\n",
                elapsed_ms(t1 - t0));

    if_red = false;
    t0 = Clock::now();
    const bool switched = if_pipeline.sync_shader_conditions();
    draw_once(if_pipeline);
    t1 = Clock::now();
    std::printf("[compile-comparison] $IF combine incremental (variant -> Vulkan pipeline): %.3f ms (switched=%d)\n",
                elapsed_ms(t1 - t0), switched ? 1 : 0);
    std::fflush(stdout);
}
