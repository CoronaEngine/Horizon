// RSM（Reflective Shadow Maps，Dachsbacher & Stamminger 2005）EDSL 版，
// 与 example_rsm（GLSL 版）像素级对齐：
//   Pass 1 光源视角生成 RSM（世界坐标+光空间深度 / 世界法线 / flux），
//   Pass 2 直接光（世界空间 Blinn-Phong 聚光 + 硬阴影）+ RSM 单次弹射间接光。
// EDSL 侧差异：
//   - Pass 1 与 GLSL 版相同：单 pass MRT 三附件，片元里按顺序三次
//     `texture << value`（location 按调用顺序 0/1/2）。历史上这里曾拆成
//     三个单目标 pass 规避 device lost，根因是引擎的 renderTargetLocation
//     跨管线残留（RasterizedPipelineObject::parse 已修复,解析前复位）；
//   - 64 个重要性采样用 C++ 循环在 codegen 期展开，黄金比例螺旋的
//     偏移/权重直接以常量进 shader（GLSL 版是运行期循环算同一组值）；
//   - smoothstep 库里没有，手写 t*t*(3-2t)。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

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
#include "Codegen/BuiltinVariate.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

#include <glm/gtx/euler_angles.hpp>

namespace
{
ktm::fmat4x4 to_edsl_matrix(const glm::mat4& matrix)
{
    static_assert(sizeof(ktm::fmat4x4) == sizeof(glm::mat4));

    ktm::fmat4x4 result;
    std::memcpy(&result, &matrix, sizeof(result));
    return result;
}

constexpr uint32_t rsm_width = 1280;
constexpr uint32_t rsm_height = 720;
constexpr uint32_t rsm_map_size = 512; // RSM 分辨率（间接光照低频，从1024降低减少4倍开销）
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

// bgfx 的网格按 group 切分，每个 group 的顶点数天然 <=65535；索引保持 group
// 相对，base_vertex 在 draw 时通过 vertex_offset 补回，从而全程用 16-bit 索引。
struct MeshGroup
{
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    int32_t base_vertex = 0;
};

struct LoadedMesh
{
    std::vector<RsmVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<MeshGroup> groups;
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

            // 索引保持 group 相对（bgfx 原样的 16-bit 值），group 的顶点基址
            // 交给 draw 的 vertex_offset。否则 bunny/orb 这类多 group 网格
            // 累加后会越过 65535，16-bit 索引直接回绕。
            MeshGroup group;
            group.first_index = static_cast<uint32_t>(mesh.indices.size());
            group.index_count = num_indices;
            group.base_vertex = static_cast<int32_t>(group_base_vertex);
            mesh.groups.push_back(group);

