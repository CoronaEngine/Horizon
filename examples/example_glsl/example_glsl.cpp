#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"

// 经 CMake helicon_compile_shaders 自动生成的反射头：提供 baseline_vert_glsl /
// baseline_frag_glsl 命名空间下的 slangModule 与绑定符号，免去手写 BindingSlot。
#include GLSL(shaders/baseline_vert.glsl)
#include GLSL(shaders/baseline_frag.glsl)

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace H = Corona::Horizon;

namespace
{
constexpr uint32_t glsl_width = 800;
constexpr uint32_t glsl_height = 600;

const std::filesystem::path viking_room_model_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "models" / "viking_room.obj";
const std::filesystem::path viking_room_texture_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "textures" / "viking_room.png";

void check_assets()
{
    // shader 已在构建期经 helicon_compile_shaders 编入二进制，运行期只需校验模型 / 纹理。
    if (std::filesystem::exists(viking_room_model_path) &&
        std::filesystem::exists(viking_room_texture_path))
    {
        return;
    }

    std::string message = "GLSL baseline assets are missing.";
    if (!std::filesystem::exists(viking_room_model_path))
        message += "\n  Missing model: " + viking_room_model_path.string();
    if (!std::filesystem::exists(viking_room_texture_path))
        message += "\n  Missing texture: " + viking_room_texture_path.string();
    throw std::runtime_error(message);
}

baseline::UniformBufferObject make_ubo(float time_seconds)
{
    return baseline::make_ubo(time_seconds, glsl_width / static_cast<float>(glsl_height));
}

H::HardwareImage create_output_image()
{
    H::HardwareImage image(
        H::HardwareImageDesc::texture_2d(glsl_width,
                                         glsl_height,
                                         H::Format::RGBA16_FLOAT,
                                         H::ImageUsageFlags::Storage | H::ImageUsageFlags::ColorAttachment |
                                             H::ImageUsageFlags::Sampled | H::ImageUsageFlags::TransferSrc |
                                             H::ImageUsageFlags::TransferDst,
                                         "example_glsl.output"));
    image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    return image;
}

H::HardwareImage create_depth_image()
{
    H::HardwareImage image(
        H::HardwareImageDesc::depth_attachment(glsl_width, glsl_height, H::Format::D32, "example_glsl.depth"));
    image.set_clear_depth(1.0f, 0);
    return image;
}
} // namespace

void run_example_glsl()
{
    check_assets();

    if (glfwInit() < 0)
        return;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(glsl_width, glsl_height, "Horizon Baseline [GLSL]", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return;
    }

    try
    {
        baseline::Mesh mesh = baseline::load_mesh(viking_room_model_path);
        TextureLoadResult texture_result = loadTexture(viking_room_texture_path.string());
        if (!texture_result.success)
            throw std::runtime_error("failed to load GLSL baseline texture: " + viking_room_texture_path.string());

        H::HardwareImage texture_image = texture_result.texture;
        H::HardwareImage final_output_image = create_output_image();
        H::HardwareImage depth_image = create_depth_image();
        H::HardwareBuffer vertex_buffer = H::HardwareBuffer::vertex(mesh.vertices, "example_glsl.vertex");
        H::HardwareBuffer index_buffer = H::HardwareBuffer::index(mesh.indices, "example_glsl.index");
        H::HardwareExecutor render_executor;
        H::HardwareExecutor display_executor;
        H::HardwareDisplayer display(glfwGetWin32Window(window));

        // 复用自动生成的反射模块创建管线，对齐 example_default 的 GLSL 路径。
        H::RasterizerPipelineDesc rasterizer_desc(
            H::PipelineShaderDesc::from_slang_module(H::PipelineShaderStage::Vertex, baseline_vert_glsl::slangModule),
            H::PipelineShaderDesc::from_slang_module(H::PipelineShaderStage::Fragment, baseline_frag_glsl::slangModule));
        rasterizer_desc.set_depth_attachment(H::DepthAttachmentDesc::with_format(H::Format::D32, "example_glsl.depth"));
        rasterizer_desc.set_debug_name("example_glsl.baseline_rasterizer");

        H::RasterizerPipeline rasterizer(std::move(rasterizer_desc));
        rasterizer[baseline_frag_glsl::outColor] = final_output_image;
        rasterizer.bind_depth_target(depth_image);
        rasterizer[baseline_frag_glsl::texSampler] = texture_image;

        H::DrawIndexedParams draw_params;
        draw_params.index_type = H::IndexType::UInt32;
        draw_params.index_count = static_cast<uint32_t>(mesh.indices.size());

        const auto start_time = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            const float time_seconds =
                std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - start_time)
                    .count();
            baseline::UniformBufferObject ubo = make_ubo(time_seconds);
            // UBO 成员通过反射符号绑定（块实例名 ubo），无需手写 byte_offset/type_size。
            rasterizer[baseline_vert_glsl::ubo::model] = ubo.model;
            rasterizer[baseline_vert_glsl::ubo::view] = ubo.view;
            rasterizer[baseline_vert_glsl::ubo::proj] = ubo.proj;

            rasterizer.clear_records();
            rasterizer.record(index_buffer, vertex_buffer, draw_params);

            H::SubmitReceipt render_receipt = render_executor
                << rasterizer(glsl_width, glsl_height)
                << H::submit;

            display_executor.wait(render_receipt);
            (void)(display_executor.stream()
                << H::present(display, final_output_image)
                << H::commit());
        }
    }
    catch (...)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        throw;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
