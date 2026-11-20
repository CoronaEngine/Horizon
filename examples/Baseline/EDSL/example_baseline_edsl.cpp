#include <GLFW/glfw3.h>

#include "common.h"
#include "../scene.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

#include <bit>
#include <cstdint>
#include <utility>

struct BaselineEdslVertexProxy
{
    EmbeddedShader::Float3 pos;
    EmbeddedShader::Float2 tex_coord;
};

void run_example_baseline_edsl()
{
    using namespace baseline;
    constexpr auto window_title = "Horizon Baseline [EDSL]";
    examples::Window owner(width, height, window_title);
    auto* window = owner.get();

    Scene scene;
    HardwareExecutor render_executor;
    HardwareDisplayer display(owner.native_handle(), PresentDevicePreference::RenderDevice);

    using namespace EmbeddedShader;
    using namespace ktm;

    Texture2D<fvec4> texture_proxy = scene.texture;
    Texture2D<fvec4> final_output_proxy = scene.color;
    Float4x4 model;
    Float4x4 view;
    Float4x4 proj;

    auto vertex_shader = [&](Aggregate<BaselineEdslVertexProxy> vertex) -> Float4 {
        position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
        return Float4(vertex->tex_coord, 1.0f, 1.0f);
    };

    auto fragment_shader = [&](Float4 input) {
        Float4 color = texture(texture_proxy, input->xy());
        final_output_proxy << color * Float4(input->z, input->z, input->z, 1.0f);
    };

    RasterizerPipeline<> rasterizer(vertex_shader, fragment_shader, 1, CompilerOption{.enableMatrixColumnMajor = true});
    rasterizer.setRasterState({.compare = RasterCompare::LessEqual});
    rasterizer.setDepthImage(scene.depth);

    view = std::bit_cast<fmat4x4>(scene.view);
    proj = std::bit_cast<fmat4x4>(scene.projection);

    Animation animation;
    examples::PresentBenchmark benchmark("baseline_edsl");
    while (benchmark.running(window))
    {
        glfwPollEvents();

        model = std::bit_cast<fmat4x4>(animation.next_model(window, window_title));

        rasterizer.syncShaderConditions();
        auto batch = rasterizer.beginBatch(width, height);
        batch.draw(scene.geometry);
        render_executor << std::move(batch);
        benchmark.presented(display.present(render_executor, scene.color));
    }
    render_executor.waitForDeferredResources();
}