            for (uint32_t i = 0; i < num_indices; ++i)
                mesh.indices.push_back(read_pod<uint16_t>(bytes, cursor));
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
    horizon::HardwareBuffer vb;
    horizon::HardwareBuffer ib;
    uint32_t index_count = 0;
    // 一个 group 一次 draw：index 是 group 相对的，base_vertex 走 vertex_offset。
    std::vector<MeshGroup> groups;
};

GpuMesh upload_mesh(const LoadedMesh& mesh, const std::string& name)
{
    return GpuMesh {
        horizon::HardwareBuffer::vertex(mesh.vertices, name + ".vb"),
        horizon::HardwareBuffer::index(mesh.indices, name + ".ib"),
        static_cast<uint32_t>(mesh.indices.size()),
        mesh.groups,
    };
}

// 预缩放的单面四边形（不剔除背面，绕向不敏感）
GpuMesh make_quad(const std::array<glm::vec3, 4>& corners, const glm::vec3& normal, const std::string& name)
{
    std::vector<RsmVertex> vertices;
    vertices.reserve(4);
    for (const glm::vec3& c : corners)
        vertices.push_back({ { c.x, c.y, c.z }, { normal.x, normal.y, normal.z } });
    const std::vector<uint16_t> indices = { 0, 1, 2, 1, 3, 2 };
    return GpuMesh {
        horizon::HardwareBuffer::vertex(vertices, name + ".vb"),
        horizon::HardwareBuffer::index(indices, name + ".ib"),
        static_cast<uint32_t>(indices.size()),
        { MeshGroup { 0, static_cast<uint32_t>(indices.size()), 0 } },
    };
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

using namespace EmbeddedShader;

// VS 输出聚合（对应 rsm_pack_vert.glsl 的 out 变量，location 按成员顺序）
struct RsmPackVertOut
{
    Float4 v_position;     // 光空间裁剪坐标（FS 取深度）
    Float3 v_world_pos;
    Float3 v_world_normal;
};

// VS 输出聚合（对应 rsm_scene_vert.glsl 的 out 变量）
struct RsmSceneVertOut
{
    Float3 v_world_pos;
    Float3 v_world_normal;
    Float4 v_shadowcoord;
};

void run_example_edsl_rsm()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(rsm_width, rsm_height, "Horizon RSM [EDSL]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    GpuMesh bunny = upload_mesh(load_bin_mesh(rsm_asset_root / "meshes" / "bunny.bin"), "example_edsl_rsm.bunny");
    GpuMesh column = upload_mesh(load_bin_mesh(rsm_asset_root / "meshes" / "column.bin"), "example_edsl_rsm.column");
    GpuMesh cube = upload_mesh(load_bin_mesh(rsm_asset_root / "meshes" / "cube.bin"), "example_edsl_rsm.cube");
    GpuMesh hollowcube = upload_mesh(load_bin_mesh(rsm_asset_root / "meshes" / "hollowcube.bin"), "example_edsl_rsm.hollowcube");

    // 地面 hplane（±1，法线 +Y，model 里缩放 550）
    GpuMesh floor_plane = make_quad(
        { glm::vec3(-1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f),
          glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 0.0f, -1.0f) },
        glm::vec3(0.0f, 1.0f, 0.0f), "example_edsl_rsm.floor");

    // 两面彩墙（预缩放顶点，避免非均匀缩放的法线问题）：间接光的颜色源
    GpuMesh wall_orange = make_quad(
        { glm::vec3(-35.0f, 0.0f, -30.0f), glm::vec3(-35.0f, 0.0f, 30.0f),
          glm::vec3(-35.0f, 24.0f, -30.0f), glm::vec3(-35.0f, 24.0f, 30.0f) },
        glm::vec3(1.0f, 0.0f, 0.0f), "example_edsl_rsm.wall_orange");
    GpuMesh wall_blue = make_quad(
        { glm::vec3(-30.0f, 0.0f, 35.0f), glm::vec3(30.0f, 0.0f, 35.0f),
          glm::vec3(-30.0f, 24.0f, 35.0f), glm::vec3(30.0f, 24.0f, 35.0f) },
        glm::vec3(0.0f, 0.0f, -1.0f), "example_edsl_rsm.wall_blue");

    // Pass 1 目标：1024x1024 RSM 三附件 + D32
    const auto rsm_rt_usage = horizon::ImageUsage_ColorAttachment |
                              horizon::ImageUsage_Sampled;

    // xyz: 世界坐标, w: 光空间深度（清屏 w=1 → 最远，阴影比较视为无遮挡）
    horizon::HardwareImage rsm_position_image(horizon::HardwareImageDesc::texture_2d(
        rsm_map_size, rsm_map_size, horizon::Format::RGBA32_FLOAT, rsm_rt_usage, "example_edsl_rsm.position"));
    rsm_position_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    horizon::HardwareImage rsm_normal_image(horizon::HardwareImageDesc::texture_2d(
        rsm_map_size, rsm_map_size, horizon::Format::RGBA16_FLOAT, rsm_rt_usage, "example_edsl_rsm.normal"));
    rsm_normal_image.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f); // 零法线 → gather 无贡献

    horizon::HardwareImage rsm_flux_image(horizon::HardwareImageDesc::texture_2d(
        rsm_map_size, rsm_map_size, horizon::Format::RGBA16_FLOAT, rsm_rt_usage, "example_edsl_rsm.flux"));
    rsm_flux_image.set_clear_color(0.0f, 0.0f, 0.0f, 0.0f); // 零 flux → gather 无贡献

    horizon::HardwareImage rsm_depth_image(horizon::HardwareImageDesc::depth_attachment(
        rsm_map_size, rsm_map_size, horizon::Format::D32, "example_edsl_rsm.rsm_depth"));
    rsm_depth_image.set_clear_depth(1.0f, 0);

