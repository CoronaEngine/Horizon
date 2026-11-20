#include "../scene.h"
#include "common.h"
#include "Horizon.h"
#include "horizon_profiling.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

#include <GLFW/glfw3.h>
#include <chrono>
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
    examples::Window window_owner(stress_width, stress_height, mode);
    GLFWwindow* window = window_owner.get();

    StressState state;
    attach_input(window, state, mode);

    CubeGrid grid;

    HardwareImage final_output_image(stress_width, stress_height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    final_output_image.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);

    HardwareImage depth_image(stress_width, stress_height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage);

    const auto shader_start = std::chrono::steady_clock::now();
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
    const double shader_setup_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shader_start).count();
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
        output = final_output_image;
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
