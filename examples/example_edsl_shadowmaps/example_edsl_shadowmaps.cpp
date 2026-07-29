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

// #include GLSL(shaders/shadowmaps_pack_vert.glsl)
// #include GLSL(shaders/shadowmaps_pack_frag.glsl)
// #include GLSL(shaders/shadowmaps_scene_vert.glsl)
// #include GLSL(shaders/shadowmaps_scene_frag.glsl)

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


using namespace EmbeddedShader;
// 顶点输入必须用 Aggregate 而不是多个独立参数：多参数那条路径
// （VariateProxy.h:177 走 ParseHelper::getCurrentInputIndex()，递减）会把
// vertex input location 分配反，position 和 normal 会互换。Aggregate 走的是
// VariateProxy.h:213 那条显式递增的展开路径，location 与成员声明顺序一致。
// 其它 edsl 例子（edsl / edsl_ibl / edsl_sky）都用 Aggregate，所以没踩到。
struct ShadowMapsVertexIn
{
    Float3 pos;    // location 0 —— 与 AoVertex.position 对应
    Float3 normal; // location 1 —— 与 AoVertex.normal 对应
};

struct ShadowMapsSceneVertOut
{
    Float3 v_normal;
    Float3 v_view;
    Float4 v_shadowcoord;
};

