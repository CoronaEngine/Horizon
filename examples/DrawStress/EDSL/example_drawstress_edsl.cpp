#include "Horizon.h"
#include "../scene.h"
#include "window.h"
#include "example_benchmark/benchmark.h"
#include "horizon_profiling.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <utility>

namespace drawstress_edsl {
struct Varyings { EmbeddedShader::Float3 color; };
}

void run_example_drawstress_edsl()
{
    using namespace drawstress;
    using namespace EmbeddedShader;
    using namespace ktm;
    using namespace drawstress_edsl;
    constexpr auto mode = "drawstress_edsl";
    auto window = examples::open_window(stress_width, stress_height, mode);

    int dim = std::clamp(bench::env_int("HORIZON_DRAWSTRESS_DIM", 40), dim_min, dim_max);
    attach_input(window.get(), dim, mode);

    auto cube = make_cube();
    auto commands = make_draw_packets();
    const auto view_projection = make_view_projection();

    HardwareImage final_output_image(stress_width, stress_height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    final_output_image.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);

    const auto shader_start = bench::Clock::now();
    Texture2D<fvec4> output;
    auto vs = [&]() {
        const auto corner = vertexIndex();
        Aggregate<Varyings> out;
        out->color = Float3(corner & Uint(1), (corner >> Uint(1)) & Uint(1), (corner >> Uint(2)) & Uint(1));
        Float3 pos = out->color * Float3(2.0f, -2.0f, -2.0f) + Float3(-1.0f, 1.0f, 1.0f);
        auto mvp = rasterDrawParameter<fmat4x4>(0);
        position() = mul(mvp, Float4(pos, Float(1)));
        return out;
    };
    auto fs = [&](Aggregate<Varyings> in) { output << Float4(in->color, Float(1)); };
    RasterizerPipeline<> rasterizer(vs, fs, 1, CompilerOption{.enableMatrixColumnMajor = true});
    const double shader_setup_ms = bench::milliseconds_since(shader_start);

    HardwareExecutor render_executor;
    HardwareDisplayer display(examples::native_handle(window.get()), PresentDevicePreference::RenderDevice);

    auto run = bench::start_run(mode, stress_width, stress_height, shader_setup_ms);
    while (!glfwWindowShouldClose(window.get()))
    {
        bench::begin_frame(run);
        glfwPollEvents();

        const auto record_start = bench::Clock::now();
        output = final_output_image;
        auto batch = rasterizer.beginBatch();
        const uint32_t draw_count = static_cast<uint32_t>(dim * dim * dim);
        HORIZON_PROFILE_PLOT("draw calls", draw_count);
        {
            HORIZON_PROFILE_SCOPE_N("drawstress::submit");
            batch.drawIndirect(cube, commands, draw_count, draw_count * sizeof(glm::mat4),
                [&](std::span<std::byte> destination) {
                    write_matrices(destination, view_projection, dim, run.elapsed_seconds);
                }, IndirectFirstInstance::AlwaysZero);

            if (run.present_normally) {
                render_executor << std::move(batch);
                const bool presented = display.present(render_executor, final_output_image);
                bench::observe_present(run.presents, window.get(), presented);
            } else {
                bench::submit_measured(run, record_start, render_executor, std::move(batch), display, final_output_image);
            }
        }
        bench::finish_frame(run, window.get(), draw_count, final_output_image, render_executor);
    }

    render_executor.waitForDeferredResources();
}
