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

Corona::Horizon::HardwareImage create_output_image()
{
    Corona::Horizon::HardwareImage image(
        Corona::Horizon::HardwareImageDesc::texture_2d(glsl_width,
                                         glsl_height,
                                         Corona::Horizon::Format::RGBA16_FLOAT,
                                         Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
                                             Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
                                             Corona::Horizon::ImageUsageFlags::TransferDst,
                                         "example_glsl.output"));
    image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    return image;
}

Corona::Horizon::HardwareImage create_depth_image()
{
    Corona::Horizon::HardwareImage image(
        Corona::Horizon::HardwareImageDesc::depth_attachment(glsl_width, glsl_height, Corona::Horizon::Format::D32, "example_glsl.depth"));
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

        Corona::Horizon::HardwareImage texture_image = texture_result.texture;
        Corona::Horizon::HardwareImage final_output_image = create_output_image();
        Corona::Horizon::HardwareImage depth_image = create_depth_image();
        Corona::Horizon::HardwareBuffer vertex_buffer = Corona::Horizon::HardwareBuffer::vertex(mesh.vertices, "example_glsl.vertex");
        Corona::Horizon::HardwareBuffer index_buffer = Corona::Horizon::HardwareBuffer::index(mesh.indices, "example_glsl.index");
        Corona::Horizon::HardwareExecutor render_executor;
        Corona::Horizon::HardwareExecutor display_executor;
        Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

        // 复用自动生成的反射模块创建管线，对齐 example_default 的 GLSL 路径。
        Corona::Horizon::RasterizerPipelineDesc rasterizer_desc(
            Corona::Horizon::PipelineShaderDesc::from_slang_module(Corona::Horizon::PipelineShaderStage::Vertex, baseline_vert_glsl::slangModule),
            Corona::Horizon::PipelineShaderDesc::from_slang_module(Corona::Horizon::PipelineShaderStage::Fragment, baseline_frag_glsl::slangModule));
        rasterizer_desc.set_depth_attachment(Corona::Horizon::DepthAttachmentDesc::with_format(Corona::Horizon::Format::D32, "example_glsl.depth"));
        rasterizer_desc.set_debug_name("example_glsl.baseline_rasterizer");

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

            Corona::Horizon::SubmitReceipt render_receipt = render_executor
                << rasterizer(glsl_width, glsl_height)
                << Corona::Horizon::submit;

            display_executor.wait(render_receipt);
            (void)(display_executor.stream()
                << Corona::Horizon::present(display, final_output_image)
                << Corona::Horizon::commit());
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
