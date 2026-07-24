// 移植自 bgfx examples/17-drawstress（draw call 压测），参照 example_glsl 的
// GLSL 预编译路线。目的：与 bgfx 的单线程提交（面板 Num threads=1）对比
// Horizon record() 逐 draw uniform 快照路径的提交开销。
//
// 与 bgfx 原版对齐的部分：同一套 8 顶点立方体、scale 0.25、step 0.6、
// 逐立方体 rotXYZ(time + xx*0.21, time + yy*0.37, time + zz*0.13)、
// 相机 (0,0,-35) 看原点、fovy 60°。差异：单线程 record（框架现状）、
// 键盘调 dim（bgfx 是 imgui 滑块）、帧率显示在窗口标题。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon_profiling.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/drawstress_vert.glsl)
#include GLSL(shaders/drawstress_frag.glsl)

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace
{
constexpr uint32_t stress_width = 1280;
constexpr uint32_t stress_height = 720;
constexpr int dim_min = 2;
constexpr int dim_max = 40;

struct StressVertex
{
    std::array<float, 3> position;
    std::array<float, 3> color;
};

// bgfx s_cubeVertices 的 abgr 顶点色解码为 float3
const std::vector<StressVertex> cube_vertices = {
    { { -1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } },   // 0xff000000
    { { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },    // 0xff0000ff
    { { -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },  // 0xff00ff00
    { { 1.0f, -1.0f, 1.0f }, { 1.0f, 1.0f, 0.0f } },   // 0xff00ffff
    { { -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },  // 0xffff0000
    { { 1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 1.0f } },   // 0xffff00ff
    { { -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f } }, // 0xffffff00
    { { 1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } },  // 0xffffffff
};

const std::vector<uint32_t> cube_indices = {
    0, 1, 2, 1, 3, 2,
    4, 6, 5, 5, 6, 7,
    0, 2, 4, 4, 2, 6,
    1, 5, 3, 5, 7, 3,
    0, 4, 1, 4, 5, 1,
    2, 3, 6, 6, 3, 7,
};

struct StressState
{
    int dim = 40; // 与 bgfx drawstress 默认一致（64000 draws）
};

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    auto* state = static_cast<StressState*>(glfwGetWindowUserPointer(window));
    switch (key)
    {
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:
        state->dim = std::min(state->dim + 2, dim_max);
        break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
        state->dim = std::max(state->dim - 2, dim_min);
        break;
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    default:
        break;
    }
}

} // namespace

void run_example_drawstress()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(stress_width, stress_height, "Horizon DrawStress [Vulkan]", nullptr, nullptr);

    StressState state;
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, key_callback);
    std::cout << "== example_drawstress controls ==\n"
              << "  + / - : dim +2 / -2 (draws = dim^3)\n"
              << "  Esc   : quit\n";

    Corona::Horizon::HardwareBuffer cube_vb = Corona::Horizon::HardwareBuffer::vertex(cube_vertices, "example_drawstress.vb");
    Corona::Horizon::HardwareBuffer cube_ib = Corona::Horizon::HardwareBuffer::index(cube_indices, "example_drawstress.ib");

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        stress_width, stress_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_drawstress.output"));
    final_output_image.set_clear_color(0.19f, 0.19f, 0.19f, 1.0f);

    Corona::Horizon::HardwareImage depth_image(Corona::Horizon::HardwareImageDesc::depth_attachment(
        stress_width, stress_height, Corona::Horizon::Format::D32, "example_drawstress.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    Corona::Horizon::RasterizerPipelineDesc desc;
    desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline rasterizer(drawstress_vert_glsl, drawstress_frag_glsl, desc);
    rasterizer.outColor = final_output_image;
    rasterizer.bind_depth_target(depth_image);

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    Corona::Horizon::DrawIndexedParams cube_params;
    cube_params.index_type = Corona::Horizon::IndexType::UInt32;
    cube_params.index_count = static_cast<uint32_t>(cube_indices.size());

    constexpr float aspect = static_cast<float>(stress_width) / static_cast<float>(stress_height);
    // bgfx 左手系：mtxLookAt (0,0,-35)->(0,0,0)、mtxProj fovy 60°
    const glm::mat4 view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -35.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    const glm::mat4 view_proj = proj * view;
    const glm::mat4 scale_mtx = glm::scale(glm::mat4(1.0f), glm::vec3(0.25f));

    HorizonImGuiLayer ui(window, stress_width, stress_height);

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
            const int dim = state.dim;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon DrawStress [Vulkan] - dim=%d (%d draws) - %.1f FPS (%.2f ms)",
                          dim, dim * dim * dim, fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        const int dim = state.dim;
        const float step = 0.6f;
        const glm::vec3 base(-step * dim / 2.0f, -step * dim / 2.0f, -15.0f);

        HORIZON_PROFILE_PLOT("draw calls", dim * dim * dim);

        rasterizer.clear_records();
        {
            HORIZON_PROFILE_SCOPE_N("drawstress::build_records");
            for (int zz = 0; zz < dim; ++zz)
            {
                for (int yy = 0; yy < dim; ++yy)
                {
                    for (int xx = 0; xx < dim; ++xx)
                    {
                        // 等价于 bgfx 的 mtxScale(0.25) × mtxRotateXYZ(...)（行向量约定），
                        // 换到 glm 列向量为 Rz·Ry·Rx·S，再直接写入平移列。
                        glm::mat4 model = glm::eulerAngleZYX(time + zz * 0.13f, time + yy * 0.37f, time + xx * 0.21f) * scale_mtx;
                        model[3] = glm::vec4(base + glm::vec3(xx * step, yy * step, zz * step), 1.0f);

                        rasterizer.model_pc.mvp = view_proj * model;
                        rasterizer.record(cube_ib, cube_vb, cube_params);
                    }
                }
            }
        }

        Corona::Horizon::SubmitReceipt render_receipt;
        {
            HORIZON_PROFILE_SCOPE_N("drawstress::submit");
            render_receipt = render_executor << rasterizer(stress_width, stress_height) << Corona::Horizon::submit;
        }

        {
            HORIZON_PROFILE_SCOPE_N("drawstress::wait_present");
            ui.draw_overlay(display_executor, final_output_image, render_receipt);
            display_executor.wait(render_receipt);
            (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                             << Corona::Horizon::commit());
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