void run_example_edsl_shadowmaps()
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

    Texture2D<ktm::fvec4> pack_out_color = shadow_map_image;
    Float4x4 mvp;
    mvp.as_push_constant();
    auto shadowmaps_pack_vert = [&](Aggregate<ShadowMapsVertexIn> vin) {
        position() = mul(mvp,Float4(vin->pos, 1.f));
        return position();
    };

    auto packFloatToRgba = [&](Float value) {
        auto shift = Float4(256.0 * 256.0 * 256.0, 256.0 * 256.0, 256.0, 1.0);
        auto mask = Float4(0.0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0);
        auto comp = fract(value * shift);
        auto nComp = comp - comp->xxyz() * mask;
        return nComp;
    };

    auto shadowmaps_pack_frag = [&](Float4 v_position) {
        auto depth = v_position->z / v_position->w;
        pack_out_color << packFloatToRgba(depth);
    };

    Texture2D<ktm::fvec4> scene_out_color = final_output_image;

    Float4x4 proj_view;
    Float4x4 view_matrix;
    Float4x4 light_proj_view;
    Float4 light_pos_vs;
    Float4 light_ambient;
    Float4 light_diffuse;
    Float4 light_specular;
    Float4 spot_dir_inner_vs;
    Float4 attn_spot_outer;
    Float4 params1;
    Float4 material_ka;
    Float4 material_kd;
    Float4 material_ks;
    Float4 color;

    Float4x4 model;
    model.as_push_constant();
    auto shadowmaps_scene_vert = [&](Aggregate<ShadowMapsVertexIn> vin) {
        Aggregate<ShadowMapsSceneVertOut> out;
        Float3 inPosition = vin->pos;
        Float3 inNormal   = vin->normal;
        auto mvp_       = mul(proj_view,       model);
        auto model_view = mul(view_matrix,     model);
        auto light_mtx = mul(light_proj_view, model);
        position() = mul(mvp_, Float4(inPosition, 1.f));
        out->v_normal = normalize(mul(model_view,Float4(inNormal,0.f))->xyz());
        out->v_view = mul(model_view,Float4(inPosition,1.f))->xyz();
        auto posOffset = inPosition + inNormal * params1->y;
        out->v_shadowcoord = mul(light_mtx, Float4(posOffset,1.f));
        return out;
    };

    auto unpackRgbaToFloat = [&](Float4 rgba) {
        auto shift = Float4(1.0 / (256.0 * 256.0 * 256.0), 1.0 / (256.0 * 256.0), 1.0 / 256.0, 1.0);
        return dot(rgba, shift);
    };

    auto attenuation = [&](Float dist, Float3 attn)
    {
        return Float(1.0f) / (attn->x + attn->y * dist + attn->z * dist * dist);
    };

    auto spotFalloff = [&](Float ldotsd, Float innerDeg, Float outerDeg)
    {
        auto inner = cos(radians(innerDeg));
        auto outer = cos(radians(min(outerDeg, innerDeg - Float(0.001f))));
        return clamp((ldotsd - inner) / (outer - inner), 0.0f, 1.0f);
    };

    auto lit = [&](Float3 ld, Float3 n, Float3 vd, Float exp_)
    {
        auto ndotl = dot(n, ld);
        auto r = 2.0f * ndotl * n - ld;
        auto rdotv = dot(r, vd);
        auto spec = step(0.0f, ndotl) * pow(max(0.0f, rdotv), exp_) * (2.0f + exp_) / Float(8.0f);
        return max(Float2(ndotl, spec), Float2(0.0f, 0.0f));
    };

    Texture2D<ktm::fvec4> shadowMap = shadow_map_image;
    auto hardShadow = [&](Float4 shadowCoord, Float bias)
    {
        Float result;
        auto texCoord = shadowCoord->xy() / shadowCoord->w;
        auto outside = any(texCoord > Float2(1.0f,1.0f)) || any(texCoord < Float2(0.0f,0.0f));
        $IF (outside) result = Float(1.0f);
        $ELSE
        {
            auto receiver = (shadowCoord->z - bias) / shadowCoord->w;
            auto occluder = unpackRgbaToFloat(texture(shadowMap, texCoord));
            result = step(receiver, occluder);
        }
        return result;
    };

    auto shadowmaps_scene_frag = [&](Aggregate<ShadowMapsSceneVertOut> in) {
        auto visibility = hardShadow(in->v_shadowcoord,params1->x);
        auto v = in->v_view;
        auto vd = -normalize(in->v_view);
        auto n = in->v_normal;

        auto l = light_pos_vs->xyz() - v;
        auto ld = normalize(l);
        auto ldotsd = max(0.f, dot(-ld, normalize(spot_dir_inner_vs->xyz())));
        auto falloff = spotFalloff(ldotsd,attn_spot_outer->w,spot_dir_inner_vs->w);
        auto attn = attenuation(length(l),attn_spot_outer->xyz()) * mix(falloff,1.f,step(90.f,attn_spot_outer->w));
        auto lc = lit(ld,n,vd,material_ks->w) * attn;

        auto ambi = light_ambient->xyz() * light_ambient->w * material_ka->xyz();
        auto diff = light_diffuse->xyz() * light_diffuse->w * material_kd->xyz() * lc->x;
        auto spec = light_specular->xyz() * light_specular->w * material_ks->xyz() * lc->y;

        auto fogColor = Float3(0.f,0.f,0.f);
        Float fogDensity = 0.0035f;
        Float LOG2 = 1.442695f;
        auto z = length(v);
        auto fogFactor = clamp(Float(1.f) / exp2(fogDensity * fogDensity * z * z * LOG2),0.f,1.f);
        auto baseColor = color->xyz();
        auto ambient = ambi * baseColor;
        auto brdf = (diff + spec) * baseColor * visibility;

        auto t = 1.f / 2.2f;
        auto final = pow(abs(ambient + brdf),Float3(t,t,t));
        scene_out_color << Float4(mix(fogColor,final,fogFactor),1.f);
    };

    Corona::Horizon::RasterizerPipeline pack_rasterizer(shadowmaps_pack_vert, shadowmaps_pack_frag, pack_desc);
    //pack_rasterizer.outColor = shadow_map_image;
    pack_rasterizer.bind_depth_target(shadow_depth_image);

    Corona::Horizon::RasterizerPipelineDesc scene_desc;
    scene_desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline scene_rasterizer(shadowmaps_scene_vert, shadowmaps_scene_frag, scene_desc);
    //scene_rasterizer.outColor = final_output_image;
    scene_rasterizer.bind_depth_target(depth_image);

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
    const glm::vec4 c_material_ka(1.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec4 c_material_kd(1.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec4 c_material_ks(1.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec4 c_light_ambient(1.0f, 1.0f, 1.0f, 0.0f); // 与原版一致（ambient power 0）
    const glm::vec4 c_light_diffuse(1.0f, 1.0f, 1.0f, 850.0f);
    const glm::vec4 c_light_specular(1.0f, 1.0f, 1.0f, 0.0f);
    const glm::vec3 c_light_attn(1.0f, 0.0f, 1.0f);

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
        const glm::vec3 c_light_pos(std::cos(time) * 20.0f, 26.0f, std::sin(time) * 20.0f);
        const glm::vec3 c_spot_dir = -c_light_pos;

        const glm::mat4 c_light_view = glm::lookAtLH(c_light_pos, c_light_pos + c_spot_dir, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 light_view_proj = light_proj * c_light_view;
        const glm::mat4 shadow_mtx = shadow_bias_mtx * light_view_proj;

        // view 空间光源参数
        const glm::vec4 c_light_pos_vs = view * glm::vec4(c_light_pos, 1.0f);
        const glm::vec3 c_spot_dir_vs = glm::vec3(view * glm::vec4(c_spot_dir, 0.0f));

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

            mvp = to_edsl_matrix(transpose((light_view_proj * item.model)));
            pack_rasterizer.record(item.mesh->ib, item.mesh->vb, params);
        }

        // Pass 2：场景光照 + 硬阴影
        scene_rasterizer.clear_records();
        // 共享矩阵（batch 内不变）：VS 内用 proj_view * pc.model 等现场计算 mvp/model_view/light_mtx
        proj_view       = to_edsl_matrix(transpose(view_proj));
        view_matrix     = to_edsl_matrix(transpose(view));
        light_proj_view = to_edsl_matrix(transpose(shadow_mtx)); // bias * light_proj * light_view
        light_pos_vs = ktm::fvec4(c_light_pos_vs.x,c_light_pos_vs.y,c_light_pos_vs.z,c_light_pos_vs.w);
        light_ambient = ktm::fvec4(c_light_ambient.x,c_light_ambient.y,c_light_ambient.z,c_light_ambient.w);
        light_diffuse = ktm::fvec4(c_light_diffuse.x,c_light_diffuse.y,c_light_diffuse.z,c_light_diffuse.w);
        light_specular = ktm::fvec4(c_light_specular.x,c_light_specular.y,c_light_specular.z,c_light_specular.w);
        auto glm_spot_dir_inner_vs = glm::vec4(c_spot_dir_vs, spot_inner_angle);
        spot_dir_inner_vs = ktm::fvec4(glm_spot_dir_inner_vs.x,glm_spot_dir_inner_vs.y,glm_spot_dir_inner_vs.z,glm_spot_dir_inner_vs.w);
        auto glm_attn_spot_outer = glm::vec4(c_light_attn, spot_outer_angle);
        attn_spot_outer = ktm::fvec4(glm_attn_spot_outer.x, glm_attn_spot_outer.y,glm_attn_spot_outer.z,glm_attn_spot_outer.w);
        material_ka = ktm::fvec4(c_material_ka.x,c_material_ka.y,c_material_ka.z,c_material_ka.w);
        material_kd = ktm::fvec4(c_material_kd.x,c_material_kd.y,c_material_kd.z,c_material_kd.w);
        material_ks = ktm::fvec4(c_material_ks.x,c_material_ks.y,c_material_ks.z,c_material_ks.w);
        color = ktm::fvec4(1.0f);
        params1 = ktm::fvec4(shadow_bias, shadow_normal_offset, 1.0f / shadow_map_size, 0.0f);
        for (const DrawItem& item : items)
        {
            Corona::Horizon::DrawIndexedParams params;
            params.index_type = Corona::Horizon::IndexType::UInt32;
            params.index_count = item.mesh->index_count;

            model = to_edsl_matrix(transpose(item.model)); // per-draw；VS 从中计算 mvp/model_view/light_mtx
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
