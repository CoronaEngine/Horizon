// RSM（Reflective Shadow Maps，Dachsbacher & Stamminger 2005）示例。
//
// 在 example_shadowmaps 的两 pass 结构上扩展：光源视角 pass 从单张打包深度
// 变成 MRT 三附件（世界坐标+深度 / 世界法线 / flux），场景 pass 在硬阴影
// 直接光之外，把 RSM 的每个 texel 当成一个像素光（VPL），围绕着色点在光空间
// uv 上重要性采样 64 个，按论文式(1) 累加单次弹射间接光——彩墙的颜色会
// bleed 到白色物体和地面上。
//
// 场景：shadowmaps 场景（550 地面 + bunny + hollowcube + cube + 环形柱子、
// 聚光灯绕场景旋转）+ 两面彩色墙（橙 x=-35、蓝 z=+35，给间接光提供颜色源），
// 每个物体带 albedo（push constant），相机固定 (0,60,-105) 俯视。
// imgui 可调：间接光强度 / 采样半径 / 光强 / debug 视图（Final|Direct|Indirect）。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/rsm_pack_vert.glsl)
#include GLSL(shaders/rsm_pack_frag.glsl)
#include GLSL(shaders/rsm_scene_vert.glsl)
#include GLSL(shaders/rsm_scene_frag.glsl)

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
constexpr uint32_t rsm_width = 1280;
constexpr uint32_t rsm_height = 720;
constexpr uint32_t rsm_map_size = 1024; // RSM 分辨率（阴影比较共用）
constexpr float rsm_near = 1.0f;
constexpr float rsm_far = 250.0f;
constexpr float rsm_shadow_bias = 0.0035f;
constexpr float rsm_normal_offset = 0.15f; // 世界空间沿法线偏移
constexpr float coverage_spot = 90.0f;     // RSM 覆盖角（光源投影 fovy）
constexpr float spot_outer_angle = 45.0f;
constexpr float spot_inner_angle = 30.0f;

const std::filesystem::path rsm_asset_root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

// ============================================================================
// 二进制网格加载（与 example_shadowmaps 相同的未压缩 VB/IB chunk 解析）
// ============================================================================

struct RsmVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

struct LoadedMesh
{
    std::vector<RsmVertex> vertices;
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

