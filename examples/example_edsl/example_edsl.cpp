#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include GLSL(shaders/edsl_header.glsl)

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <utility>

struct BaselineEdslVertexProxy
{
    EmbeddedShader::Float3 pos;
    EmbeddedShader::Float3 color;
    EmbeddedShader::Float2 tex_coord;
};

namespace
{
constexpr uint32_t edsl_width = 800;
constexpr uint32_t edsl_height = 600;

const std::filesystem::path viking_room_model_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "models" / "viking_room.obj";
const std::filesystem::path viking_room_texture_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "textures" / "viking_room.png";

ktm::fmat4x4 to_edsl_matrix(const glm::mat4& matrix)
{
    static_assert(sizeof(ktm::fmat4x4) == sizeof(glm::mat4));

    ktm::fmat4x4 result;
    std::memcpy(&result, &matrix, sizeof(result));
    return result;
}
} // namespace

void run_example_edsl()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(edsl_width, edsl_height, "Horizon Baseline [EDSL]", nullptr, nullptr);

    baseline::Mesh mesh = baseline::load_mesh(viking_room_model_path);
    Corona::Horizon::HardwareImage texture_image = loadTexture(viking_room_texture_path.string()).texture;

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        edsl_width, edsl_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_edsl.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    Corona::Horizon::HardwareImage depth_image(
        Corona::Horizon::HardwareImageDesc::depth_attachment(edsl_width, edsl_height, Corona::Horizon::Format::D32, "example_edsl.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    Corona::Horizon::HardwareBuffer vertex_buffer = Corona::Horizon::HardwareBuffer::vertex(mesh.vertices, "example_edsl.vertex");
    Corona::Horizon::HardwareBuffer index_buffer = Corona::Horizon::HardwareBuffer::index(mesh.indices, "example_edsl.index");
    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    using namespace EmbeddedShader;
    using namespace ktm;

    Texture2D<fvec4> texture_proxy = texture_image;
    Texture2D<fvec4> final_output_proxy = final_output_image;
    Float4x4 model;
    Float4x4 view;
    Float4x4 proj;

    bool option = false;

    auto vertex_shader = [&](Aggregate<BaselineEdslVertexProxy> vertex) -> Float4 {
        position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
        Float color_weight = edsl_header_glsl.get_color_weight(vertex->color);
        return Float4(vertex->tex_coord, color_weight, 1.0f);
    };

    auto fragment_shader = [&](Float4 input) {
        Float4 color = texture(texture_proxy, input->xy());
        Float4 finalColor;
        $IF (option)
        {
            finalColor = color * Float4(input->z, input->z, input->z, 1.0f);;
        }
        $ELSE
        {
            finalColor = Float4(input->x, input->y, input->z, 1.0f);
        }
        final_output_proxy << finalColor;
    };

    Corona::Horizon::RasterizerPipelineDesc desc;
    desc.depth_attachment = Corona::Horizon::DepthAttachmentDesc::with_format(Corona::Horizon::Format::D32, "example_edsl.depth");

    Corona::Horizon::RasterizerPipeline rasterizer(vertex_shader, fragment_shader, desc);
    rasterizer.bind_depth_target(depth_image);

    Corona::Horizon::DrawIndexedParams draw_params;
    draw_params.index_type = Corona::Horizon::IndexType::UInt32;
    draw_params.index_count = static_cast<uint32_t>(mesh.indices.size());

    //option = true;
    float t = 0.f;
    const auto start_time = std::chrono::high_resolution_clock::now();
    auto last_time = std::chrono::high_resolution_clock::now();
    std::cout << std::boolalpha;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        const float time_seconds =
            std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start_time).count();
        auto now = std::chrono::high_resolution_clock::now();
        t += std::chrono::duration<float>(now - last_time).count();
        last_time = now;
        if (t >= 1.f)
        {
            t -= 1.f;
            option = !option;
        }
        baseline::UniformBufferObject ubo = baseline::make_ubo(time_seconds, edsl_width / static_cast<float>(edsl_height));
        model = to_edsl_matrix(glm::transpose(ubo.model));
        view = to_edsl_matrix(glm::transpose(ubo.view));
        proj = to_edsl_matrix(glm::transpose(ubo.proj));

        rasterizer.clear_records();
        rasterizer.record(index_buffer, vertex_buffer, draw_params);

        Corona::Horizon::SubmitReceipt render_receipt = render_executor << rasterizer(edsl_width, edsl_height) << Corona::Horizon::submit;

        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image) << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