    // Pass 2 目标：主输出
    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        rsm_width, rsm_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_edsl_rsm.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    horizon::HardwareImage depth_image(horizon::HardwareImageDesc::depth_attachment(
        rsm_width, rsm_height, horizon::Format::D32, "example_edsl_rsm.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    // ========================================================================
    // Pass 1：RSM 生成（EDSL，单 pass MRT：position / normal / flux）
    // ========================================================================
    // 混合状态给一份即可,运行时会复制到全部颜色附件
    horizon::RasterizerPipelineDesc pack_desc;
    pack_desc.blend_enabled = false;

    Texture2D<ktm::fvec4> pack_out_position = rsm_position_image;
    Texture2D<ktm::fvec4> pack_out_normal = rsm_normal_image;
    Texture2D<ktm::fvec4> pack_out_flux = rsm_flux_image;

    // pack 共享 uniform
    Float4x4 pk_light_view_proj;
    Float4 pk_light_pos_ws; // xyz: 光源位置
    Float4 pk_light_dir_ws; // xyz: 聚光方向（已归一化）
    Float4 pk_light_color;  // rgb: 光色
    Float4 pk_spot_params;  // x: cos(inner), y: cos(outer)

    // per-draw push constant
    Float4x4 pk_model;
    Float4 pk_albedo;
    pk_model.as_push_constant();
    pk_albedo.as_push_constant();

    // smoothstep 库里没有，手写（GLSL 版用原生 smoothstep）
    auto smoothstep_f = [&](Float edge0, Float edge1, Float x) {
        auto t = clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
        return t * t * (3.0f - 2.0f * t);
    };

    auto rsm_pack_vert = [&](Float3 pos, Float3 normal) {
        Aggregate<RsmPackVertOut> out;
        auto world_pos = mul(pk_model, Float4(pos, 1.f));
        auto clip = mul(pk_light_view_proj, world_pos);
        position() = clip;
        out->v_position = clip;
        out->v_world_pos = world_pos->xyz();
        // 场景无非均匀缩放（墙/地面用预缩放顶点数据），model 直接变换法线即可
        out->v_world_normal = normalize(mul(pk_model, Float4(normal, 0.f))->xyz());
        return out;
    };

    // MRT:三次 << 依调用顺序落在 location 0/1/2
    auto rsm_pack_frag = [&](Aggregate<RsmPackVertOut> in) {
        auto depth = in->v_position->z / in->v_position->w;
        pack_out_position << Float4(in->v_world_pos, depth);
        pack_out_normal << Float4(normalize(in->v_world_normal), 0.f);
        auto to_frag = normalize(in->v_world_pos - pk_light_pos_ws->xyz());
        auto cosang = dot(to_frag, pk_light_dir_ws->xyz());
        auto falloff = smoothstep_f(pk_spot_params->y, pk_spot_params->x, cosang);
        // flux 按论文不含距离衰减：albedo * 光色 * 聚光锥衰减
        pack_out_flux << Float4(pk_albedo->xyz() * pk_light_color->xyz() * falloff, 1.f);
    };

    // ========================================================================
    // Pass 2：场景直接光 + 硬阴影 + RSM 间接光（EDSL）
    // ========================================================================
    Texture2D<ktm::fvec4> scene_out_color = final_output_image;
    Texture2D<ktm::fvec4> rsm_position_map = rsm_position_image;
    Texture2D<ktm::fvec4> rsm_normal_map = rsm_normal_image;
    Texture2D<ktm::fvec4> rsm_flux_map = rsm_flux_image;

    Float4x4 proj_view;
    Float4x4 light_proj_view; // bias * light_proj * light_view
    Float4 light_pos_ws;      // xyz: 光源位置, w: 光强（直接光）
    Float4 light_dir_ws;      // xyz: 聚光方向, w: 环境光强度
    Float4 light_color;       // rgb: 光色
    Float4 spot_params;       // x: cos(inner), y: cos(outer), z: shadow bias, w: normal offset
    Float4 rsm_params;        // x: 采样半径（uv）, y: 间接光强度, z: debug 模式
    Float4 camera_pos_ws;     // xyz: 相机位置（高光用）

    Float4x4 model;
    Float4 albedo;
    model.as_push_constant();
    albedo.as_push_constant();

    auto rsm_scene_vert = [&](Float3 pos, Float3 normal) {
        Aggregate<RsmSceneVertOut> out;
        auto world_pos = mul(model, Float4(pos, 1.f));
        position() = mul(proj_view, world_pos);
        auto n = normalize(mul(model, Float4(normal, 0.f))->xyz());
        out->v_world_pos = world_pos->xyz();
        out->v_world_normal = n;
        // 世界空间沿法线偏移再投光空间，缓解 shadow acne（配合深度 bias）
        auto pos_offset = world_pos->xyz() + n * spot_params->w;
        out->v_shadowcoord = mul(light_proj_view, Float4(pos_offset, 1.f));
        return out;
    };

    auto hardShadow = [&](Float4 shadowCoord, Float bias) {
        Float result;
        auto uv = shadowCoord->xy() / shadowCoord->w;
        auto outside = any(uv > Float2(1.0f, 1.0f)) || any(uv < Float2(0.0f, 0.0f));
        $IF (outside) result = Float(1.0f);
        $ELSE
        {
            auto receiver = shadowCoord->z / shadowCoord->w - bias;
            auto pos_sample = texture(rsm_position_map, uv);
            result = step(receiver, pos_sample->w);
        }
        return result;
    };

    // RSM gather：64 个像素光，论文式(1) 的单次弹射。C++ 循环在 codegen 期
    // 展开，黄金比例螺旋偏移/ξ1² 权重以常量进 shader。
    auto indirect_gather = [&](Float2 rsm_uv, Float3 x, Float3 n) {
        constexpr int kSampleCount = 64;
        constexpr float kGolden = 0.61803398875f;
        constexpr float kTwoPi = 6.28318530718f;
        Float3 sum;
        sum = Float3(0.f, 0.f, 0.f);
        for (int i = 0; i < kSampleCount; ++i)
        {
            const float xi1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSampleCount);
            const float xi2 = std::fmod(static_cast<float>(i) * kGolden, 1.0f);
            const float ox = xi1 * std::cos(kTwoPi * xi2);
            const float oy = xi1 * std::sin(kTwoPi * xi2);
            const float weight = xi1 * xi1; // ξ1² 补偿中心密集的采样分布

            // 出界钳到边缘（清屏 texel flux=0，无贡献）
            auto uv = min(max(rsm_uv + Float2(ox, oy) * rsm_params->x, Float2(0.0f, 0.0f)), Float2(1.0f, 1.0f));
            auto xp_sample = texture(rsm_position_map, uv);
            auto np_sample = texture(rsm_normal_map, uv);
            auto flux_sample = texture(rsm_flux_map, uv);

            auto w = x - xp_sample->xyz();           // 像素光 → 着色点（未归一化，d² 折进分母）
            auto d2 = max(dot(w, w), Float(1.0f));   // 近距钳制防 firefly
            auto e = max(0.f, dot(np_sample->xyz(), w)) * max(0.f, dot(n, -w)) / (d2 * d2);
            sum = sum + flux_sample->xyz() * (e * Float(weight));
        }
        return sum;
    };

    auto rsm_scene_frag = [&](Aggregate<RsmSceneVertOut> in) {
        auto n = normalize(in->v_world_normal);
        auto x = in->v_world_pos;

        // 直接光：聚光 Blinn-Phong + 距离衰减
        auto to_light = light_pos_ws->xyz() - x;
        auto dist = length(to_light);
        auto l = to_light / dist;
        auto cosang = dot(-l, light_dir_ws->xyz());
        auto spot = smoothstep_f(spot_params->y, spot_params->x, cosang);
        auto attn = light_pos_ws->w / max(dist * dist, Float(1.0f));

        auto v = normalize(camera_pos_ws->xyz() - x);
        auto h = normalize(l + v);
        auto ndotl = max(0.f, dot(n, l));
        auto spec = pow(max(0.f, dot(n, h)), Float(32.0f)) * Float(0.25f);

        auto visibility = hardShadow(in->v_shadowcoord, spot_params->z);

        auto base = albedo->xyz();
        auto direct = (base * ndotl + Float3(spec, spec, spec)) * light_color->xyz() * (spot * attn * visibility);
        auto ambient = base * light_dir_ws->w;

        // 间接光：着色点投到 RSM uv，在周围 gather 像素光
        auto suv = in->v_shadowcoord->xy() / in->v_shadowcoord->w;
        auto rsm_uv = min(max(suv, Float2(0.0f, 0.0f)), Float2(1.0f, 1.0f));
        auto indirect = base * indirect_gather(rsm_uv, x, n) * rsm_params->y;

        // debug 选择：0 → Final, 1 → Direct Only, 2 → Indirect Only
        auto full = ambient + direct + indirect;
        auto direct_only = ambient + direct;
        auto sel1 = step(Float(0.5f), rsm_params->z);
        auto sel2 = step(Float(1.5f), rsm_params->z);
        auto final_color = mix(mix(full, direct_only, sel1), indirect, sel2);

        auto t = 1.f / 2.2f;
        scene_out_color << Float4(pow(abs(final_color), Float3(t, t, t)), 1.f);
    };

    horizon::RasterizerPipeline pack_rasterizer(rsm_pack_vert, rsm_pack_frag, pack_desc);
    pack_rasterizer.bind_depth_target(rsm_depth_image);

    horizon::RasterizerPipelineDesc scene_desc;
    scene_desc.blend_enabled = false;

    horizon::RasterizerPipeline scene_rasterizer(rsm_scene_vert, rsm_scene_frag, scene_desc);
    scene_rasterizer.bind_depth_target(depth_image);

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

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

    const glm::vec3 c_light_color(1.0f, 0.96f, 0.9f);

    // ---- RSM 可调参数 ----
    float indirect_intensity = 4.0f;
    float sample_radius = 0.30f; // 光空间 uv 半径
    float light_power = 850.0f;
    float ambient_strength = 0.03f;
    int debug_mode = 0;

    HorizonImGuiLayer ui(window, rsm_width, rsm_height);

    const auto start_time = std::chrono::high_resolution_clock::now();
    auto prev_time = start_time;
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;
    // HORIZON_FIXED_DT=<秒>：用固定步长取代墙钟，使逐帧画面可复现（仅供像素比对）。
    const float fixed_dt = [] {
        if (const char* value = std::getenv("HORIZON_FIXED_DT"))
        {
            return static_cast<float>(std::atof(value));
        }
        return 0.0f;
    }();
    uint64_t frame_index = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();
        ImGui::Begin("RSM");
        ImGui::SliderFloat("Indirect Intensity", &indirect_intensity, 0.0f, 20.0f);
        ImGui::SliderFloat("Sample Radius", &sample_radius, 0.02f, 0.6f);
        ImGui::SliderFloat("Light Power", &light_power, 100.0f, 3000.0f);
        ImGui::SliderFloat("Ambient", &ambient_strength, 0.0f, 0.2f);
        ImGui::Combo("Debug View", &debug_mode, "Final\0Direct Only\0Indirect Only\0");
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prev_time).count();
        float time = std::chrono::duration<float>(now - start_time).count();
        prev_time = now;
        if (fixed_dt > 0.0f)
        {
            dt = fixed_dt;
            time = fixed_dt * static_cast<float>(frame_index);
        }
        ++frame_index;

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[160];
            std::snprintf(title, sizeof(title), "Horizon RSM [EDSL] (spot/64 samples) - %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        // 聚光灯：绕场景旋转，指向原点
        const glm::vec3 c_light_pos(std::cos(time * 0.5f) * 20.0f, 26.0f, std::sin(time * 0.5f) * 20.0f);
        const glm::vec3 c_spot_dir = glm::normalize(-c_light_pos);

        const glm::mat4 c_light_view = glm::lookAtLH(c_light_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 light_view_proj = light_proj * c_light_view;
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

        // Pass 1：光源视角生成 RSM（position+depth / normal / flux 三个单目标 pass）- Convert to indirect
        pack_rasterizer.clear_records();
        pk_light_view_proj = to_edsl_matrix(light_view_proj);
        pk_light_pos_ws = ktm::fvec4(c_light_pos.x, c_light_pos.y, c_light_pos.z, 0.0f);
        pk_light_dir_ws = ktm::fvec4(c_spot_dir.x, c_spot_dir.y, c_spot_dir.z, 0.0f);
        pk_light_color = ktm::fvec4(c_light_color.x, c_light_color.y, c_light_color.z, 0.0f);
        pk_spot_params = ktm::fvec4(cos_inner, cos_outer, 0.0f, 0.0f);

        std::vector<horizon::DrawIndexedIndirectCommand> pack_indirect_cmds;
        for (const DrawItem& item : items)
        {
            pk_model = to_edsl_matrix(item.model);
            pk_albedo = ktm::fvec4(item.albedo.x, item.albedo.y, item.albedo.z, item.albedo.w);
            for (const MeshGroup& group : item.mesh->groups)
            {
                horizon::DrawIndexedIndirectCommand cmd;
                cmd.index_count = group.index_count;
                cmd.first_index = group.first_index;
                cmd.vertex_offset = group.base_vertex;
                cmd.instance_count = 1;
                cmd.first_instance = static_cast<uint32_t>(pack_indirect_cmds.size());
                pack_indirect_cmds.push_back(cmd);
            }
        }

        if (!pack_indirect_cmds.empty())
        {
            horizon::HardwareBuffer pack_indirect_buffer = horizon::HardwareBuffer::from_bytes(
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(pack_indirect_cmds.data()),
                    pack_indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                static_cast<uint32_t>(pack_indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
                "example_edsl_rsm.pack_indirect");

            const DrawItem& first_item = items[0];
            horizon::DrawIndexedIndirectParams pack_params;
            pack_params.draw_count = static_cast<uint32_t>(pack_indirect_cmds.size());
            pack_params.indirect_offset = 0;
            pack_params.stride = sizeof(horizon::DrawIndexedIndirectCommand);
            pack_rasterizer.record_indirect(first_item.mesh->ib, first_item.mesh->vb, pack_indirect_buffer, pack_params);
        }

        // Pass 2：场景直接光 + 硬阴影 + RSM 间接光 - Convert to indirect
        scene_rasterizer.clear_records();
        proj_view = to_edsl_matrix(view_proj);
        light_proj_view = to_edsl_matrix(shadow_mtx); // bias * light_proj * light_view
        light_pos_ws = ktm::fvec4(c_light_pos.x, c_light_pos.y, c_light_pos.z, light_power);
        light_dir_ws = ktm::fvec4(c_spot_dir.x, c_spot_dir.y, c_spot_dir.z, ambient_strength);
        light_color = ktm::fvec4(c_light_color.x, c_light_color.y, c_light_color.z, 0.0f);
        spot_params = ktm::fvec4(cos_inner, cos_outer, rsm_shadow_bias, rsm_normal_offset);
        rsm_params = ktm::fvec4(sample_radius, indirect_intensity, static_cast<float>(debug_mode), 0.0f);
        camera_pos_ws = ktm::fvec4(eye.x, eye.y, eye.z, 0.0f);

        std::vector<horizon::DrawIndexedIndirectCommand> scene_indirect_cmds;
        for (const DrawItem& item : items)
        {
            model = to_edsl_matrix(item.model);
            albedo = ktm::fvec4(item.albedo.x, item.albedo.y, item.albedo.z, item.albedo.w);
            for (const MeshGroup& group : item.mesh->groups)
            {
                horizon::DrawIndexedIndirectCommand cmd;
                cmd.index_count = group.index_count;
                cmd.first_index = group.first_index;
                cmd.vertex_offset = group.base_vertex;
                cmd.instance_count = 1;
                cmd.first_instance = static_cast<uint32_t>(scene_indirect_cmds.size());
                scene_indirect_cmds.push_back(cmd);
            }
        }

        if (!scene_indirect_cmds.empty())
        {
            horizon::HardwareBuffer scene_indirect_buffer = horizon::HardwareBuffer::from_bytes(
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(scene_indirect_cmds.data()),
                    scene_indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                static_cast<uint32_t>(scene_indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
                "example_edsl_rsm.scene_indirect");

            const DrawItem& first_item = items[0];
            horizon::DrawIndexedIndirectParams scene_params;
            scene_params.draw_count = static_cast<uint32_t>(scene_indirect_cmds.size());
            scene_params.indirect_offset = 0;
            scene_params.stride = sizeof(horizon::DrawIndexedIndirectCommand);
            scene_rasterizer.record_indirect(first_item.mesh->ib, first_item.mesh->vb, scene_indirect_buffer, scene_params);
        }

        horizon::SubmitReceipt render_receipt =
            render_executor << pack_rasterizer.extent(rsm_map_size, rsm_map_size)
                            << scene_rasterizer.extent(rsm_width, rsm_height)
                            << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
