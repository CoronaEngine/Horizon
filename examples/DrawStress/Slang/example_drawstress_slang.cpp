#include "../scene.h"
#include "common.h"
#include "Horizon.h"
#include "horizon_profiling.h"
#include "Codegen/ControlFlows.h"

#include SLANG(DrawStress/Slang/drawstress_vert.slang)
#include SLANG(DrawStress/Slang/drawstress_frag.slang)

#include <GLFW/glfw3.h>
#include <chrono>
#include <utility>

void run_example_drawstress_slang()
{
    using namespace drawstress;
    constexpr auto mode = "drawstress_slang";
    examples::Window window_owner(stress_width, stress_height, mode);
    GLFWwindow* window = window_owner.get();

    StressState state;
    attach_input(window, state, mode);

    CubeGrid grid;

    HardwareImage final_output_image(stress_width, stress_height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    final_output_image.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);

    HardwareImage depth_image(stress_width, stress_height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage);

    const auto shader_start = std::chrono::steady_clock::now();
    RasterizerPipeline<drawstress_vert_slang_t, drawstress_frag_slang_t> rasterizer;
    const double shader_setup_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shader_start).count();
    rasterizer.outColor = final_output_image;
    rasterizer.setDepthImage(depth_image);

    HardwareExecutor render_executor;
    HardwareDisplayer display(window_owner.native_handle(), PresentDevicePreference::RenderDevice);

    examples::Benchmark benchmark(mode, stress_width, stress_height);
    benchmark.set_shader_setup_ms(shader_setup_ms);
    while (benchmark.running(window))
    {
        benchmark.begin_frame();
        glfwPollEvents();

        benchmark.begin_scene();
        const float time = benchmark.time();
        auto batch = rasterizer.beginBatch(stress_width, stress_height);
        {
            HORIZON_PROFILE_SCOPE_N("drawstress::submit");
            grid.draw(batch, state, time);
            benchmark.submit(render_executor, std::move(batch), display, final_output_image);
        }
        benchmark.end_frame(window, state.draw_count(), final_output_image, render_executor);
    }

    render_executor.waitForDeferredResources();
}