                RsmVertex vertex;
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

// 预缩放的单面四边形（默认 CullMode::None，绕向不敏感）
GpuMesh make_quad(const std::array<glm::vec3, 4>& corners, const glm::vec3& normal, const std::string& name)
{
    std::vector<RsmVertex> vertices;
    vertices.reserve(4);
    for (const glm::vec3& c : corners)
        vertices.push_back({ { c.x, c.y, c.z }, { normal.x, normal.y, normal.z } });
    const std::vector<uint32_t> indices = { 0, 1, 2, 1, 3, 2 };
    return GpuMesh {
        Corona::Horizon::HardwareBuffer::vertex(vertices, name + ".vb"),
        Corona::Horizon::HardwareBuffer::index(indices, name + ".ib"),
        static_cast<uint32_t>(indices.size()),
    };
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

void run_example_rsm()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(rsm_width, rsm_height, "Horizon RSM [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    GpuMesh bunny = upload_mesh(load_bin_mesh(rsm_asset_root / "meshes" / "bunny.bin"), "example_rsm.bunny");
    GpuMesh column = upload_mesh(load_bin_mesh(rsm_asset_root / "meshes" / "column.bin"), "example_rsm.column");
    GpuMesh cube = upload_mesh(load_bin_mesh(rsm_asset_root / "meshes" / "cube.bin"), "example_rsm.cube");
    GpuMesh hollowcube = upload_mesh(load_bin_mesh(rsm_asset_root / "meshes" / "hollowcube.bin"), "example_rsm.hollowcube");

    // 地面 hplane（±1，法线 +Y，model 里缩放 550）
    GpuMesh floor_plane = make_quad(
        { glm::vec3(-1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f),
          glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 0.0f, -1.0f) },
        glm::vec3(0.0f, 1.0f, 0.0f), "example_rsm.floor");

    // 两面彩墙（预缩放顶点，避免非均匀缩放的法线问题）：间接光的颜色源
    GpuMesh wall_orange = make_quad(
        { glm::vec3(-35.0f, 0.0f, -30.0f), glm::vec3(-35.0f, 0.0f, 30.0f),
          glm::vec3(-35.0f, 24.0f, -30.0f), glm::vec3(-35.0f, 24.0f, 30.0f) },
        glm::vec3(1.0f, 0.0f, 0.0f), "example_rsm.wall_orange");
    GpuMesh wall_blue = make_quad(
        { glm::vec3(-30.0f, 0.0f, 35.0f), glm::vec3(30.0f, 0.0f, 35.0f),
          glm::vec3(-30.0f, 24.0f, 35.0f), glm::vec3(30.0f, 24.0f, 35.0f) },
        glm::vec3(0.0f, 0.0f, -1.0f), "example_rsm.wall_blue");

    // Pass 1 目标：1024x1024 RSM 三附件 + D32
    const auto rsm_rt_usage = Corona::Horizon::ImageUsageFlags::ColorAttachment |
                              Corona::Horizon::ImageUsageFlags::Sampled;

    // xyz: 世界坐标, w: 光空间深度（清屏 w=1 → 最远，阴影比较视为无遮挡）
    Corona::Horizon::HardwareImage rsm_position_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        rsm_map_size, rsm_map_size, Corona::Horizon::Format::RGBA32_FLOAT, rsm_rt_usage, "example_rsm.position"));
    rsm_position_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    Corona::Horizon::HardwareImage rsm_normal_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        rsm_map_size, rsm_map_size, Corona::Horizon::Format::RGBA16_FLOAT, rsm_rt_usage, "example_rsm.normal"));
    rsm_normal_image.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f); // 零法线 → gather 无贡献

    Corona::Horizon::HardwareImage rsm_flux_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        rsm_map_size, rsm_map_size, Corona::Horizon::Format::RGBA16_FLOAT, rsm_rt_usage, "example_rsm.flux"));
    rsm_flux_image.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f); // 零 flux → gather 无贡献

    Corona::Horizon::HardwareImage rsm_depth_image(Corona::Horizon::HardwareImageDesc::depth_attachment(
        rsm_map_size, rsm_map_size, Corona::Horizon::Format::D32, "example_rsm.rsm_depth"));
    rsm_depth_image.set_clear_depth(1.0f, 0);

    // Pass 2 目标：主输出
    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        rsm_width, rsm_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_rsm.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    Corona::Horizon::HardwareImage depth_image(Corona::Horizon::HardwareImageDesc::depth_attachment(
        rsm_width, rsm_height, Corona::Horizon::Format::D32, "example_rsm.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    Corona::Horizon::RasterizerPipelineDesc pack_desc;
    pack_desc.blend_enabled = false;

    Corona::Horizon::RasterizerPipeline pack_rasterizer(rsm_pack_vert_glsl, rsm_pack_frag_glsl, pack_desc);
    pack_rasterizer.outPosition = rsm_position_image;
    pack_rasterizer.outNormal = rsm_normal_image;
    pack_rasterizer.outFlux = rsm_flux_image;
    pack_rasterizer.bind_depth_target(rsm_depth_image);

    Corona::Horizon::RasterizerPipelineDesc scene_desc;
    scene_desc.blend_enabled = false;

    Corona::Horizon::RasterizerPipeline scene_rasterizer(rsm_scene_vert_glsl, rsm_scene_frag_glsl, scene_desc);
    scene_rasterizer.outColor = final_output_image;
    scene_rasterizer.bind_depth_target(depth_image);
    // RSM 三张图存入 bindless combined-texture 表（set 0），索引经 push constant 传入。
    scene_rasterizer.model_pc.rsmPositionIndex = rsm_position_image.store_descriptor();
    scene_rasterizer.model_pc.rsmNormalIndex = rsm_normal_image.store_descriptor();
    scene_rasterizer.model_pc.rsmFluxIndex = rsm_flux_image.store_descriptor();

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    constexpr float aspect = static_cast<float>(rsm_width) / static_cast<float>(rsm_height);
    // 与 shadowmaps 相同的固定俯视相机
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
        glm::mat4 m = glm::perspectiveLH(glm::radians(coverage_spot), 1.0f, rsm_near, rsm_far);
        m[1][1] *= -1.0f;
        return m;
    }();

    // NDC → uv 的 bias 矩阵（Vulkan：z 恒等映射，与 pack 侧一致）
    const glm::mat4 shadow_bias_mtx = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.0f)) *
                                      glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 1.0f));

    const glm::vec3 light_color(1.0f, 0.96f, 0.9f);

    // ---- RSM 可调参数 ----
    float indirect_intensity = 4.0f;
    float sample_radius = 0.30f; // 光空间 uv 半径
    float light_power = 850.0f;
    float ambient = 0.03f;
    int debug_mode = 0;

    HorizonImGuiLayer ui(window, rsm_width, rsm_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();
        ImGui::Begin("RSM");
        ImGui::SliderFloat("Indirect Intensity", &indirect_intensity, 0.0f, 20.0f);
        ImGui::SliderFloat("Sample Radius", &sample_radius, 0.02f, 0.6f);
        ImGui::SliderFloat("Light Power", &light_power, 100.0f, 3000.0f);
        ImGui::SliderFloat("Ambient", &ambient, 0.0f, 0.2f);
        ImGui::Combo("Debug View", &debug_mode, "Final\0Direct Only\0Indirect Only\0");
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
            std::snprintf(title, sizeof(title), "Horizon RSM [Vulkan] (spot/64 samples) - %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // 聚光灯：绕场景旋转，指向原点
        const glm::vec3 light_pos(std::cos(time * 0.5f) * 20.0f, 26.0f, std::sin(time * 0.5f) * 20.0f);
        const glm::vec3 spot_dir = glm::normalize(-light_pos);

        const glm::mat4 light_view = glm::lookAtLH(light_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 light_view_proj = light_proj * light_view;
        const glm::mat4 shadow_mtx = shadow_bias_mtx * light_view_proj;

        // 场景物体：albedo 走 push constant；白色物体等着被彩墙 bleed
        struct DrawItem
        {
            const GpuMesh* mesh;
            glm::mat4 model;
            glm::vec4 albedo;
        };
        std::vector<DrawItem> items;
        items.reserve(6 + 10);
        items.push_back({ &floor_plane, glm::scale(glm::mat4(1.0f), glm::vec3(550.0f)),
                          glm::vec4(0.75f, 0.75f, 0.75f, 1.0f) });
        items.push_back({ &wall_orange, glm::mat4(1.0f), glm::vec4(0.90f, 0.42f, 0.10f, 1.0f) });
        items.push_back({ &wall_blue, glm::mat4(1.0f), glm::vec4(0.15f, 0.35f, 0.90f, 1.0f) });
        items.push_back({ &bunny,
                          glm::translate(glm::mat4(1.0f), glm::vec3(15.0f, 5.0f, 0.0f)) *
                              glm::eulerAngleY(time - 1.56f) * glm::scale(glm::mat4(1.0f), glm::vec3(5.0f)),
                          glm::vec4(0.85f, 0.85f, 0.85f, 1.0f) });
        items.push_back({ &hollowcube,
                          glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)) *
                              glm::eulerAngleY(time - 1.56f) * glm::scale(glm::mat4(1.0f), glm::vec3(2.5f)),
                          glm::vec4(0.20f, 0.75f, 0.25f, 1.0f) });
        items.push_back({ &cube,
                          glm::translate(glm::mat4(1.0f), glm::vec3(-15.0f, 5.0f, 0.0f)) *
                              glm::eulerAngleY(time - 1.56f) * glm::scale(glm::mat4(1.0f), glm::vec3(2.5f)),
                          glm::vec4(0.85f, 0.18f, 0.12f, 1.0f) });
        constexpr int num_columns = 10;
        for (int i = 0; i < num_columns; ++i)
        {
            const float angle = i * 2.0f * glm::pi<float>() / num_columns;
            items.push_back({ &column,
                              glm::translate(glm::mat4(1.0f), glm::vec3(std::sin(angle) * 60.0f, 0.0f, std::cos(angle) * 60.0f)) *
                                  glm::eulerAngleY(-float(i)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)),
                              glm::vec4(0.6f, 0.6f, 0.6f, 1.0f) });
        }

        const float cos_inner = std::cos(glm::radians(spot_inner_angle));
        const float cos_outer = std::cos(glm::radians(spot_outer_angle));

        // Pass 1：光源视角生成 RSM（MRT：position+depth / normal / flux）
        pack_rasterizer.clear_records();
        pack_rasterizer.vsp.light_view_proj = light_view_proj;
        pack_rasterizer.vsp.light_pos_ws = glm::vec4(light_pos, 0.0f);
        pack_rasterizer.vsp.light_dir_ws = glm::vec4(spot_dir, 0.0f);
        pack_rasterizer.vsp.light_color = glm::vec4(light_color, 0.0f);
        pack_rasterizer.vsp.spot_params = glm::vec4(cos_inner, cos_outer, 0.0f, 0.0f);
        for (const DrawItem& item : items)
        {
            Corona::Horizon::DrawIndexedParams params;
            params.index_type = Corona::Horizon::IndexType::UInt32;
            params.index_count = item.mesh->index_count;

            pack_rasterizer.pack_pc.model = item.model;
            pack_rasterizer.pack_pc.albedo = item.albedo;
            pack_rasterizer.record(item.mesh->ib, item.mesh->vb, params);
        }

        // Pass 2：场景直接光 + 硬阴影 + RSM 间接光
        scene_rasterizer.clear_records();
        scene_rasterizer.vsp.proj_view = view_proj;
        scene_rasterizer.vsp.light_proj_view = shadow_mtx; // bias * light_proj * light_view
        scene_rasterizer.vsp.light_pos_ws = glm::vec4(light_pos, light_power);
        scene_rasterizer.vsp.light_dir_ws = glm::vec4(spot_dir, ambient);
        scene_rasterizer.vsp.light_color = glm::vec4(light_color, 0.0f);
        scene_rasterizer.vsp.spot_params = glm::vec4(cos_inner, cos_outer, rsm_shadow_bias, rsm_normal_offset);
        scene_rasterizer.vsp.rsm_params = glm::vec4(sample_radius, indirect_intensity, float(debug_mode), 0.0f);
        scene_rasterizer.vsp.camera_pos_ws = glm::vec4(eye, 0.0f);
        for (const DrawItem& item : items)
        {
            Corona::Horizon::DrawIndexedParams params;
            params.index_type = Corona::Horizon::IndexType::UInt32;
            params.index_count = item.mesh->index_count;

            scene_rasterizer.model_pc.model = item.model;
            scene_rasterizer.model_pc.albedo = item.albedo;
            scene_rasterizer.record(item.mesh->ib, item.mesh->vb, params);
        }

        Corona::Horizon::SubmitReceipt render_receipt =
            render_executor << pack_rasterizer(rsm_map_size, rsm_map_size)
                            << scene_rasterizer(rsm_width, rsm_height)
                            << Corona::Horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
