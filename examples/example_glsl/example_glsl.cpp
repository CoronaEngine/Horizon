#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define HORIZON_NATIVE_WINDOW(window) glfwGetWin32Window(window)
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#define HORIZON_NATIVE_WINDOW(window) glfwGetCocoaView(window)
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "scene_pass.h"
#include "legacy_common.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/baseline_vert.glsl)
#include GLSL(shaders/baseline_frag.glsl)

#include <chrono>
#include <filesystem>
#include <optional>

namespace
{
struct PushConsts {
    glm::mat4 model{};
    uint32_t texIndex{};
};

constexpr uint32_t glsl_width = 800;
constexpr uint32_t glsl_height = 600;

const std::filesystem::path viking_room_model_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "models" / "viking_room.obj";
const std::filesystem::path viking_room_texture_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "textures" / "viking_room.png";
} // namespace

void run_viking_room(bool show_ui)
{
    legacy_example::Window owner(glsl_width, glsl_height, show_ui ? "Horizon Baseline [GLSL]" : "Horizon Baseline");
    auto* window = owner.get();

    baseline::Mesh mesh = baseline::load_mesh(viking_room_model_path);
    HardwareImage texture_image = loadTexture(viking_room_texture_path.string()).texture;

    HardwareImage final_output_image(example::imageInfo(glsl_width, glsl_height, ImageFormat::RGBA16_FLOAT, ImageUsage::SampledStorageImage));
    final_output_image.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    HardwareImage depth_image(
        example::imageInfo(glsl_width, glsl_height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage));

    auto vertex_buffer = HardwareBuffer::uploadDeviceLocal(mesh.vertices, BufferUsage::VertexBuffer);
    auto index_buffer = HardwareBuffer::uploadDeviceLocal(mesh.indices, BufferUsage::IndexBuffer);

    HardwareExecutor render_executor;
    HardwareDisplayer display(HORIZON_NATIVE_WINDOW(window), PresentDevicePreference::RenderDevice);


    example::ScenePass<PushConsts, baseline_vert_glsl_t, baseline_frag_glsl_t> rasterizer(glsl_width, glsl_height, {{true, true, RasterCompare::LessEqual, RasterBlend::Alpha}, {false, false}});
    rasterizer.outColor = final_output_image;
    rasterizer.setDepthImage(depth_image);
    // 纹理存入 bindless combined-texture 表（set 0），拿到索引后经逐 draw 参数传入 shader。
    rasterizer.parameters.texIndex = texture_image.storeDescriptor();

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


    std::optional<HorizonImGuiLayer> ui;
    if (show_ui) ui.emplace(window, glsl_width, glsl_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (ui) {
            ui->new_frame();
            ImGui::Begin("Hello");
            ImGui::Text("hello world!");
            ImGui::End();
        }

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

        // Model 每帧更新（逐 draw 的 BDA 参数）；VP 已在循环外绑定
        rasterizer.parameters.model = glm::rotate(glm::mat4(1.0f),
                                          time_seconds * glm::pi<float>() * 0.5f,
                                          glm::vec3(0.0f, 0.0f, 1.0f));

        rasterizer.draw({vertex_buffer, index_buffer});

        render_executor << rasterizer;
        render_executor.commit();

        if (ui) {
            auto frame = ui->draw_overlay(final_output_image, render_executor);
            display.wait(*frame.executor) << frame.image;
        } else {
            display.wait(render_executor) << final_output_image;
        }
    }
    render_executor.waitForDeferredResources();
}

void run_example_glsl() { run_viking_room(true); }
