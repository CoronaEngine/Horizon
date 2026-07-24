// 移植自参考示例 16-shadowmaps（完整版阴影贴图），取其默认启动配置：
// SpotLight + InvZ + Hard（1024x1024 单张阴影图，深度打包进 RGBA8 颜色目标，
// 与 原版 默认路径一致）。场景与原版对齐：550 地面 + bunny + hollowcube +
// cube + 环形 10 根柱子、聚光灯绕场景旋转（pos=(cos t*20, 26, sin t*20)，
// 方向指向原点）、Blinn-Phong 材质光照 + 距离衰减 + 锥形衰减 + 指数雾。
// 差异：固定默认参数（无 imgui 技术切换面板：PCF/PCSS/VSM/ESM/CSM/omni 等
// 变体不在本移植范围）、相机固定 (0,60,-105) 俯视、不画光源 billboard、
// 原版的 tree.bin 是 meshopt 压缩 chunk（VBC）加载器不支持，用 column.bin 替代。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/shadowmaps_pack_vert.glsl)
#include GLSL(shaders/shadowmaps_pack_frag.glsl)
#include GLSL(shaders/shadowmaps_scene_vert.glsl)
#include GLSL(shaders/shadowmaps_scene_frag.glsl)

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace
{
constexpr uint32_t smx_width = 1280;
constexpr uint32_t smx_height = 720;
constexpr uint32_t shadow_map_size = 1024; // m_sizePwrTwo=10
constexpr float shadow_near = 1.0f;
constexpr float shadow_far = 250.0f;
constexpr float shadow_bias = 0.0035f;
constexpr float shadow_normal_offset = 0.0012f;
constexpr float coverage_spot = 90.0f; // 阴影图覆盖角（光源投影 fovy）
constexpr float spot_outer_angle = 45.0f;
constexpr float spot_inner_angle = 30.0f;

const std::filesystem::path smx_asset_root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

// ============================================================================
// 原版 二进制网格加载（与 example_ibl 相同的未压缩 VB/IB chunk 解析）
// ============================================================================

struct SmxVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

struct LoadedMesh
{
    std::vector<SmxVertex> vertices;
    std::vector<uint32_t> indices;
};

uint32_t fourcc(char a, char b, char c, uint8_t d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(d) << 24);
}

