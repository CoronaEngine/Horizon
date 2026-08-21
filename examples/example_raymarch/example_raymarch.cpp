// 移植自参考示例 03-raymarch（全屏 SDF 光线步进），参照 example_glsl 的
// GLSL 预编译路线。与原版对齐：相机 (0,0,-15) 看原点 fovy 60°、场景随
// rotXY(time, time*0.37) 旋转、光方向 (-0.4,-0.5,-1) 变换到模型空间、四角
// 渐变背景（红/绿/蓝/白）。差异：全屏四边形直接给 NDC 坐标（省掉 原版的
// ortho pass），帧率显示在窗口标题。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/raymarch_vert.glsl)
#include GLSL(shaders/raymarch_frag.glsl)

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace
{
constexpr uint32_t rm_width = 1280;
constexpr uint32_t rm_height = 720;

struct QuadVertex
{
    std::array<float, 3> position;
    std::array<float, 3> color;
    std::array<float, 2> uv;
};

// 全屏四边形（NDC，Vulkan y 向下）。四角颜色对齐 原版：
// 左上红、右上绿、右下蓝、左下白。uv 直接取 NDC xy 供 inv_mvp 反投影。
const std::vector<QuadVertex> quad_vertices = {
    { { -1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { -1.0f, -1.0f } }, // 左上
    { { 1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, -1.0f } },   // 右上
    { { 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },     // 右下
    { { -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { -1.0f, 1.0f } },   // 左下
};

const std::vector<uint16_t> quad_indices = { 0, 2, 1, 0, 3, 2 };

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

void run_example_raymarch()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(rm_width, rm_height, "Horizon Raymarch [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    horizon::HardwareBuffer quad_vb = horizon::HardwareBuffer::vertex(quad_vertices, "example_raymarch.vb");
    horizon::HardwareBuffer quad_ib = horizon::HardwareBuffer::index(quad_indices, "example_raymarch.ib");

    // 方案 A：构建静态 indirect args buffer（单个 draw command）
    horizon::DrawIndexedIndirectCommand indirect_cmd;
    indirect_cmd.index_count = static_cast<uint32_t>(quad_indices.size());
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
        "example_raymarch.indirect_args");


    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        rm_width, rm_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_raymarch.output"));
    final_output_image.set_clear_color(0.19f, 0.19f, 0.19f, 1.0f);

    horizon::HardwareImage depth_image(horizon::HardwareImageDesc::depth_attachment(
        rm_width, rm_height, horizon::Format::D32, "example_raymarch.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    horizon::RasterizerPipelineDesc desc;
    desc.blend_enabled = false;

    horizon::RasterizerPipeline rasterizer(raymarch_vert_glsl, raymarch_frag_glsl, desc);
    rasterizer.outColor = final_output_image;
    rasterizer.bind_depth_target(depth_image);

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));


    constexpr float aspect = static_cast<float>(rm_width) / static_cast<float>(rm_height);
    // 原版左手系：mtxLookAt (0,0,-15)->(0,0,0)、mtxProj fovy 60°
    const glm::mat4 view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -15.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();

    HorizonImGuiLayer ui(window, rm_width, rm_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();
        ImGui::Begin("Hello");
        ImGui::Text("hello world!");
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        const float time = std::chrono::duration<float>(now - start_time).count();
        prev_time = now;

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[128];
            std::snprintf(title, sizeof(title), "Horizon Raymarch [Vulkan] - %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // 原版 mtxRotateXY(time, time*0.37)（行向量 Rx·Ry）→ glm 列向量 Ry·Rx，
        // 且行/列向量旋转矩阵互为转置，等效角度取反
        const glm::mat4 model = glm::eulerAngleYX(-time * 0.37f, -time);
        const glm::mat4 mvp = proj * view * model;
        const glm::mat4 inv_mvp = glm::inverse(mvp);

        // 世界空间光方向变换到模型空间（SDF 法线在模型空间求值）
        const glm::vec3 light_world = glm::normalize(glm::vec3(-0.4f, -0.5f, -1.0f));
        const glm::vec3 light_model = glm::normalize(glm::vec3(glm::inverse(model) * glm::vec4(light_world, 0.0f)));

        rasterizer.clear_records();
        rasterizer.rmp.inv_mvp = inv_mvp;
        rasterizer.rmp.light_dir_time = glm::vec4(light_model, time);

        // 单次 indirect draw 替代 record()
        horizon::DrawIndexedIndirectParams indirect_params;
        indirect_params.draw_count = 1;
        indirect_params.indirect_offset = 0;
        indirect_params.stride = 0;
        rasterizer.record_indirect(quad_ib, quad_vb, indirect_args_buffer, indirect_params);

        horizon::SubmitReceipt render_receipt =
            render_executor << rasterizer.extent(rm_width, rm_height) << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
