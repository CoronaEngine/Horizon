#include "Horizon.h"
#include <GLFW/glfw3.h>

#include "window.h"
#include "example_benchmark/benchmark.h"
#include "../assets.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

#include <bit>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <utility>

struct BaselineEdslVertexProxy
{
    EmbeddedShader::Float3 pos;
    EmbeddedShader::Float2 tex_coord;
};

void run_example_baseline_edsl()
{
    constexpr int width = 1920, height = 1080;
    constexpr auto window_title = "Horizon Baseline [EDSL]";
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

    using namespace EmbeddedShader;
    using namespace ktm;

    Texture2D<fvec4> texture_proxy = texture_image;
    Texture2D<fvec4> final_output_proxy = color;
    Float4x4 model;
    Float4x4 view;
    Float4x4 proj;

    auto vertex_shader = [&](Aggregate<BaselineEdslVertexProxy> vertex) -> Float4 {
        position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
        return Float4(vertex->tex_coord, 1.0f, 1.0f);
    };

    auto fragment_shader = [&](Float4 input) {
        Float4 sampled_color = texture(texture_proxy, input->xy());
        final_output_proxy << sampled_color * Float4(input->z, input->z, input->z, 1.0f);
    };

    RasterizerPipeline<> rasterizer(vertex_shader, fragment_shader, 1, CompilerOption{.enableMatrixColumnMajor = true});
    rasterizer.setRasterState({.compare = RasterCompare::LessEqual});

    view = std::bit_cast<fmat4x4>(camera_view);
    proj = std::bit_cast<fmat4x4>(projection);

    const double start = glfwGetTime();
    auto samples = bench::present_samples("baseline_edsl", window_title);
    while (!glfwWindowShouldClose(window.get()))
    {
        glfwPollEvents();
        const float seconds = static_cast<float>(glfwGetTime() - start);
        const auto rotation = glm::rotate(glm::mat4(1.0f), seconds * glm::radians(90.0f), glm::vec3(0, 0, 1));

        model = std::bit_cast<fmat4x4>(rotation);

        auto batch = rasterizer.beginBatch();
        batch.draw(geometry);
        render_executor << std::move(batch);
        const bool presented = display.present(render_executor, color);
        bench::observe_present(samples, window.get(), presented);
    }
    render_executor.waitForDeferredResources();
}
