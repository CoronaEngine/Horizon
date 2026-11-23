#include "Horizon.h"
#include <GLFW/glfw3.h>

#include "Codegen/ControlFlows.h"
#include "window.h"
#include "example_benchmark/benchmark.h"
#include "../assets.h"

#include SLANG(Baseline/Slang/baseline_vert.slang)
#include SLANG(Baseline/Slang/baseline_frag.slang)

#include <glm/gtc/matrix_transform.hpp>
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
    constexpr int width = 1920, height = 1080;
    constexpr auto window_title = "Horizon Baseline [Slang]";
    auto window = examples::open_window(width, height, window_title);

    auto geometry = baseline::load_model("models/viking_room.obj");
    auto texture_image = baseline::load_texture("textures/viking_room.png");

    HardwareImageCreateInfo color_info(width, height, ImageFormat::RGBA16_FLOAT);
    color_info.usage = ImageUsage::SampledStorageImage;
    color_info.linearFiltering = true;
    HardwareImage color(color_info);
    color.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    const auto camera_view = glm::lookAt(glm::vec3(2.0f), glm::vec3(0.0f), glm::vec3(0, 0, 1));
    auto projection = glm::perspective(glm::radians(45.0f), float(width) / height, 0.1f, 10.0f);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.

    HardwareExecutor render_executor;
    HardwareDisplayer display(examples::native_handle(window.get()), PresentDevicePreference::RenderDevice);

    RasterizerPipeline<baseline_vert_slang_t, baseline_frag_slang_t> rasterizer;
    rasterizer.setRasterState({.compare = RasterCompare::LessEqual});
    rasterizer.outColor = color;
    // 纹理存入 bindless combined-texture 表（set 0），拿到索引后经逐 draw 参数传入 shader。
    DrawParameters parameters{};
    parameters.texIndex = texture_image.storeDescriptor();

    rasterizer.vp.view = camera_view;
    rasterizer.vp.proj = projection;

    const double start = glfwGetTime();
    auto samples = bench::present_samples("baseline_slang", window_title);
    while (!glfwWindowShouldClose(window.get()))
    {
        glfwPollEvents();
        const float seconds = static_cast<float>(glfwGetTime() - start);
        const auto rotation = glm::rotate(glm::mat4(1.0f), seconds * glm::radians(90.0f), glm::vec3(0, 0, 1));

        parameters.model = rotation;

        auto batch = rasterizer.beginBatch();
        batch.draw(geometry, {}, parameters);
        render_executor << std::move(batch);
        const bool presented = display.present(render_executor, color);
        bench::observe_present(samples, window.get(), presented);
    }
    render_executor.waitForDeferredResources();
}
