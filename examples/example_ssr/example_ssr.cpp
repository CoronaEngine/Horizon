// 屏幕空间反射（SSR）示例。
//
// 参考示例里没有 SSR，这里以 44-sss（screen space shadows）的屏幕空间射线步进
// 内核为基础改写 —— 两者的核心是同一件事：在 view 空间沿一条射线步进，每步投影
// 到屏幕、和深度缓冲比较、用「穿到表面之后但没穿太厚」判命中。区别只有两点：
//   - 射线方向：44-sss 是 normalize(lightPos - viewPos)，这里是 reflect(V, N)
//   - 命中之后：44-sss 累加遮蔽量，这里精修交点并采样颜色图
// G-buffer / 全屏 compute 链路沿用 example_deferred（MRT 三附件）与 example_assao
// （bindless storage image + push constant 参数块）的既有写法。
//
// 管线（4 站）：
//   raster  geom          → outColor(RGBA16F rgb=直接光, a=reflectivity)
//                           outNormal(RGBA8 rgb=view 法线, a=roughness)
//                           outDepthVal(R32F 器件深度)
//   compute linear_depth  → R32F view 空间线性深度（44-sss 的 fs_sss_linear_depth）
//   compute trace         → RGBA16F rgb=反射色, a=权重（44-sss 的步进循环）
//   compute composite     → RGBA16F 最终输出（44-sss 的 deferred_combine）
//
// 与 44-sss 的差异：
// - 原版把直接光单独一趟 deferred combine；这里基础光照直接在 G-buffer FS 里算完，
//   因为 SSR 需要一张已着色的颜色图作为采样源；
// - 原版把 viewToProj 整个 mat4 拆成 4 个 vec4 传进 shader；这里投影无斜切，
//   只传 p00/p11 两个系数即可完成 view→uv 投影；
// - 原版 radius 兼作步进距离和厚度阈值；这里 maxDistance / thickness 分开可调；
// - 新增 binary search 交点精修（阴影不需要交点精度，采颜色需要）。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/ssr_geom_vert.glsl)
#include GLSL(shaders/ssr_geom_mrt_frag.glsl)
#include GLSL(shaders/ssr_linear_depth_compute.glsl)
#include GLSL(shaders/ssr_trace_compute.glsl)
#include GLSL(shaders/ssr_composite_compute.glsl)

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace
{
constexpr uint32_t ssr_width = 1280;
constexpr uint32_t ssr_height = 720;
constexpr float ssr_near = 0.1f;
constexpr float ssr_far = 100.0f;

struct SsrVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

// 单位立方体：每面 4 顶点独立法线（面法线，不共享顶点）
std::vector<SsrVertex> build_cube_vertices()
{
    struct Face
    {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };
    const Face faces[6] = {
        { { 0, 0, 1 }, { { -1, 1, 1 }, { 1, 1, 1 }, { -1, -1, 1 }, { 1, -1, 1 } } },
        { { 0, 0, -1 }, { { -1, 1, -1 }, { 1, 1, -1 }, { -1, -1, -1 }, { 1, -1, -1 } } },
        { { 0, 1, 0 }, { { -1, 1, 1 }, { 1, 1, 1 }, { -1, 1, -1 }, { 1, 1, -1 } } },
        { { 0, -1, 0 }, { { -1, -1, 1 }, { 1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 } } },
        { { 1, 0, 0 }, { { 1, -1, 1 }, { 1, 1, 1 }, { 1, -1, -1 }, { 1, 1, -1 } } },
        { { -1, 0, 0 }, { { -1, -1, 1 }, { -1, 1, 1 }, { -1, -1, -1 }, { -1, 1, -1 } } },
    };

    std::vector<SsrVertex> vertices;
    vertices.reserve(24);
    for (const Face& face : faces)
    {
        for (const glm::vec3& corner : face.corners)
        {
            SsrVertex v {};
            v.position = { corner.x, corner.y, corner.z };
            v.normal = { face.normal.x, face.normal.y, face.normal.z };
            vertices.push_back(v);
        }
    }
    return vertices;
}

// 与 example_deferred 的立方体索引一致（左手系正面朝外）
const std::vector<uint32_t> cube_indices = {
    0, 2, 1, 1, 2, 3,
    4, 5, 6, 5, 7, 6,
    8, 10, 9, 9, 10, 11,
    12, 13, 14, 13, 15, 14,
    16, 18, 17, 17, 18, 19,
    20, 21, 22, 21, 23, 22,
};

// 场景里的一个物体：变换 + 材质
struct Instance
{
    glm::mat4 model;
    glm::vec3 albedo;
    float reflectivity;
    float roughness;
};

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

void run_example_ssr()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(ssr_width, ssr_height, "Horizon SSR [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    const std::vector<SsrVertex> cube_vertices = build_cube_vertices();
    Corona::Horizon::HardwareBuffer cube_vb = Corona::Horizon::HardwareBuffer::vertex(cube_vertices, "example_ssr.cube.vb");
    Corona::Horizon::HardwareBuffer cube_ib = Corona::Horizon::HardwareBuffer::index(cube_indices, "example_ssr.cube.ib");

    // ---- G-buffer / 中间目标 ----
    // compute 侧用 imageLoad/imageStore 访问，故都要带 Storage
    const auto rt_usage = Corona::Horizon::ImageUsageFlags::ColorAttachment |
                          Corona::Horizon::ImageUsageFlags::Sampled |
                          Corona::Horizon::ImageUsageFlags::Storage;

    // rgb: 直接光结果（SSR 的采样源）, a: reflectivity。清屏 alpha=0 → 天空不产生反射
    Corona::Horizon::HardwareImage color_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, Corona::Horizon::Format::RGBA16_FLOAT, rt_usage, "example_ssr.color"));
    color_image.set_clear_color(0.14f, 0.19f, 0.28f, 0.0f);

    // rgb: view 空间法线, a: roughness
    Corona::Horizon::HardwareImage normal_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, Corona::Horizon::Format::RGBA8_UNORM, rt_usage, "example_ssr.normal"));
    normal_image.set_clear_color(0.5f, 0.5f, 1.0f, 1.0f);

    Corona::Horizon::HardwareImage depth_val_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, Corona::Horizon::Format::R32_FLOAT, rt_usage, "example_ssr.depthval"));
    depth_val_image.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f); // 远平面

    Corona::Horizon::HardwareImage linear_depth_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, Corona::Horizon::Format::R32_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::Sampled,
        "example_ssr.lineardepth"));

    Corona::Horizon::HardwareImage ssr_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::Sampled,
        "example_ssr.ssr"));

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_ssr.output"));

    Corona::Horizon::HardwareImage scene_depth(Corona::Horizon::HardwareImageDesc::depth_attachment(
        ssr_width, ssr_height, Corona::Horizon::Format::D32, "example_ssr.depth"));
    scene_depth.set_clear_depth(1.0f, 0);

    // ---- 管线 ----
    Corona::Horizon::RasterizerPipelineDesc geom_desc;
    geom_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline geom_rasterizer(ssr_geom_vert_glsl, ssr_geom_mrt_frag_glsl, geom_desc);
    geom_rasterizer.outColor = color_image;
    geom_rasterizer.outNormal = normal_image;
    geom_rasterizer.outDepthVal = depth_val_image;
    geom_rasterizer.bind_depth_target(scene_depth);

    Corona::Horizon::ComputePipeline linear_depth_compute(ssr_linear_depth_compute_glsl, ktm::uvec3(8, 8, 1));
    Corona::Horizon::ComputePipeline trace_compute(ssr_trace_compute_glsl, ktm::uvec3(8, 8, 1));
    Corona::Horizon::ComputePipeline composite_compute(ssr_composite_compute_glsl, ktm::uvec3(8, 8, 1));

    // bindless 索引取一次即可（图像本身不变）
    const uint32_t color_id = color_image.store_descriptor();
    const uint32_t normal_id = normal_image.store_descriptor();
    const uint32_t depth_val_id = depth_val_image.store_descriptor();
    const uint32_t linear_depth_id = linear_depth_image.store_descriptor();
    const uint32_t ssr_id = ssr_image.store_descriptor();
    const uint32_t output_id = final_output_image.store_descriptor();

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    Corona::Horizon::DrawIndexedParams cube_params;
    cube_params.index_type = Corona::Horizon::IndexType::UInt32;
    cube_params.index_count = static_cast<uint32_t>(cube_indices.size());

    // ---- 相机：低角度俯视地面，让反射拉出长条 ----
    constexpr float aspect = static_cast<float>(ssr_width) / static_cast<float>(ssr_height);
    const glm::vec3 eye(0.0f, 2.6f, -10.5f);
    const glm::vec3 target(0.0f, 0.7f, 0.5f);
    const glm::mat4 view = glm::lookAtLH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, ssr_near, ssr_far);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    const glm::mat4 view_proj = proj * view;

    // 深度/投影系数（同 example_assao）：
    //   viewZ    = p32 / (device_z - p22)
    //   view.xy  = (uv * (2/p00, 2/p11) + (-1/p00, -1/p11)) * viewZ
    const float p00 = proj[0][0];
    const float p11 = proj[1][1];
    const float p22 = proj[2][2];
    const float p32 = proj[3][2];

    const glm::vec4 depth_unpack(p32, -p22, 0.0f, 0.0f);
    const glm::vec4 ndc_to_view(2.0f / p00, 2.0f / p11, -1.0f / p00, -1.0f / p11);

    // 方向光（世界空间指向光源）→ view 空间，供 G-buffer FS 直接使用
    const glm::vec3 light_dir_world = glm::normalize(glm::vec3(0.45f, 0.85f, -0.35f));
    const glm::vec3 light_dir_vs = glm::normalize(glm::vec3(view * glm::vec4(light_dir_world, 0.0f)));
    constexpr float ambient = 0.22f;

    const uint32_t dispatch_x = (ssr_width + 7) / 8;
    const uint32_t dispatch_y = (ssr_height + 7) / 8;

    // ---- SSR 可调参数（原版 44-sss 同样把这些做成 imgui 滑条） ----
    bool ssr_enabled = true;
    float max_distance = 12.0f;
    int num_steps = 48;
    float thickness = 0.55f;
    int refine_steps = 5;
    bool use_jitter = true;
    float fresnel_power = 4.0f;
    float fresnel_f0 = 0.04f;
    float intensity = 1.0f;
    int debug_mode = 0;

    HorizonImGuiLayer ui(window, ssr_width, ssr_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;
    uint32_t frame_index = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        const float time = std::chrono::duration<float>(now - start_time).count();
        prev_time = now;

        ui.new_frame();
        ImGui::Begin("SSR");
        ImGui::Checkbox("Enable SSR", &ssr_enabled);
        ImGui::SliderFloat("Max Distance", &max_distance, 1.0f, 40.0f);
        ImGui::SliderInt("Steps", &num_steps, 4, 128);
        ImGui::SliderFloat("Thickness", &thickness, 0.02f, 3.0f);
        ImGui::SliderInt("Refine Steps", &refine_steps, 0, 8);
        ImGui::Checkbox("Jitter Start", &use_jitter);
        ImGui::SliderFloat("Fresnel Power", &fresnel_power, 1.0f, 8.0f);
        ImGui::SliderFloat("Fresnel F0", &fresnel_f0, 0.0f, 1.0f);
        ImGui::SliderFloat("Intensity", &intensity, 0.0f, 2.0f);
        ImGui::Combo("Debug View", &debug_mode, "Final\0SSR Color\0SSR Weight\0Reflectivity\0");
        ImGui::End();

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon SSR [Vulkan] - %d steps - %.1f FPS (%.2f ms)",
                          num_steps, fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // ---- 场景：镜面地板 + 4x4 起伏旋转立方体 + 两根立柱 ----
        std::vector<Instance> instances;
        instances.reserve(32);

        // 地板：压扁的 cube，顶面正好 y=0
        instances.push_back({
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.2f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, 0.2f, 16.0f)),
            glm::vec3(0.10f, 0.11f, 0.13f), 0.88f, 0.05f });

        constexpr int dim = 4;
        constexpr float spacing = 2.9f;
        constexpr float grid_offset = (dim - 1) * spacing * 0.5f;
        const glm::vec3 palette[4] = {
            { 0.90f, 0.32f, 0.28f },
            { 0.98f, 0.76f, 0.24f },
            { 0.32f, 0.72f, 0.55f },
            { 0.38f, 0.55f, 0.92f },
        };
        for (int zz = 0; zz < dim; ++zz)
        {
            for (int xx = 0; xx < dim; ++xx)
            {
                const int idx = zz * dim + xx;
                const float height = 1.05f + std::sin(time * 0.9f + idx * 0.7f) * 0.35f;
                glm::mat4 model = glm::eulerAngleYX(time * 0.35f + idx * 0.4f, time * 0.22f + idx * 0.25f);
                model = glm::translate(glm::mat4(1.0f),
                                       glm::vec3(-grid_offset + xx * spacing, height, -grid_offset + zz * spacing)) *
                        model * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
                instances.push_back({ model, palette[idx & 3], 0.14f, 0.38f });
            }
        }

        // 两根立柱：给画面一点竖向结构，反射里更容易看出拉伸
        for (int side = 0; side < 2; ++side)
        {
            const float x = (side == 0) ? -6.4f : 6.4f;
            instances.push_back({
                glm::translate(glm::mat4(1.0f), glm::vec3(x, 2.2f, 2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.42f, 2.2f, 0.42f)),
                glm::vec3(0.82f, 0.80f, 0.76f), 0.10f, 0.30f });
        }

        // Pass 1：几何 → G-buffer（MRT 三附件）
        geom_rasterizer.clear_records();
        geom_rasterizer.vsp.view_proj = view_proj;
        geom_rasterizer.vsp.view = view;
        geom_rasterizer.vsp.light_dir_vs = glm::vec4(light_dir_vs, ambient);
        for (const Instance& inst : instances)
        {
            geom_rasterizer.vpc.model = inst.model;
            geom_rasterizer.vpc.material = glm::vec4(inst.albedo, 0.0f);
            geom_rasterizer.vpc.params = glm::vec4(inst.reflectivity, inst.roughness, 0.0f, 0.0f);
            geom_rasterizer.record(cube_ib, cube_vb, cube_params);
        }

        // Pass 2：器件深度 → view 空间线性深度
        linear_depth_compute.pushConsts.depthID = depth_val_id;
        linear_depth_compute.pushConsts.linearDepthID = linear_depth_id;
        linear_depth_compute.pushConsts.depth_unpack = depth_unpack;
        linear_depth_compute.pushConsts.params0 =
            glm::vec4(float(ssr_width), float(ssr_height), ssr_far, 0.0f);

        // Pass 3：屏幕空间射线步进
        trace_compute.pushConsts.linearDepthID = linear_depth_id;
        trace_compute.pushConsts.normalID = normal_id;
        trace_compute.pushConsts.colorID = color_id;
        trace_compute.pushConsts.ssrID = ssr_id;
        trace_compute.pushConsts.ndc_to_view = ndc_to_view;
        trace_compute.pushConsts.params0 = glm::vec4(p00, p11, max_distance, float(num_steps));
        trace_compute.pushConsts.params1 =
            glm::vec4(thickness, float(frame_index & 63u), float(ssr_width), float(ssr_height));
        trace_compute.pushConsts.params2 =
            glm::vec4(fresnel_power, fresnel_f0, float(refine_steps), use_jitter ? 1.0f : 0.0f);

        // Pass 4：合成（SSR 关闭时 intensity 置 0，链路照跑便于对比开销）
        composite_compute.pushConsts.colorID = color_id;
        composite_compute.pushConsts.ssrID = ssr_id;
        composite_compute.pushConsts.outputID = output_id;
        composite_compute.pushConsts.params0 = glm::vec4(
            float(ssr_width), float(ssr_height), ssr_enabled ? intensity : 0.0f, float(debug_mode));

        Corona::Horizon::SubmitReceipt render_receipt =
            render_executor << geom_rasterizer(ssr_width, ssr_height)
                            << linear_depth_compute(dispatch_x, dispatch_y, 1)
                            << trace_compute(dispatch_x, dispatch_y, 1)
                            << composite_compute(dispatch_x, dispatch_y, 1)
                            << Corona::Horizon::submit;

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());

        ++frame_index;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
