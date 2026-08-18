// 屏幕空间反射（SSR）示例。
//
// 参考示例里没有 SSR，这里以 44-sss（screen space shadows）的屏幕空间射线步进
// 内核为基础改写 —— 两者的核心是同一件事：在 view 空间沿一条射线步进，每步投影
// 到屏幕、和深度缓冲比较、用「穿到表面之后但没穿太厚」判命中。区别只有两点：
//   - 射线方向：44-sss 是 normalize(lightPos - viewPos)，这里是 SSSR VNDF 采样
//   - 命中之后：44-sss 累加遮蔽量，这里精修交点并采样颜色图
//
// 材质参数升级：完整 Disney Principled BRDF（对齐 example_disney_pbr）。
// 所有非 albedo 参数通过 imgui 实时可调，批次共享（每帧统一一套材质）。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/ssr_geom_vert.glsl)
#include GLSL(shaders/ssr_geom_mrt_frag.glsl)
#include GLSL(shaders/ssr_linear_depth_compute.glsl)
#include GLSL(shaders/ssr_trace_compute.glsl)
#include GLSL(shaders/ssr_composite_compute.glsl)
#include GLSL(shaders/ssr_pathtrace_compute.glsl)

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
const std::vector<uint16_t> cube_indices = {
    0, 2, 1, 1, 2, 3,
    4, 5, 6, 5, 7, 6,
    8, 10, 9, 9, 10, 11,
    12, 13, 14, 13, 15, 14,
    16, 18, 17, 17, 18, 19,
    20, 21, 22, 21, 23, 22,
};

// 完整 Disney Principled BRDF 参数（对齐 example_disney_pbr 的 DisneyMaterial）。
// 批次共享——场景里所有物体都使用同一套参数，仅 albedo 随 per-draw 变化。
struct DisneyMaterial
{
    float metallic        = 0.5f;
    float roughness       = 0.25f;
    float specular        = 0.5f;
    float specular_tint   = 0.0f;
    float subsurface      = 0.0f;
    float anisotropic     = 0.0f;
    float sheen           = 0.0f;
    float sheen_tint      = 0.5f;
    float clearcoat       = 0.0f;
    float clearcoat_gloss = 1.0f;
};

// 场景里的一个物体：变换 + albedo（其余材质参数全局共享）
struct Instance
{
    glm::mat4  model;
    glm::vec3  albedo;
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
    horizon::HardwareBuffer cube_vb = horizon::HardwareBuffer::vertex(cube_vertices, "example_ssr.cube.vb");
    horizon::HardwareBuffer cube_ib = horizon::HardwareBuffer::index(cube_indices, "example_ssr.cube.ib");

    // ---- G-buffer / 中间目标 ----
    const auto rt_usage = horizon::ImageUsage_ColorAttachment |
                          horizon::ImageUsage_Sampled |
                          horizon::ImageUsage_Storage;

