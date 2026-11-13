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
#include "scene_pass.h"
#include "legacy_common.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

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

    HardwareImage output_image(example::imageInfo(benchmark_size, benchmark_size, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage));
    output_image.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    Texture2D<fvec4> final_output = output_image;

    const std::array<BenchCpuVertex, 3> vertices {{
        { { -1.0f, -1.0f, 0.0f } },
        { { 3.0f, -1.0f, 0.0f } },
        { { -1.0f, 3.0f, 0.0f } },
    }};
    const std::array<uint16_t, 3> indices { 0, 1, 2 };

    HardwareBuffer vertex_buffer =
        HardwareBuffer::uploadDeviceLocal(vertices, BufferUsage::VertexBuffer);
    HardwareBuffer index_buffer =
        HardwareBuffer::uploadDeviceLocal(indices, BufferUsage::IndexBuffer);


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


    HardwareExecutor executor;
    auto draw_once = [&](auto& pipeline) {
        pipeline.draw({vertex_buffer, index_buffer});
        executor << pipeline;
        executor.commit().waitForDeferredResources();
    };

    // Readback is outside the measured compile and submit interval.
    auto verify_color = [&](bool red) {
        using Pixel = std::array<uint16_t, 4>;
        std::vector<Pixel> pixels(benchmark_size * benchmark_size);
        HardwareBuffer readback(static_cast<uint32_t>(pixels.size() * sizeof(Pixel)), BufferUsage::StorageBuffer);
        executor << output_image.copyTo(readback);
        executor.commit().waitForDeferredResources();
        constexpr uint16_t one = 0x3c00; // binary16
        const Pixel expected = red ? Pixel{one, 0, 0, one} : Pixel{0, one, 0, one};
        if (!readback.copyToData(pixels.data(), pixels.size() * sizeof(Pixel)) ||
            !std::ranges::all_of(pixels, [&](const Pixel& pixel) { return pixel == expected; }))
            throw std::runtime_error("Unexpected color from EDSL shader variant");
    };

    // Warm up the Vulkan device/command pool and Slang global session once;
    // otherwise the first measured variant would include one-time setup.
    {
        example::ScenePass<example::NoDrawParameters> warmup(benchmark_size, benchmark_size, {{false, false, RasterCompare::LessEqual, RasterBlend::Opaque}, {false, false}}, vertex_shader, fragment_cpu, 1, example::shaderOptions());
        draw_once(warmup);
    }

    // Full compile: one native C++ branch per pipeline.
    auto t0 = Clock::now();
    example::ScenePass<example::NoDrawParameters> cpu_variant_a(benchmark_size, benchmark_size, {{false, false, RasterCompare::LessEqual, RasterBlend::Opaque}, {false, false}}, vertex_shader, fragment_cpu, 1, example::shaderOptions());
    draw_once(cpu_variant_a);
    auto t1 = Clock::now();
    std::printf("[compile-comparison] cpu-if full variant A (AST -> Vulkan pipeline): %.3f ms\n",
                elapsed_ms(t1 - t0));

    verify_color(true);
    cpu_red = false;
    t0 = Clock::now();
    example::ScenePass<example::NoDrawParameters> cpu_variant_b(benchmark_size, benchmark_size, {{false, false, RasterCompare::LessEqual, RasterBlend::Opaque}, {false, false}}, vertex_shader, fragment_cpu, 1, example::shaderOptions());
    draw_once(cpu_variant_b);
    t1 = Clock::now();
    std::printf("[compile-comparison] cpu-if full variant B (AST -> Vulkan pipeline): %.3f ms\n",
                elapsed_ms(t1 - t0));

    verify_color(false);

    // $IF initial compile, then incremental combine for the second variant.
    if_red = true;
    t0 = Clock::now();
    example::ScenePass<example::NoDrawParameters> if_pipeline(benchmark_size, benchmark_size, {{false, false, RasterCompare::LessEqual, RasterBlend::Opaque}, {false, false}}, vertex_shader, fragment_if, 1, example::shaderOptions());
    draw_once(if_pipeline);
    t1 = Clock::now();
    std::printf("[compile-comparison] $IF full initial (AST -> Vulkan pipeline): %.3f ms\n",
                elapsed_ms(t1 - t0));

    verify_color(true);
    if_red = false;
    t0 = Clock::now();
    const bool switched = if_pipeline.syncShaderConditions();
    draw_once(if_pipeline);
    t1 = Clock::now();
    std::printf("[compile-comparison] $IF combine incremental (variant -> Vulkan pipeline): %.3f ms (switched=%d)\n",
                elapsed_ms(t1 - t0), switched ? 1 : 0);
    verify_color(false);
    if (!switched) throw std::runtime_error("EDSL condition did not switch");
    std::fflush(stdout);
}
