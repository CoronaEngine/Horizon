#include <GLFW/glfw3.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "../scene.h"

#include SLANG(Baseline/Slang/baseline_vert.slang)
#include SLANG(Baseline/Slang/baseline_frag.slang)

#include <utility>

namespace
{
struct DrawParameters {
    glm::mat4 model{};
    uint32_t texIndex{};
};

} // namespace

void run_example_baseline_slang()
{
    using namespace baseline;
    constexpr auto window_title = "Horizon Baseline [Slang]";
    examples::Window owner(width, height, window_title);
    auto* window = owner.get();

    Scene scene;
    HardwareExecutor render_executor;
    HardwareDisplayer display(owner.native_handle(), PresentDevicePreference::RenderDevice);

    RasterizerPipeline<baseline_vert_slang_t, baseline_frag_slang_t> rasterizer;
    rasterizer.setRasterState({.compare = RasterCompare::LessEqual});
    rasterizer.outColor = scene.color;
    rasterizer.setDepthImage(scene.depth);
    // 纹理存入 bindless combined-texture 表（set 0），拿到索引后经逐 draw 参数传入 shader。
    DrawParameters parameters{};
    parameters.texIndex = scene.texture.storeDescriptor();

    rasterizer.vp.view = scene.view;
    rasterizer.vp.proj = scene.projection;

    Animation animation;
    examples::PresentBenchmark benchmark("baseline_slang");
    while (benchmark.running(window))
    {
        glfwPollEvents();

        parameters.model = animation.next_model(window, window_title);

        auto batch = rasterizer.beginBatch(width, height);
        batch.draw(scene.geometry, {}, parameters);
        render_executor << std::move(batch);
        benchmark.presented(display.present(render_executor, scene.color));
    }
    render_executor.waitForDeferredResources();
}
