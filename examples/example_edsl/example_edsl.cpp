#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

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
    horizon::HardwareImage texture_image = loadTexture(viking_room_texture_path.string()).texture;

    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        edsl_width, edsl_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_edsl.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    horizon::HardwareImage depth_image(
        horizon::HardwareImageDesc::depth_attachment(edsl_width, edsl_height, horizon::Format::D32, "example_edsl.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    horizon::HardwareBuffer vertex_buffer = horizon::HardwareBuffer::vertex(mesh.vertices, "example_edsl.vertex");
    horizon::HardwareBuffer index_buffer = horizon::HardwareBuffer::index(mesh.indices, "example_edsl.index");

    // 方案 A：构建静态 indirect args buffer（单个 draw command）
    horizon::DrawIndexedIndirectCommand indirect_cmd;
    indirect_cmd.index_count = static_cast<uint32_t>(mesh.indices.size());
    indirect_cmd.instance_count = 1;
    indirect_cmd.first_index = 0;
    indirect_cmd.vertex_offset = 0;
    indirect_cmd.first_instance = 0;

    horizon::HardwareBuffer indirect_args_buffer = horizon::HardwareBuffer::from_bytes(
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(&indirect_cmd),
            sizeof(horizon::DrawIndexedIndirectCommand)),
        static_cast<uint32_t>(sizeof(horizon::DrawIndexedIndirectCommand)),
        horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
        "example_edsl.indirect_args");

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    using namespace EmbeddedShader;
    using namespace ktm;

    Texture2D<fvec4> texture_proxy = texture_image;
    Texture2D<fvec4> final_output_proxy = final_output_image;
    Float4x4 model;
    Float4x4 view;
    Float4x4 proj;

    auto vertex_shader = [&](Aggregate<BaselineEdslVertexProxy> vertex) -> Float4 {
        position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
        Float color_weight = (vertex->color->x + vertex->color->y + vertex->color->z) / Float(3.0f);
        return Float4(vertex->tex_coord, color_weight, 1.0f);
    };

    auto fragment_shader = [&](Float4 input) {
        Float4 color = texture(texture_proxy, input->xy());
        final_output_proxy << color * Float4(input->z, input->z, input->z, 1.0f);
    };

    horizon::RasterizerPipelineDesc desc;

    horizon::RasterizerPipeline rasterizer(vertex_shader, fragment_shader, desc);
    rasterizer.bind_depth_target(depth_image);

    horizon::DrawIndexedParams draw_params;

    HorizonImGuiLayer ui(window, edsl_width, edsl_height);

    // VP 不随帧变化（相机固定），在循环外赋值一次
    view = to_edsl_matrix(
        glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                    glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f)));
    {
        glm::mat4 proj_mat = glm::perspective(glm::pi<float>() * 0.25f,
                                              edsl_width / static_cast<float>(edsl_height),
                                              0.1f, 10.0f);
        proj_mat[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        proj = to_edsl_matrix(proj_mat);
    }

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    horizon::SubmitReceipt render_receipt;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();
        ImGui::Begin("Hello");
        ImGui::Text("hello world!");
        ImGui::End();

        const float time_seconds =
            std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start_time).count();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon Baseline [EDSL] %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // Model 每帧更新（push constant）；VP 已在循环外绑定
        model = to_edsl_matrix(
            glm::rotate(glm::mat4(1.0f),
                        time_seconds * glm::pi<float>() * 0.5f,
                        glm::vec3(0.0f, 0.0f, 1.0f)));

        rasterizer.clear_records();

        // 单次 indirect draw 替代 record()
        horizon::DrawIndexedIndirectParams indirect_params;
        indirect_params.draw_count = 1;
        indirect_params.indirect_offset = 0;
        indirect_params.stride = 0;
        rasterizer.record_indirect(index_buffer, vertex_buffer, indirect_args_buffer, indirect_params);

        render_receipt = render_executor << rasterizer.extent(edsl_width, edsl_height) << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image) << horizon::commit());
    }
    display_executor.wait_idle(render_receipt);
    glfwDestroyWindow(window);
    glfwTerminate();
}
