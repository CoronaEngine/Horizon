#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"

#include GLSL(shaders/baseline_vert.glsl)
#include GLSL(shaders/baseline_frag.glsl)

#include <chrono>
#include <filesystem>
#include <utility>

namespace
{
constexpr uint32_t glsl_width = 800;
constexpr uint32_t glsl_height = 600;

const std::filesystem::path viking_room_model_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "models" / "viking_room.obj";
const std::filesystem::path viking_room_texture_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "textures" / "viking_room.png";
} // namespace

void run_example_glsl()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(glsl_width, glsl_height, "Horizon Baseline [GLSL]", nullptr, nullptr);

    baseline::Mesh mesh = baseline::load_mesh(viking_room_model_path);
    Corona::Horizon::HardwareImage texture_image = loadTexture(viking_room_texture_path.string()).texture;

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        glsl_width, glsl_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_glsl.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    Corona::Horizon::HardwareImage depth_image(
        Corona::Horizon::HardwareImageDesc::depth_attachment(glsl_width, glsl_height, Corona::Horizon::Format::D32, "example_glsl.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    Corona::Horizon::HardwareBuffer vertex_buffer = Corona::Horizon::HardwareBuffer::vertex(mesh.vertices, "example_glsl.vertex");
    Corona::Horizon::HardwareBuffer index_buffer = Corona::Horizon::HardwareBuffer::index(mesh.indices, "example_glsl.index");
    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    Corona::Horizon::RasterizerPipelineDesc rasterizer_desc(
        Corona::Horizon::PipelineShaderDesc::from_slang_module(Corona::Horizon::PipelineShaderStage::Vertex, baseline_vert_glsl::slangModule),
        Corona::Horizon::PipelineShaderDesc::from_slang_module(Corona::Horizon::PipelineShaderStage::Fragment, baseline_frag_glsl::slangModule));
    rasterizer_desc.set_depth_attachment(Corona::Horizon::DepthAttachmentDesc::with_format(Corona::Horizon::Format::D32, "example_glsl.depth"));

    Corona::Horizon::RasterizerPipeline rasterizer(std::move(rasterizer_desc));
    rasterizer[baseline_frag_glsl::outColor] = final_output_image;
    rasterizer.bind_depth_target(depth_image);
    rasterizer[baseline_frag_glsl::texSampler] = texture_image;

    Corona::Horizon::DrawIndexedParams draw_params;
    draw_params.index_type = Corona::Horizon::IndexType::UInt32;
    draw_params.index_count = static_cast<uint32_t>(mesh.indices.size());

    const auto start_time = std::chrono::high_resolution_clock::now();
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        const float time_seconds =
            std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start_time).count();
        baseline::UniformBufferObject ubo = baseline::make_ubo(time_seconds, glsl_width / static_cast<float>(glsl_height));
        rasterizer[baseline_vert_glsl::ubo::model] = ubo.model;
        rasterizer[baseline_vert_glsl::ubo::view] = ubo.view;
        rasterizer[baseline_vert_glsl::ubo::proj] = ubo.proj;

        rasterizer.clear_records();
        rasterizer.record(index_buffer, vertex_buffer, draw_params);

        Corona::Horizon::SubmitReceipt render_receipt = render_executor << rasterizer(glsl_width, glsl_height) << Corona::Horizon::submit;

        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image) << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