template <typename T>
T read_pod(const std::vector<std::byte>& bytes, size_t& cursor)
{
    T value {};
    std::memcpy(&value, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return value;
}

LoadedMesh load_bin_mesh(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open mesh file: " + path.string());

    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);

    const uint32_t chunk_vb = fourcc('V', 'B', ' ', 0x1);
    const uint32_t chunk_ib = fourcc('I', 'B', ' ', 0x0);
    const uint32_t chunk_pri = fourcc('P', 'R', 'I', 0x0);

    constexpr uint16_t attrib_id_position = 0x0001;
    constexpr uint16_t attrib_id_normal = 0x0002;
    constexpr uint16_t attrib_type_id_uint8 = 0x0001;
    constexpr uint16_t attrib_type_id_float = 0x0004;

    LoadedMesh mesh;
    uint32_t group_base_vertex = 0;
    size_t cursor = 0;

    while (cursor + sizeof(uint32_t) <= bytes.size())
    {
        const uint32_t chunk = read_pod<uint32_t>(bytes, cursor);
        if (chunk == chunk_vb)
        {
            cursor += 16 + 24 + 64; // Sphere + Aabb + Obb

            const uint8_t num_attrs = read_pod<uint8_t>(bytes, cursor);
            const uint16_t stride = read_pod<uint16_t>(bytes, cursor);

            int32_t position_offset = -1;
            int32_t normal_offset = -1;
            uint16_t normal_type = 0;
            for (uint8_t i = 0; i < num_attrs; ++i)
            {
                const uint16_t attr_offset = read_pod<uint16_t>(bytes, cursor);
                const uint16_t attr_id = read_pod<uint16_t>(bytes, cursor);
                cursor += 1; // num
                const uint16_t type_id = read_pod<uint16_t>(bytes, cursor);
                cursor += 2; // normalized + asInt

                if (attr_id == attrib_id_position)
                    position_offset = attr_offset;
                if (attr_id == attrib_id_normal)
                {
                    normal_offset = attr_offset;
                    normal_type = type_id;
                }
            }

            if (position_offset < 0 || normal_offset < 0)
                throw std::runtime_error("Mesh misses position/normal attribute: " + path.string());

            const uint16_t num_vertices = read_pod<uint16_t>(bytes, cursor);
            group_base_vertex = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + num_vertices);

            for (uint16_t v = 0; v < num_vertices; ++v)
            {
                const std::byte* vertex_data = bytes.data() + cursor + static_cast<size_t>(v) * stride;

                SmxVertex vertex;
                std::memcpy(vertex.position.data(), vertex_data + position_offset, sizeof(float) * 3);

                if (normal_type == attrib_type_id_uint8)
                {
                    uint8_t packed[3];
                    std::memcpy(packed, vertex_data + normal_offset, 3);
                    for (int c = 0; c < 3; ++c)
                        vertex.normal[c] = static_cast<float>(packed[c]) / 255.0f * 2.0f - 1.0f;
                }
                else if (normal_type == attrib_type_id_float)
                {
                    std::memcpy(vertex.normal.data(), vertex_data + normal_offset, sizeof(float) * 3);
                }
                else
                {
                    throw std::runtime_error("Unsupported normal attribute type in mesh: " + path.string());
                }

                mesh.vertices.push_back(vertex);
            }
            cursor += static_cast<size_t>(num_vertices) * stride;
        }
        else if (chunk == chunk_ib)
        {
            const uint32_t num_indices = read_pod<uint32_t>(bytes, cursor);
            mesh.indices.reserve(mesh.indices.size() + num_indices);
            for (uint32_t i = 0; i < num_indices; ++i)
            {
                const uint16_t index = read_pod<uint16_t>(bytes, cursor);
                mesh.indices.push_back(group_base_vertex + index);
            }
        }
        else if (chunk == chunk_pri)
        {
            const uint16_t material_len = read_pod<uint16_t>(bytes, cursor);
            cursor += material_len;
            const uint16_t num_prims = read_pod<uint16_t>(bytes, cursor);
            for (uint16_t i = 0; i < num_prims; ++i)
            {
                const uint16_t name_len = read_pod<uint16_t>(bytes, cursor);
                cursor += name_len;
                cursor += 16 + 16 + 24 + 64; // 4×u32 + Sphere + Aabb + Obb
            }
        }
        else
        {
            throw std::runtime_error("Unsupported chunk in mesh (compressed?): " + path.string());
        }
    }

    return mesh;
}

struct GpuMesh
{
    Corona::Horizon::HardwareBuffer vb;
    Corona::Horizon::HardwareBuffer ib;
    uint32_t index_count = 0;
};

