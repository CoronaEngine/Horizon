// Legacy baseline: same scene, direct indexed draws; no RHI changes.
// 移植自 bgfx examples/17-drawstress（draw call 压测），参照 example_glsl 的
// GLSL 预编译路线。目的：与 bgfx 的单线程提交（面板 Num threads=1）对比
// Horizon record() 逐 draw push constant 录制路径的提交开销。
//
// 与 bgfx 原版对齐的部分：同一套 8 顶点立方体、scale 0.25、step 0.6、
// 逐立方体 rotXYZ(time + xx*0.21, time + yy*0.37, time + zz*0.13)、
// 相机 (0,0,-35) 看原点、fovy 60°。差异：单线程 record（框架现状）、
// 键盘调 dim（bgfx 是 imgui 滑块）、帧率显示在窗口标题。

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
#include "legacy_common.h"
#include "Horizon_old.h"
#include "horizon_profiling.h"  // 内部头（src/），埋点需在 CMake 里 opt-in
#include "imgui_horizon_old.h"

#include <imgui.h>

#include GLSL(shaders/drawstress_old_vert.glsl)
#include GLSL(shaders/drawstress_old_frag.glsl)

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
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

const std::vector<uint16_t> cube_indices = {
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

void run_example_drawstress_old()
{
    legacy_example::Window window_owner(stress_width, stress_height, "Horizon drawstress_old [Legacy]");
    GLFWwindow* window = window_owner.get();

    StressState state;
    state.dim = std::clamp(legacy_example::env_int("HORIZON_DRAWSTRESS_DIM", state.dim), dim_min, dim_max);
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, key_callback);
    std::cout << "== example_drawstress controls ==\n"
              << "  + / - : dim +2 / -2 (draws = dim^3)\n"
              << "  Esc   : quit\n";

    // Separate storage/vertex buffers avoid extending Legacy BufferUsage flags.
    HardwareBuffer cube_vb(cube_vertices, BufferUsage::VertexBuffer);
    HardwareBuffer vertex_ssbo(cube_vertices, BufferUsage::StorageBuffer);
    HardwareBuffer cube_ib = HardwareBuffer(cube_indices, BufferUsage::IndexBuffer);

    HardwareImage final_output_image(stress_width, stress_height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    final_output_image.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);

    HardwareImage depth_image(stress_width, stress_height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage);

    RasterizerPipeline<drawstress_old_vert_glsl_t, drawstress_old_frag_glsl_t> rasterizer;
    rasterizer.outColor = final_output_image;
    rasterizer.setDepthImage(depth_image);

    // per-draw MVP SSBO. Initial capacity 64000 mat4 = 4MB.
    // Always allocate max capacity to avoid recreating the buffer (which invalidates descriptors).
    struct PerDrawData
    {
        glm::mat4 mvp;
    };
    constexpr size_t max_draws = dim_max * dim_max * dim_max;
    HardwareBuffer per_draw_ssbo(static_cast<uint32_t>(max_draws), sizeof(PerDrawData), BufferUsage::StorageBuffer);
    // Get bindless descriptor index once at initialization
    const uint32_t ssbo_descriptor_index = per_draw_ssbo.storeDescriptor();
    // 顶点表也注册进 bindless,顶点阶段靠 push constant 里的 vertex_ssbo_index 选表。
    const uint32_t vertex_descriptor_index = vertex_ssbo.storeDescriptor();
    std::cout << "[DEBUG] SSBO descriptor_index=" << ssbo_descriptor_index
              << ", vertex descriptor_index=" << vertex_descriptor_index
              << ", capacity=" << max_draws << std::endl;

    HardwareExecutor render_executor;
    HardwareDisplayer display(HORIZON_NATIVE_WINDOW(window));


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

    HorizonImGuiLayerOld ui(window, stress_width, stress_height);

    legacy_example::Benchmark benchmark("drawstress_old", stress_width, stress_height);
    while (!glfwWindowShouldClose(window))
    {
        benchmark.begin_frame();
        // Legacy owns one mutable UBO/SSBO: wait before the next host write.
        render_executor.waitForDeferredResources();
        glfwPollEvents();
        ui.new_frame();
        ImGui::Begin("Hello");
        ImGui::Text("hello world!");
        ImGui::End();

        const float time = benchmark.time();
        const int dim = state.dim;
        const float step = 0.6f;
        const glm::vec3 base(-step * dim / 2.0f, -step * dim / 2.0f, -15.0f);

        HORIZON_PROFILE_PLOT("draw calls", dim * dim * dim);

        // build per-draw MVP array and upload to SSBO.
        std::vector<PerDrawData> per_draw_array;
        per_draw_array.reserve(dim * dim * dim);


        {
            HORIZON_PROFILE_SCOPE_N("drawstress_old::build_records");
            for (int zz = 0; zz < dim; ++zz)
            {
                for (int yy = 0; yy < dim; ++yy)
                {
                    for (int xx = 0; xx < dim; ++xx)
                    {
                        glm::mat4 model = glm::eulerAngleZYX(time + zz * 0.13f, time + yy * 0.37f, time + xx * 0.21f) * scale_mtx;
                        model[3] = glm::vec4(base + glm::vec3(xx * step, yy * step, zz * step), 1.0f);

                        per_draw_array.push_back({ view_proj * model });
                    }
                }
            }

            // Upload per-draw data to SSBO (buffer was pre-allocated at max capacity).
            const size_t upload_size = per_draw_array.size();

            if (!per_draw_ssbo.copyFromData(per_draw_array.data(), per_draw_array.size() * sizeof(PerDrawData)))
                throw std::runtime_error("Failed to upload per-draw SSBO");

            DrawIndexedParams params;
            params.indexCount = static_cast<uint32_t>(cube_indices.size());
            params.indexType = IndexType::UInt16;
            for (uint32_t i = 0; i < static_cast<uint32_t>(upload_size); ++i) {
                rasterizer.model_pc.per_draw_ssbo_index = ssbo_descriptor_index;
                rasterizer.model_pc.vertex_ssbo_index = vertex_descriptor_index;
                rasterizer.model_pc.draw_index = i;
                rasterizer.record(cube_ib, cube_vb, params);
            }
        }

        {
            HORIZON_PROFILE_SCOPE_N("drawstress_old::submit");
            render_executor << rasterizer(stress_width, stress_height);
            render_executor.commit();
        }
        auto frame_output = ui.draw_overlay(final_output_image, render_executor);
        display.wait(*frame_output.executor) << frame_output.image;
        benchmark.end_frame(window, static_cast<uint64_t>(dim) * dim * dim,
                            frame_output.image, *frame_output.executor);
    }

    render_executor.waitForDeferredResources();
}