    horizon::HardwareImage color_image(horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, horizon::Format::RGBA16_FLOAT, rt_usage, "example_ssr.color"));
    color_image.set_clear_color(0.14f, 0.19f, 0.28f, 0.0f);

    horizon::HardwareImage normal_image(horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, horizon::Format::RGBA8_UNORM, rt_usage, "example_ssr.normal"));
    normal_image.set_clear_color(0.5f, 0.5f, 1.0f, 1.0f);

    horizon::HardwareImage albedo_met_image(horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, horizon::Format::RGBA8_UNORM, rt_usage, "example_ssr.albedo_met"));
    albedo_met_image.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);

    horizon::HardwareImage depth_val_image(horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, horizon::Format::R32_FLOAT, rt_usage, "example_ssr.depthval"));
    depth_val_image.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);

    horizon::HardwareImage linear_depth_image(horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, horizon::Format::R32_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_Sampled,
        "example_ssr.lineardepth"));

    horizon::HardwareImage ssr_image(horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_Sampled,
        "example_ssr.ssr"));

    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        ssr_width, ssr_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_ssr.output"));

    horizon::HardwareImage scene_depth(horizon::HardwareImageDesc::depth_attachment(
        ssr_width, ssr_height, horizon::Format::D32, "example_ssr.depth"));
    scene_depth.set_clear_depth(1.0f, 0);

    // ---- 管线 ----
    horizon::RasterizerPipelineDesc geom_desc;
    geom_desc.blend_enabled = false;

    horizon::RasterizerPipeline geom_rasterizer(ssr_geom_vert_glsl, ssr_geom_mrt_frag_glsl, geom_desc);
    geom_rasterizer.outColor      = color_image;
    geom_rasterizer.outNormal     = normal_image;
    geom_rasterizer.outDepthVal   = depth_val_image;
    geom_rasterizer.outAlbedoMet  = albedo_met_image;
    geom_rasterizer.bind_depth_target(scene_depth);

    horizon::ComputePipeline linear_depth_compute(ssr_linear_depth_compute_glsl, ktm::uvec3(8, 8, 1));
    horizon::ComputePipeline trace_compute(ssr_trace_compute_glsl, ktm::uvec3(8, 8, 1));
    horizon::ComputePipeline composite_compute(ssr_composite_compute_glsl, ktm::uvec3(8, 8, 1));
    horizon::ComputePipeline pathtrace_compute(ssr_pathtrace_compute_glsl, ktm::uvec3(8, 8, 1));

    const uint32_t color_id        = color_image.store_descriptor();
    const uint32_t normal_id       = normal_image.store_descriptor();
    const uint32_t depth_val_id    = depth_val_image.store_descriptor();
    const uint32_t albedo_met_id   = albedo_met_image.store_descriptor();
    const uint32_t linear_depth_id = linear_depth_image.store_descriptor();
    const uint32_t ssr_id          = ssr_image.store_descriptor();
    const uint32_t output_id       = final_output_image.store_descriptor();

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    horizon::DrawIndexedParams cube_params;

    // ---- 相机 ----
    constexpr float aspect = static_cast<float>(ssr_width) / static_cast<float>(ssr_height);
    const glm::vec3 eye(0.0f, 2.6f, -10.5f);
    const glm::vec3 target(0.0f, 0.7f, 0.5f);
    const glm::mat4 view = glm::lookAtLH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, ssr_near, ssr_far);
        m[1][1] *= -1.0f;
        return m;
    }();
    const glm::mat4 view_proj = proj * view;

    const float p00 = proj[0][0];
    const float p11 = proj[1][1];
    const float p22 = proj[2][2];
    const float p32 = proj[3][2];

    const glm::vec4 depth_unpack(p32, -p22, 0.0f, 0.0f);
    const glm::vec4 ndc_to_view(2.0f / p00, 2.0f / p11, -1.0f / p00, -1.0f / p11);

    const glm::vec3 light_dir_world = glm::normalize(glm::vec3(0.45f, 0.85f, -0.35f));
    const glm::vec3 light_dir_vs    = glm::normalize(glm::vec3(view * glm::vec4(light_dir_world, 0.0f)));
    constexpr float ambient         = 0.22f;


    // ---- Disney 材质（全局共享，imgui 可调）----
    DisneyMaterial mat;

    // ---- SSR 可调参数 ----
    bool  ssr_enabled  = true;
    float max_distance = 12.0f;
    int   num_steps    = 48;
    float thickness    = 0.55f;
    int   refine_steps = 5;
    bool  use_jitter   = true;
    float intensity    = 1.0f;
    int   debug_mode   = 0;

    // ---- Path Trace 模式 ----
    bool pathtrace_mode = std::getenv("SSR_PATHTRACE") != nullptr;
    const glm::vec3 pt_ro(0.0f, 3.2f, -9.0f);
    const glm::vec3 pt_ta(0.0f, 1.0f, 0.0f);
    const glm::vec3 pt_fwd   = glm::normalize(pt_ta - pt_ro);
    const glm::vec3 pt_right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), pt_fwd));
    const glm::vec3 pt_up    = glm::cross(pt_fwd, pt_right);
    constexpr float pt_focal = 1.6f;

    HorizonImGuiLayer ui(window, ssr_width, ssr_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int    fps_frame_count   = 0;
    uint32_t frame_index     = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        const auto  now  = std::chrono::high_resolution_clock::now();
        const float dt   = std::chrono::duration<float>(now - prev_time).count();
        const float time = std::chrono::duration<float>(now - start_time).count();
        prev_time = now;

        ui.new_frame();
        ImGui::Begin("SSR");

        // SSR 参数
        ImGui::SeparatorText("SSR");
        ImGui::Checkbox("Enable SSR", &ssr_enabled);
        ImGui::SliderFloat("Max Distance", &max_distance, 1.0f, 40.0f);
        ImGui::SliderInt("Steps", &num_steps, 4, 128);
        ImGui::SliderFloat("Thickness", &thickness, 0.02f, 3.0f);
        ImGui::SliderInt("Refine Steps", &refine_steps, 0, 8);
        ImGui::Checkbox("Jitter Start", &use_jitter);
        ImGui::SliderFloat("Intensity", &intensity, 0.0f, 2.0f);
        ImGui::Combo("Debug View", &debug_mode, "Final\0SSR Color\0SSR Weight\0Reflectivity\0");
        ImGui::Checkbox("Path Trace (Disney)", &pathtrace_mode);

        // 完整 Disney 材质参数
        ImGui::SeparatorText("Disney Material (shared)");
        ImGui::SliderFloat("Metallic",        &mat.metallic,        0.0f, 1.0f);
        ImGui::SliderFloat("Roughness",       &mat.roughness,       0.0f, 1.0f);
        ImGui::SliderFloat("Specular",        &mat.specular,        0.0f, 1.0f);
        ImGui::SliderFloat("Specular Tint",   &mat.specular_tint,   0.0f, 1.0f);
        ImGui::SliderFloat("Subsurface",      &mat.subsurface,      0.0f, 1.0f);
        ImGui::SliderFloat("Anisotropic",     &mat.anisotropic,     0.0f, 1.0f);
        ImGui::SliderFloat("Sheen",           &mat.sheen,           0.0f, 1.0f);
        ImGui::SliderFloat("Sheen Tint",      &mat.sheen_tint,      0.0f, 1.0f);
        ImGui::SliderFloat("Clearcoat",       &mat.clearcoat,       0.0f, 1.0f);
        ImGui::SliderFloat("Clearcoat Gloss", &mat.clearcoat_gloss, 0.0f, 1.0f);

        ImGui::End();

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon SSR [Vulkan]%s - %d steps - %.1f FPS (%.2f ms)",
                          pathtrace_mode ? " PT" : "", num_steps, fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count   = 0;
        }

        // ---- 场景 ----
        std::vector<Instance> instances;
        instances.reserve(32);

        instances.push_back({
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.2f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, 0.2f, 16.0f)),
            glm::vec3(0.10f, 0.11f, 0.13f) });

        constexpr int   dim         = 4;
        constexpr float spacing     = 2.9f;
        constexpr float grid_offset = (dim - 1) * spacing * 0.5f;
        const glm::vec3 palette[4]  = {
            { 0.90f, 0.32f, 0.28f },
            { 0.98f, 0.76f, 0.24f },
            { 0.32f, 0.72f, 0.55f },
            { 0.38f, 0.55f, 0.92f },
        };
        for (int zz = 0; zz < dim; ++zz)
        {
            for (int xx = 0; xx < dim; ++xx)
            {
                const int   idx    = zz * dim + xx;
                const float height = 1.05f + std::sin(time * 0.9f + idx * 0.7f) * 0.35f;
                glm::mat4 model = glm::eulerAngleYX(time * 0.35f + idx * 0.4f, time * 0.22f + idx * 0.25f);
                model = glm::translate(glm::mat4(1.0f),
                            glm::vec3(-grid_offset + xx * spacing, height, -grid_offset + zz * spacing)) *
                        model * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
                instances.push_back({ model, palette[idx & 3] });
            }
        }

        for (int side = 0; side < 2; ++side)
        {
            const float x = (side == 0) ? -6.4f : 6.4f;
            instances.push_back({
                glm::translate(glm::mat4(1.0f), glm::vec3(x, 2.2f, 2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.42f, 2.2f, 0.42f)),
                glm::vec3(0.82f, 0.80f, 0.76f) });
        }

        // Pass 1：几何 → G-buffer
        geom_rasterizer.clear_records();
        geom_rasterizer.vsp.view_proj    = view_proj;
        geom_rasterizer.vsp.view         = view;
        geom_rasterizer.vsp.light_dir_vs = glm::vec4(light_dir_vs, ambient);
        // 批次共享 Disney 材质参数
        geom_rasterizer.vsp.disney_a = glm::vec4(mat.metallic, mat.roughness,
                                                  mat.specular, mat.specular_tint);
        geom_rasterizer.vsp.disney_b = glm::vec4(mat.subsurface, mat.anisotropic,
                                                  mat.sheen, mat.sheen_tint);
        geom_rasterizer.vsp.disney_c = glm::vec4(mat.clearcoat, mat.clearcoat_gloss, 0.0f, 0.0f);

        for (const Instance& inst : instances)
        {
            geom_rasterizer.vpc.model    = inst.model;
            geom_rasterizer.vpc.material = glm::vec4(inst.albedo, 0.0f);
            geom_rasterizer.record(cube_ib, cube_vb, cube_params);
        }

        // Pass 2：器件深度 → view 空间线性深度
        linear_depth_compute.pushConsts.depthID       = depth_val_id;
        linear_depth_compute.pushConsts.linearDepthID = linear_depth_id;
        linear_depth_compute.pushConsts.depth_unpack  = depth_unpack;
        linear_depth_compute.pushConsts.params0       =
            glm::vec4(float(ssr_width), float(ssr_height), ssr_far, 0.0f);

        // Pass 3：屏幕空间射线步进
        // params2 新布局：x=refineSteps, y=useJitter, z=specular, w=subsurface
        trace_compute.pushConsts.linearDepthID = linear_depth_id;
        trace_compute.pushConsts.normalID      = normal_id;
        trace_compute.pushConsts.colorID       = color_id;
        trace_compute.pushConsts.ssrID         = ssr_id;
        trace_compute.pushConsts.albedoMetID   = albedo_met_id;
        trace_compute.pushConsts.ndc_to_view   = ndc_to_view;
        trace_compute.pushConsts.params0       = glm::vec4(p00, p11, max_distance, float(num_steps));
        trace_compute.pushConsts.params1       =
            glm::vec4(thickness, float(frame_index), float(ssr_width), float(ssr_height));
        trace_compute.pushConsts.params2       =
            glm::vec4(float(refine_steps), use_jitter ? 1.0f : 0.0f,
                      mat.specular, mat.subsurface);
        trace_compute.pushConsts.disney_mat1   =
            glm::vec4(mat.specular_tint, mat.anisotropic, mat.sheen, mat.sheen_tint);
        trace_compute.pushConsts.disney_mat2   =
            glm::vec4(mat.clearcoat, mat.clearcoat_gloss, 0.0f, 0.0f);

        // Pass 4：合成
        composite_compute.pushConsts.colorID  = color_id;
        composite_compute.pushConsts.ssrID    = ssr_id;
        composite_compute.pushConsts.outputID = output_id;
        composite_compute.pushConsts.params0  = glm::vec4(
            float(ssr_width), float(ssr_height), ssr_enabled ? intensity : 0.0f, float(debug_mode));

        // Path Trace 模式
        pathtrace_compute.pushConsts.outputID = output_id;
        pathtrace_compute.pushConsts.pt0 = glm::vec4(pt_ro, float(frame_index));
        pathtrace_compute.pushConsts.pt1 = glm::vec4(pt_fwd, pt_focal);
        pathtrace_compute.pushConsts.pt2 = glm::vec4(pt_right, float(ssr_width));
        pathtrace_compute.pushConsts.pt3 = glm::vec4(pt_up, float(ssr_height));

        horizon::SubmitReceipt render_receipt;
        if (pathtrace_mode)
        {
            render_receipt = render_executor.stream() << pathtrace_compute.dispatch_extent(ssr_width, ssr_height)
                                                      << horizon::commit();
        }
        else
        {
            render_receipt = render_executor << geom_rasterizer.extent(ssr_width, ssr_height)
                                             << linear_depth_compute.dispatch_extent(ssr_width, ssr_height)
                                             << trace_compute.dispatch_extent(ssr_width, ssr_height)
                                             << composite_compute.dispatch_extent(ssr_width, ssr_height)
                                             << horizon::commit();
        }

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());

        ++frame_index;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