GpuMesh upload_mesh(const LoadedMesh& mesh, const std::string& name)
{
    return GpuMesh {
        Corona::Horizon::HardwareBuffer::vertex(mesh.vertices, name + ".vb"),
        Corona::Horizon::HardwareBuffer::index(mesh.indices, name + ".ib"),
        static_cast<uint32_t>(mesh.indices.size()),
    };
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

void run_example_shadowmaps()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(smx_width, smx_height, "Horizon Shadowmaps [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    GpuMesh bunny = upload_mesh(load_bin_mesh(smx_asset_root / "meshes" / "bunny.bin"), "example_shadowmaps.bunny");
    GpuMesh column = upload_mesh(load_bin_mesh(smx_asset_root / "meshes" / "column.bin"), "example_shadowmaps.column");
    GpuMesh cube = upload_mesh(load_bin_mesh(smx_asset_root / "meshes" / "cube.bin"), "example_shadowmaps.cube");
    GpuMesh hollowcube = upload_mesh(load_bin_mesh(smx_asset_root / "meshes" / "hollowcube.bin"), "example_shadowmaps.hollowcube");

    // 地面 hplane（±1，法线 +Y）
    const std::vector<SmxVertex> plane_vertices = {
        { { -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { -1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
    };
    const std::vector<uint32_t> plane_indices = { 0, 1, 2, 1, 3, 2 };
    GpuMesh floor_plane {
        Corona::Horizon::HardwareBuffer::vertex(plane_vertices, "example_shadowmaps.floor.vb"),
        Corona::Horizon::HardwareBuffer::index(plane_indices, "example_shadowmaps.floor.ib"),
        static_cast<uint32_t>(plane_indices.size()),
    };

    // Pass 1 目标：1024x1024 RGBA8 打包深度 + 独立 D32
    Corona::Horizon::HardwareImage shadow_map_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        shadow_map_size, shadow_map_size, Corona::Horizon::Format::RGBA8_UNORM,
        Corona::Horizon::ImageUsageFlags::ColorAttachment | Corona::Horizon::ImageUsageFlags::Sampled,
        "example_shadowmaps.shadowmap"));
    shadow_map_image.set_clear_color(1.0f, 1.0f, 1.0f, 1.0f); // 白 = 最远深度

    Corona::Horizon::HardwareImage shadow_depth_image(Corona::Horizon::HardwareImageDesc::depth_attachment(
        shadow_map_size, shadow_map_size, Corona::Horizon::Format::D32, "example_shadowmaps.shadow_depth"));
    shadow_depth_image.set_clear_depth(1.0f, 0);

    // Pass 2 目标：主输出
    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        smx_width, smx_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_shadowmaps.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f); // 雾色黑

    Corona::Horizon::HardwareImage depth_image(Corona::Horizon::HardwareImageDesc::depth_attachment(
        smx_width, smx_height, Corona::Horizon::Format::D32, "example_shadowmaps.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    Corona::Horizon::RasterizerPipelineDesc pack_desc;
    pack_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline pack_rasterizer(shadowmaps_pack_vert_glsl, shadowmaps_pack_frag_glsl, pack_desc);
    pack_rasterizer.outColor = shadow_map_image;
    pack_rasterizer.bind_depth_target(shadow_depth_image);

    Corona::Horizon::RasterizerPipelineDesc scene_desc;
    scene_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline scene_rasterizer(shadowmaps_scene_vert_glsl, shadowmaps_scene_frag_glsl, scene_desc);
    scene_rasterizer.outColor = final_output_image;
    scene_rasterizer.bind_depth_target(depth_image);
    scene_rasterizer.shadowMap = shadow_map_image;

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    constexpr float aspect = static_cast<float>(smx_width) / static_cast<float>(smx_height);
    // 原版 相机 (0,60,-105)、垂直角 -0.45 rad（俯视）
    const glm::vec3 eye(0.0f, 60.0f, -105.0f);
    const glm::vec3 forward(0.0f, std::sin(-0.45f), std::cos(-0.45f));
    const glm::mat4 view = glm::lookAtLH(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, 0.1f, 2000.0f);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    const glm::mat4 view_proj = proj * view;

    // 光源投影（fovy=覆盖角 90°、aspect 1）
    const glm::mat4 light_proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(coverage_spot), 1.0f, shadow_near, shadow_far);
        m[1][1] *= -1.0f;
        return m;
    }();

    // NDC → uv 的 bias 矩阵（Vulkan：z 恒等映射，与 pack 侧一致）
    const glm::mat4 shadow_bias_mtx = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.0f)) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 1.0f));

    // 材质/光源常量（原版 m_defaultMaterial / m_pointLight）
    const glm::vec4 material_ka(1.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec4 material_kd(1.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec4 material_ks(1.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec4 light_ambient(1.0f, 1.0f, 1.0f, 0.0f); // 与原版一致（ambient power 0）
    const glm::vec4 light_diffuse(1.0f, 1.0f, 1.0f, 850.0f);
    const glm::vec4 light_specular(1.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec3 light_attn(1.0f, 0.0f, 1.0f);

    HorizonImGuiLayer ui(window, smx_width, smx_height);

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
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon Shadowmaps [Vulkan] (spot/hard) - %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // 聚光灯：绕场景旋转，指向原点
        const glm::vec3 light_pos(std::cos(time) * 20.0f, 26.0f, std::sin(time) * 20.0f);
        const glm::vec3 spot_dir = -light_pos;

        const glm::mat4 light_view = glm::lookAtLH(light_pos, light_pos + spot_dir, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 light_view_proj = light_proj * light_view;
        const glm::mat4 shadow_mtx = shadow_bias_mtx * light_view_proj;

        // view 空间光源参数
        const glm::vec4 light_pos_vs = view * glm::vec4(light_pos, 1.0f);
        const glm::vec3 spot_dir_vs = glm::vec3(view * glm::vec4(spot_dir, 0.0f));

        // 场景物体矩阵（原版 mtxSRT 行向量 S·R·T → glm 列向量 T·R·S）
        struct DrawItem
        {
            const GpuMesh* mesh;
            glm::mat4 model;
        };
        std::vector<DrawItem> items;
        items.reserve(4 + 10);
        items.push_back({ &floor_plane, glm::scale(glm::mat4(1.0f), glm::vec3(550.0f)) });
        // 原版行向量旋转矩阵与 glm 列向量同角旋转互为转置，等效角度取反
        items.push_back({ &bunny,
                          glm::translate(glm::mat4(1.0f), glm::vec3(15.0f, 5.0f, 0.0f)) *
                              glm::eulerAngleY(time - 1.56f) * glm::scale(glm::mat4(1.0f), glm::vec3(5.0f)) });
        items.push_back({ &hollowcube,
                          glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)) *
                              glm::eulerAngleY(time - 1.56f) * glm::scale(glm::mat4(1.0f), glm::vec3(2.5f)) });
        items.push_back({ &cube,
                          glm::translate(glm::mat4(1.0f), glm::vec3(-15.0f, 5.0f, 0.0f)) *
                              glm::eulerAngleY(time - 1.56f) * glm::scale(glm::mat4(1.0f), glm::vec3(2.5f)) });
        constexpr int num_trees = 10;
        for (int i = 0; i < num_trees; ++i)
        {
            const float angle = i * 2.0f * glm::pi<float>() / num_trees;
            items.push_back({ &column,
                              glm::translate(glm::mat4(1.0f), glm::vec3(std::sin(angle) * 60.0f, 0.0f, std::cos(angle) * 60.0f)) *
                                  glm::eulerAngleY(-float(i)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)) });
        }

        // Pass 1：光源视角打包深度
        pack_rasterizer.clear_records();
        for (const DrawItem& item : items)
        {
            Corona::Horizon::DrawIndexedParams params;
            params.index_type = Corona::Horizon::IndexType::UInt32;
            params.index_count = item.mesh->index_count;

            pack_rasterizer.model_pc.mvp = light_view_proj * item.model;
            pack_rasterizer.record(item.mesh->ib, item.mesh->vb, params);
        }

        // Pass 2：场景光照 + 硬阴影
        scene_rasterizer.clear_records();
        // 共享矩阵（batch 内不变）：VS 内用 proj_view * pc.model 等现场计算 mvp/model_view/light_mtx
        scene_rasterizer.vsp.proj_view       = view_proj;
        scene_rasterizer.vsp.view_matrix     = view;
        scene_rasterizer.vsp.light_proj_view = shadow_mtx; // bias * light_proj * light_view
        scene_rasterizer.vsp.light_pos_vs = light_pos_vs;
        scene_rasterizer.vsp.light_ambient = light_ambient;
        scene_rasterizer.vsp.light_diffuse = light_diffuse;
        scene_rasterizer.vsp.light_specular = light_specular;
        scene_rasterizer.vsp.spot_dir_inner_vs = glm::vec4(spot_dir_vs, spot_inner_angle);
        scene_rasterizer.vsp.attn_spot_outer = glm::vec4(light_attn, spot_outer_angle);
        scene_rasterizer.vsp.material_ka = material_ka;
        scene_rasterizer.vsp.material_kd = material_kd;
        scene_rasterizer.vsp.material_ks = material_ks;
        scene_rasterizer.vsp.color = glm::vec4(1.0f);
        scene_rasterizer.vsp.params1 = glm::vec4(shadow_bias, shadow_normal_offset, 1.0f / shadow_map_size, 0.0f);
        for (const DrawItem& item : items)
        {
            Corona::Horizon::DrawIndexedParams params;
            params.index_type = Corona::Horizon::IndexType::UInt32;
            params.index_count = item.mesh->index_count;

            scene_rasterizer.model_pc.model = item.model; // per-draw；VS 从中计算 mvp/model_view/light_mtx
            scene_rasterizer.record(item.mesh->ib, item.mesh->vb, params);
        }

        Corona::Horizon::SubmitReceipt render_receipt =
            render_executor << pack_rasterizer(shadow_map_size, shadow_map_size)
                            << scene_rasterizer(smx_width, smx_height)
                            << Corona::Horizon::submit;

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
