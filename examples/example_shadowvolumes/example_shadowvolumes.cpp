// 移植自参考示例 14-shadowvolumes（模板阴影体），取其 "stencil texture"
// 变体思路做聚焦移植：框架的动态渲染当前不接 stencil 附件，所以用
// R32F 计数纹理 + 加法混合替代双面 stencil（depth-pass 计数：正面 +1 /
// 背面 -1，计数为 0 = 光照可见），场景深度经 R32F 深度值 pass 提供给
// 阴影体 pass 做逐像素剔除（替代只读深度测试）。
//
// 场景（聚焦默认配置）：平台地面 + bunny 投影体、单个点光源绕场景旋转。
// 阴影体在 CPU 端逐帧生成：预建边->三角形邻接表，按光源方向判定正/背面，
// 轮廓边沿光线方向挤出成侧面四边形（depth-pass 技术不需要封盖）。
//
// pass 结构（单次提交链）：
//   1 ambient   -> 主目标（清屏）+ 深度
//   2 depthval  -> R32F 器件深度（供 pass 3 采样）
//   3 volume    -> R32F 计数目标（加法混合，无深度附件）
//   4 lit       -> 主目标（clear_color_target=false，加法混合叠加漫反射×可见性）

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/sv_scene_vert.glsl)
#include GLSL(shaders/sv_ambient_frag.glsl)
#include GLSL(shaders/sv_depthval_frag.glsl)
#include GLSL(shaders/sv_volume_vert.glsl)
#include GLSL(shaders/sv_volume_frag.glsl)
#include GLSL(shaders/sv_lit_frag.glsl)

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace
{
constexpr uint32_t sv_width = 1280;
constexpr uint32_t sv_height = 720;
constexpr float volume_extrude = 400.0f;

const std::filesystem::path sv_asset_root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

struct SvVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

struct LoadedMesh
{
    std::vector<SvVertex> vertices;
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

// 与 example_ibl 相同的 .bin 网格解析（未压缩 VB/IB chunk）
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

                SvVertex vertex;
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

// ============================================================================
// 阴影体几何：边->三角形邻接（按位置焊接顶点，忽略法线差异），
// 逐帧提取轮廓边并沿光线挤出
// ============================================================================

struct VolumeSource
{
    std::vector<glm::vec3> positions;          // 焊接后的位置表
    std::vector<std::array<uint32_t, 3>> tris; // 焊接后索引
    struct Edge
    {
        uint32_t v0, v1;
        int32_t tri[2] = { -1, -1 };
    };
    std::vector<Edge> edges;
};

VolumeSource build_volume_source(const LoadedMesh& mesh)
{
    VolumeSource src;

    // 位置焊接（相同坐标合并，消除法线拆分导致的假开缝）
    std::map<std::array<float, 3>, uint32_t> remap;
    std::vector<uint32_t> vertex_to_welded(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        auto [it, inserted] = remap.try_emplace(mesh.vertices[i].position, static_cast<uint32_t>(src.positions.size()));
        if (inserted)
            src.positions.emplace_back(mesh.vertices[i].position[0], mesh.vertices[i].position[1], mesh.vertices[i].position[2]);
        vertex_to_welded[i] = it->second;
    }

    std::map<std::pair<uint32_t, uint32_t>, size_t> edge_map;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
    {
        const uint32_t a = vertex_to_welded[mesh.indices[t]];
        const uint32_t b = vertex_to_welded[mesh.indices[t + 1]];
        const uint32_t c = vertex_to_welded[mesh.indices[t + 2]];
        if (a == b || b == c || a == c)
            continue;

        const int32_t tri_index = static_cast<int32_t>(src.tris.size());
        src.tris.push_back({ a, b, c });

        const std::array<std::pair<uint32_t, uint32_t>, 3> tri_edges = { { { a, b }, { b, c }, { c, a } } };
        for (const auto& e : tri_edges)
        {
            const auto key = std::minmax(e.first, e.second);
            auto [it, inserted] = edge_map.try_emplace({ key.first, key.second }, src.edges.size());
            if (inserted)
            {
                VolumeSource::Edge edge;
                edge.v0 = e.first; // 保留第一条出现时的方向（属于 tri[0] 的绕序）
                edge.v1 = e.second;
                edge.tri[0] = tri_index;
                src.edges.push_back(edge);
            }
            else
            {
                src.edges[it->second].tri[1] = tri_index;
            }
        }
    }
    return src;
}

// 轮廓边挤出侧面（depth-pass 计数不需要封盖）；world 空间。
void build_shadow_volume(const VolumeSource& src,
                         const glm::mat4& model,
                         const glm::vec3& light_pos,
                         std::vector<std::array<float, 3>>& out_positions)
{
    static std::vector<glm::vec3> world_pos;
    static std::vector<uint8_t> front;

    world_pos.resize(src.positions.size());
    for (size_t i = 0; i < src.positions.size(); ++i)
        world_pos[i] = glm::vec3(model * glm::vec4(src.positions[i], 1.0f));

    front.resize(src.tris.size());
    for (size_t t = 0; t < src.tris.size(); ++t)
    {
        const glm::vec3& p0 = world_pos[src.tris[t][0]];
        const glm::vec3& p1 = world_pos[src.tris[t][1]];
        const glm::vec3& p2 = world_pos[src.tris[t][2]];
        const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        front[t] = glm::dot(n, light_pos - p0) > 0.0f ? 1 : 0;
    }

    out_positions.clear();
    auto emit = [&out_positions](const glm::vec3& p) {
        out_positions.push_back({ p.x, p.y, p.z });
    };

    for (const VolumeSource::Edge& edge : src.edges)
    {
        const bool f0 = front[edge.tri[0]] != 0;
        const bool f1 = edge.tri[1] >= 0 ? front[edge.tri[1]] != 0 : false;
        if (f0 == f1)
            continue; // 不是轮廓边

        // 让边方向与"朝光面"的绕序一致，保证侧面法线朝体外
        uint32_t a = edge.v0;
        uint32_t b = edge.v1;
        if (!f0)
            std::swap(a, b);

        const glm::vec3& pa = world_pos[a];
        const glm::vec3& pb = world_pos[b];
        const glm::vec3 pa_ext = pa + glm::normalize(pa - light_pos) * volume_extrude;
        const glm::vec3 pb_ext = pb + glm::normalize(pb - light_pos) * volume_extrude;

        // 四边形 (a, b, b_ext) (a, b_ext, a_ext)
        emit(pa);
        emit(pb);
        emit(pb_ext);
        emit(pa);
        emit(pb_ext);
        emit(pa_ext);
    }
}

struct GpuMesh
{
    horizon::HardwareBuffer vb;
    horizon::HardwareBuffer ib;
    uint32_t index_count = 0;
};

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

void run_example_shadowvolumes()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(sv_width, sv_height, "Horizon ShadowVolumes [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    const LoadedMesh bunny_mesh = load_bin_mesh(sv_asset_root / "meshes" / "bunny.bin");
    const VolumeSource bunny_volume = build_volume_source(bunny_mesh);

    GpuMesh bunny {
        horizon::HardwareBuffer::vertex(bunny_mesh.vertices, "example_shadowvolumes.bunny.vb"),
        horizon::HardwareBuffer::index(bunny_mesh.indices, "example_shadowvolumes.bunny.ib"),
        static_cast<uint32_t>(bunny_mesh.indices.size()),
    };

    const std::vector<SvVertex> plane_vertices = {
        { { -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { -1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
    };
    const std::vector<uint32_t> plane_indices = { 0, 1, 2, 1, 3, 2 };
    GpuMesh floor_plane {
        horizon::HardwareBuffer::vertex(plane_vertices, "example_shadowvolumes.floor.vb"),
        horizon::HardwareBuffer::index(plane_indices, "example_shadowvolumes.floor.ib"),
        static_cast<uint32_t>(plane_indices.size()),
    };

    // 阴影体索引：恒等（CPU 生成纯三角形序列，上限按轮廓边数量放宽）
    constexpr uint32_t max_volume_vertices = 1 << 18;
    std::vector<uint32_t> identity_indices(max_volume_vertices);
    std::iota(identity_indices.begin(), identity_indices.end(), 0u);
    horizon::HardwareBuffer volume_ib = horizon::HardwareBuffer::index(identity_indices, "example_shadowvolumes.volume.ib");

    // 渲染目标
    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        sv_width, sv_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_shadowvolumes.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    horizon::HardwareImage depth_image(horizon::HardwareImageDesc::depth_attachment(
        sv_width, sv_height, horizon::Format::D32, "example_shadowvolumes.depth"));
    depth_image.set_clear_depth(1.0f, 0);
    horizon::HardwareImage depthval_depth_image(horizon::HardwareImageDesc::depth_attachment(
        sv_width, sv_height, horizon::Format::D32, "example_shadowvolumes.depthval.depth"));
    depthval_depth_image.set_clear_depth(1.0f, 0);
    horizon::HardwareImage lit_depth_image(horizon::HardwareImageDesc::depth_attachment(
        sv_width, sv_height, horizon::Format::D32, "example_shadowvolumes.lit.depth"));
    lit_depth_image.set_clear_depth(1.0f, 0);

    const auto rt_usage = horizon::ImageUsage_ColorAttachment | horizon::ImageUsage_Sampled;
    horizon::HardwareImage scene_depth_image(horizon::HardwareImageDesc::texture_2d(
        sv_width, sv_height, horizon::Format::R32_FLOAT, rt_usage, "example_shadowvolumes.scenedepth"));
    scene_depth_image.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f);

    horizon::HardwareImage count_image(horizon::HardwareImageDesc::texture_2d(
        sv_width, sv_height, horizon::Format::R32_FLOAT, rt_usage, "example_shadowvolumes.count"));
    count_image.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f);

    // pass 1: ambient
    horizon::RasterizerPipelineDesc ambient_desc;
    ambient_desc.blend_enabled = false;
    horizon::RasterizerPipeline ambient_rasterizer(sv_scene_vert_glsl, sv_ambient_frag_glsl, ambient_desc);
    ambient_rasterizer.outColor = final_output_image;
    ambient_rasterizer.bind_depth_target(depth_image);

    // pass 2: depthval
    horizon::RasterizerPipelineDesc depthval_desc;
    depthval_desc.blend_enabled = false;
    horizon::RasterizerPipeline depthval_rasterizer(sv_scene_vert_glsl, sv_depthval_frag_glsl, depthval_desc);
    depthval_rasterizer.outColor = scene_depth_image;
    depthval_rasterizer.bind_depth_target(depthval_depth_image);

    // pass 3: volume（加法混合、无深度附件）
    horizon::RasterizerPipelineDesc volume_desc;
    volume_desc.depth_test_enabled = false;
    volume_desc.depth_write_enabled = false;
    volume_desc.blend_enabled = true;
    volume_desc.src_color_blend_factor = horizon::BlendFactor::One;
    volume_desc.dst_color_blend_factor = horizon::BlendFactor::One;
    volume_desc.color_blend_op = horizon::BlendOp::Add;
    volume_desc.src_alpha_blend_factor = horizon::BlendFactor::One;
    volume_desc.dst_alpha_blend_factor = horizon::BlendFactor::One;
    volume_desc.alpha_blend_op = horizon::BlendOp::Add;
    horizon::RasterizerPipeline volume_rasterizer(sv_volume_vert_glsl, sv_volume_frag_glsl, volume_desc);
    volume_rasterizer.outColor = count_image;
    // scene depth 存入 bindless combined-texture 表（set 0），索引经 push constant 传入。
    volume_rasterizer.volume_pc.sceneDepthIndex = scene_depth_image.store_descriptor();

    // pass 4: lit（不清屏、加法混合叠到 ambient 上）
    horizon::RasterizerPipelineDesc lit_desc;
    lit_desc.clear_color_target = false;
    lit_desc.blend_enabled = true;
    lit_desc.src_color_blend_factor = horizon::BlendFactor::One;
    lit_desc.dst_color_blend_factor = horizon::BlendFactor::One;
    lit_desc.color_blend_op = horizon::BlendOp::Add;
    lit_desc.src_alpha_blend_factor = horizon::BlendFactor::One;
    lit_desc.dst_alpha_blend_factor = horizon::BlendFactor::One;
    lit_desc.alpha_blend_op = horizon::BlendOp::Add;
    horizon::RasterizerPipeline lit_rasterizer(sv_scene_vert_glsl, sv_lit_frag_glsl, lit_desc);
    lit_rasterizer.outColor = final_output_image;
    lit_rasterizer.bind_depth_target(lit_depth_image);
    // shadow count 存入 bindless combined-texture 表（set 0），索引经 push constant 传入。
    lit_rasterizer.model_pc.shadowCountIndex = count_image.store_descriptor();

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    constexpr float aspect = static_cast<float>(sv_width) / static_cast<float>(sv_height);
    // 原版自由相机初始位姿：pos(3,20,-58)、垂直角 -0.25 rad、yaw 0
    const glm::vec3 eye(3.0f, 20.0f, -58.0f);
    const glm::vec3 forward(0.0f, std::sin(-0.25f), std::cos(-0.25f));
    const glm::mat4 view = glm::lookAtLH(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    const glm::mat4 view_proj = proj * view;

    // 原版默认材质与光照参数
    const glm::vec4 ambient_color(0.05f, 0.05f, 0.05f, 1.0f);
    const glm::vec4 diffuse_color(0.8f, 0.8f, 0.8f, 1.0f);
    const glm::vec4 light_rgb(1.0f, 0.7f, 0.2f, 1.0f);            // 光色表第一盏：yellow
    const glm::vec4 specular_shininess(1.0f, 1.0f, 1.0f, 25.0f);
    const glm::vec4 fog_params(0.0f, 0.0f, 0.0f, 0.0055f);        // 黑雾，密度 0.0055
    constexpr float light_radius = 20.0f;

    std::vector<std::array<float, 3>> volume_positions;

    HorizonImGuiLayer ui(window, sv_width, sv_height);

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
            std::snprintf(title, sizeof(title), "Horizon ShadowVolumes [Vulkan] - %u volume tris - %.1f FPS (%.2f ms)",
                          static_cast<uint32_t>(volume_positions.size() / 3), fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        const glm::vec3 light_pos(std::cos(time * 1.1f + 3.0f) * 20.0f, 20.0f, std::sin(time * 1.1f + 3.0f) * 20.0f);
        glm::vec4 light_pos_vs = view * glm::vec4(light_pos, 1.0f);
        light_pos_vs.w = light_radius;
        const glm::vec4 resolution(float(sv_width), float(sv_height), 0.0f, 0.0f);

        const glm::mat4 mtx_floor = glm::scale(glm::mat4(1.0f), glm::vec3(500.0f));
        // 原版 Scene1 bunny：pos(0,0,0)、scale 5、rotY = pi（静止）
        const glm::mat4 mtx_bunny = glm::eulerAngleY(glm::pi<float>()) * glm::scale(glm::mat4(1.0f), glm::vec3(5.0f));

        struct DrawItem
        {
            const GpuMesh* mesh;
            glm::mat4 model;
        };
        const DrawItem items[2] = {
            { &floor_plane, mtx_floor },
            { &bunny, mtx_bunny },
        };

        auto record_scene = [&](auto& pipeline) {
            pipeline.clear_records();
            // 共享矩阵（UBO）；per-draw model 走 push constant
            pipeline.vsp.proj_view   = view_proj;
            pipeline.vsp.view_matrix = view;
            pipeline.vsp.light_pos_vs = light_pos_vs;
            pipeline.vsp.light_rgb = light_rgb;
            pipeline.vsp.ambient = ambient_color;
            pipeline.vsp.diffuse = diffuse_color;
            pipeline.vsp.specular_shininess = specular_shininess;
            pipeline.vsp.fog = fog_params;
            pipeline.vsp.color = glm::vec4(1.0f);
            pipeline.vsp.params = resolution;
            for (const DrawItem& item : items)
            {
                horizon::DrawIndexedParams params;
                params.index_count = item.mesh->index_count;

                pipeline.model_pc.model = item.model;
                pipeline.record(item.mesh->ib, item.mesh->vb, params);
            }
        };

        record_scene(ambient_rasterizer);
        record_scene(depthval_rasterizer);
        record_scene(lit_rasterizer);

        // CPU 生成 bunny 阴影体
        build_shadow_volume(bunny_volume, mtx_bunny, light_pos, volume_positions);

        volume_rasterizer.clear_records();
        if (!volume_positions.empty())
        {
            horizon::HardwareBuffer volume_vb =
                horizon::HardwareBuffer::vertex(volume_positions, "example_shadowvolumes.volume.vb");

            horizon::DrawIndexedParams params;
            params.index_count = std::min<uint32_t>(static_cast<uint32_t>(volume_positions.size()), max_volume_vertices);

            volume_rasterizer.vvp.view_proj = view_proj;
            volume_rasterizer.vvp.params = resolution;
            volume_rasterizer.record(volume_ib, volume_vb, params);
        }

        horizon::SubmitReceipt render_receipt =
            render_executor << ambient_rasterizer.extent(sv_width, sv_height)
                            << depthval_rasterizer.extent(sv_width, sv_height)
                            << volume_rasterizer.extent(sv_width, sv_height)
                            << lit_rasterizer.extent(sv_width, sv_height)
                            << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
