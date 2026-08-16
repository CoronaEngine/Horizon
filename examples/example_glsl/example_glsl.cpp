#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

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

    Corona::Horizon::RasterizerPipelineDesc desc;

    Corona::Horizon::RasterizerPipeline rasterizer(baseline_vert_glsl, baseline_frag_glsl, desc);
    rasterizer.outColor = final_output_image;
    rasterizer.bind_depth_target(depth_image);
    // 纹理存入 bindless combined-texture 表（set 0），拿到索引后经 push constant 传入 shader。
    rasterizer.pc.texIndex = texture_image.store_descriptor();

    // VP 不随帧变化（相机固定），绑定一次即可
    rasterizer.vp.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                                     glm::vec3(0.0f, 0.0f, 0.0f),
                                     glm::vec3(0.0f, 0.0f, 1.0f));
    {
        glm::mat4 proj = glm::perspective(glm::pi<float>() * 0.25f,
                                          glsl_width / static_cast<float>(glsl_height),
                                          0.1f, 10.0f);
        proj[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        rasterizer.vp.proj = proj;
    }

    Corona::Horizon::DrawIndexedParams draw_params;
    draw_params.index_type = Corona::Horizon::IndexType::UInt32;
    draw_params.index_count = static_cast<uint32_t>(mesh.indices.size());

    HorizonImGuiLayer ui(window, glsl_width, glsl_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    Corona::Horizon::SubmitReceipt render_receipt;
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
            std::snprintf(title, sizeof(title), "Horizon Baseline [GLSL] %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // Model 每帧更新（push constant，64 bytes）；VP 已在循环外绑定
        rasterizer.pc.model = glm::rotate(glm::mat4(1.0f),
                                          time_seconds * glm::pi<float>() * 0.5f,
                                          glm::vec3(0.0f, 0.0f, 1.0f));

        rasterizer.clear_records();
        rasterizer.record(index_buffer, vertex_buffer, draw_params);

        render_receipt = render_executor << rasterizer(glsl_width, glsl_height) << Corona::Horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image) << Corona::Horizon::commit());
    }
    display_executor.wait_idle(render_receipt);
    glfwDestroyWindow(window);
    glfwTerminate();
}
