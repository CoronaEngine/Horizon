#include "../scene.h"
#include "common.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"

#include SLANG(Disney/Slang/disney_vert.slang)
#include SLANG(Disney/Slang/disney_frag.slang)

#include <GLFW/glfw3.h>
#include <chrono>

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
    examples::Window window_owner(disney_width, disney_height, mode);
    GLFWwindow* window = window_owner.get();

    InputContext input;
    attach_input(window, input);

    HardwareImage radiance = load_cubemap("bolonga_lod.dds");
    HardwareImage irradiance = load_cubemap("bolonga_irr.dds");
    MaterialChart chart;

    HardwareImage final_output_image(disney_width, disney_height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);

    HardwareImage depth_image(disney_width, disney_height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage);

    const auto shader_start = std::chrono::steady_clock::now();
    RasterizerPipeline<disney_vert_slang_t, disney_frag_slang_t> rasterizer;
    const double shader_setup_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shader_start).count();
    rasterizer.outColor = final_output_image;
    rasterizer.setDepthImage(depth_image);
    rasterizer.setRasterState({.topology = RasterTopology::TriangleStrip});

    HardwareExecutor render_executor;
    HardwareDisplayer display(window_owner.native_handle(), PresentDevicePreference::RenderDevice);

    examples::Benchmark benchmark(mode, disney_width, disney_height);
    benchmark.set_shader_setup_ms(shader_setup_ms);
    while (benchmark.running(window))
    {
        benchmark.begin_frame();
        glfwPollEvents();
        const float dt = benchmark.dt();
        const auto frame = update_frame(input, dt);

        benchmark.begin_scene();
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
                const DrawParameters draw{chart.parameters(row, col), cube_index, irradiance_index};
                batch.draw(chart.sphere, chart.range, draw);
            }
        }

        benchmark.submit(render_executor, std::move(batch), display, final_output_image);
        benchmark.end_frame(window, chart_sphere_count, final_output_image, render_executor);
    }

    render_executor.waitForDeferredResources();
}
