#include "Horizon.h"
#include "../scene.h"
#include "window.h"
#include "example_benchmark/benchmark.h"
#include "Codegen/ControlFlows.h"

#include SLANG(Disney/Slang/disney_vert.slang)
#include SLANG(Disney/Slang/disney_frag.slang)

#include <GLFW/glfw3.h>
#include <utility>

namespace {
struct DrawParameters {
    disney::DisneyDrawParameters sphere;
    uint32_t texCubeIndex, texCubeIrrIndex;
};
static_assert(offsetof(DrawParameters, texCubeIndex) == 29 * sizeof(uint32_t));
static_assert(offsetof(DrawParameters, texCubeIrrIndex) == 30 * sizeof(uint32_t));
} // namespace

void run_example_disney_slang()
{
    using namespace disney;
    constexpr auto mode = "disney_slang";
    auto window = examples::open_window(disney_width, disney_height, mode);

    InputContext input;
    attach_input(window.get(), input);

    HardwareImage radiance = load_cubemap("bolonga_lod.dds");
    HardwareImage irradiance = load_cubemap("bolonga_irr.dds");
    auto sphere = make_sphere();

    HardwareImage final_output_image(disney_width, disney_height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);

    HardwareImage depth_image(disney_width, disney_height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage);

    const auto shader_start = bench::Clock::now();
    RasterizerPipeline<disney_vert_slang_t, disney_frag_slang_t> rasterizer;
    const double shader_setup_ms = bench::milliseconds_since(shader_start);
    rasterizer.outColor = final_output_image;
    rasterizer.setDepthImage(depth_image);
    rasterizer.setRasterState({.topology = RasterTopology::TriangleStrip});

    HardwareExecutor render_executor;
    HardwareDisplayer display(examples::native_handle(window.get()), PresentDevicePreference::RenderDevice);

    auto run = bench::start_run(mode, disney_width, disney_height, shader_setup_ms);
    while (!glfwWindowShouldClose(window.get()))
    {
        bench::begin_frame(run);
        glfwPollEvents();
        const float dt = run.delta_seconds;
        const auto frame = update_frame(input, dt);

        const auto record_start = bench::Clock::now();
        const uint32_t cube_index = radiance.storeDescriptor();
        const uint32_t irradiance_index = irradiance.storeDescriptor();
        rasterizer.vsp.proj_view = frame.proj_view;
        rasterizer.vsp.camPos = frame.camera;
        rasterizer.vsp.flags = frame.flags;
        rasterizer.vsp.lightDir = frame.light_direction;
        rasterizer.vsp.lightCol = frame.light_color;
        rasterizer.vsp.envMtx = frame.env_rotation;
        rasterizer.vsp.exposurePad = frame.exposure;
        rasterizer.uses({{radiance, ResourceAccess::Read}, {irradiance, ResourceAccess::Read}});
        auto batch = rasterizer.beginBatch(disney_width, disney_height);

        for (int row = 0; row < chart_row_count; ++row)
        {
            for (int col = 0; col < chart_col_count; ++col)
            {
                const DrawParameters draw{sphere_parameters(row, col), cube_index, irradiance_index};
                batch.draw(sphere, {}, draw);
            }
        }

        if (run.present_normally) {
            render_executor << std::move(batch);
            const bool presented = display.present(render_executor, final_output_image);
            bench::observe_present(run.presents, window.get(), presented);
        } else {
            bench::submit_measured(run, record_start, render_executor, std::move(batch), display, final_output_image);
        }
        bench::finish_frame(run, window.get(), chart_sphere_count, final_output_image, render_executor);
    }

    render_executor.waitForDeferredResources();
}
