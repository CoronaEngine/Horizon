// 移植自参考示例 21-deferred（延迟着色：G-buffer MRT + 512 点光源
// 累加 + 合成），参照 example_glsl 的 GLSL 预编译路线。与原版对齐：
// 11x11 石墙立方体阵列、512 个动态点光源（radius=2、颜色按 light&7 位模式）、
// 相机 (0,0,-15) 朝 +Z、fovy 60°。
// 差异/等价替换：
// - 原版 采样 depth attachment 重建世界坐标 → 这里 G-buffer 第三张 R32F
//   颜色目标显式写 gl_FragCoord.z；
// - 原版 用 scissor 限制每个光源的着色范围 → 这里光照 quad 在 VS 按光源
//   包围盒 NDC rect 定位（等价的填充率优化）；
// - 原版 MRT 一次写三张 G-buffer → 框架动态渲染当前只绑定第一个颜色附件，
//   拆成三个几何 pass（albedo / 法线 / 深度值）分别输出，几何画三遍；
// - 固定默认设置（无 imgui）：animate=true、anim.speed=0.3、不显示 G-buffer
//   调试视图/scissor 线框、不走 TArray/UAV 变体。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/deferred_geom_vert.glsl)
#include GLSL(shaders/deferred_geom_mrt_frag.glsl)
#include GLSL(shaders/deferred_light_vert.glsl)
#include GLSL(shaders/deferred_light_frag.glsl)
#include GLSL(shaders/deferred_combine_vert.glsl)
#include GLSL(shaders/deferred_combine_frag.glsl)

#include <algorithm>
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
constexpr uint32_t dfr_width = 1280;
constexpr uint32_t dfr_height = 720;
constexpr int num_lights = 512;
constexpr float light_animation_speed = 0.3f;
constexpr float pi_half = 1.5707963f;

const std::filesystem::path dfr_asset_root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

// ============================================================================
// DDS 加载：BC2(DXT3)/BC5(ATI2) 压缩数据直接上传（全 mip 链）
// ============================================================================

uint32_t read_u32_at(const std::vector<std::byte>& bytes, size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::vector<std::byte> read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open DDS file: " + path.string());
    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);
    return bytes;
}

// 传统 DDS 头（128 字节）+ BC 压缩 payload 直接上传（含全 mip 链，与 example_bump 相同）
horizon::HardwareImage create_bc_texture(const std::filesystem::path& path, const std::string& name)
{
    const std::vector<std::byte> bytes = read_file_bytes(path);

    constexpr uint32_t dds_magic = 0x20534444; // "DDS "
    if (bytes.size() < 128 || read_u32_at(bytes, 0) != dds_magic)
        throw std::runtime_error("Not a DDS file: " + path.string());

    const uint32_t height = read_u32_at(bytes, 12);
    const uint32_t width = read_u32_at(bytes, 16);
    const uint32_t mip_count = std::max(1u, read_u32_at(bytes, 28));
    const uint32_t four_cc = read_u32_at(bytes, 84);

    constexpr uint32_t fourcc_dxt3 = 0x33545844; // "DXT3" -> BC2
    constexpr uint32_t fourcc_ati2 = 0x32495441; // "ATI2" -> BC5
    horizon::Format format;
    if (four_cc == fourcc_dxt3)
        format = horizon::Format::BC2_UNORM;
    else if (four_cc == fourcc_ati2)
        format = horizon::Format::BC5_UNORM;
    else
        throw std::runtime_error("Unsupported DDS fourcc: " + path.string());

    horizon::HardwareImageDesc desc = horizon::HardwareImageDesc::texture_2d(
        width, height, format,
        horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferDst, name);
    desc.mip_levels = mip_count;

    horizon::HardwareImage image(desc);

    const std::span<const std::byte> payload(bytes.data() + 128, bytes.size() - 128);
    horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = horizon::BufferUsage_TransferSrc;
    staging_desc.cpu_access = horizon::CpuAccessMode::Write;
    horizon::HardwareBuffer staging(staging_desc, payload);

    horizon::HardwareExecutor executor;
    horizon::HardwareStream stream = executor.stream();
    uint64_t offset = 0;
    for (uint32_t mip = 0; mip < mip_count; ++mip)
    {
        const uint32_t mip_w = std::max(1u, width >> mip);
        const uint32_t mip_h = std::max(1u, height >> mip);
        const uint64_t blocks_x = (mip_w + 3) / 4;
        const uint64_t blocks_y = (mip_h + 3) / 4;
        stream << image.copy_from(staging, offset, 0, mip);
        offset += blocks_x * blocks_y * 16; // BC2/BC5 均为 16 字节/块
    }
    (void)(stream << horizon::commit());

    return image;
}

// ============================================================================
// 立方体（24 顶点/每面 UV）+ Lengyel 切线（与 example_bump 相同）
// ============================================================================

struct GeomVertex
{
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 4> tangent;
    std::array<float, 2> uv;
};

std::vector<GeomVertex> build_cube_vertices()
{
    const float N = 1.0f;
    struct Face
    {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };
    const Face faces[6] = {
        { { 0, 0, N }, { { -1, 1, 1 }, { 1, 1, 1 }, { -1, -1, 1 }, { 1, -1, 1 } } },
        { { 0, 0, -N }, { { -1, 1, -1 }, { 1, 1, -1 }, { -1, -1, -1 }, { 1, -1, -1 } } },
        { { 0, N, 0 }, { { -1, 1, 1 }, { 1, 1, 1 }, { -1, 1, -1 }, { 1, 1, -1 } } },
        { { 0, -N, 0 }, { { -1, -1, 1 }, { 1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 } } },
        { { N, 0, 0 }, { { 1, -1, 1 }, { 1, 1, 1 }, { 1, -1, -1 }, { 1, 1, -1 } } },
        { { -N, 0, 0 }, { { -1, -1, 1 }, { -1, 1, 1 }, { -1, -1, -1 }, { -1, 1, -1 } } },
    };
    const float uvs[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };

    std::vector<GeomVertex> vertices;
    vertices.reserve(24);
    for (const Face& face : faces)
    {
        for (int c = 0; c < 4; ++c)
        {
            GeomVertex v {};
            v.position = { face.corners[c].x, face.corners[c].y, face.corners[c].z };
            v.normal = { face.normal.x, face.normal.y, face.normal.z };
            v.uv = { uvs[c][0], uvs[c][1] };
            vertices.push_back(v);
        }
    }
    return vertices;
}

const std::vector<uint16_t> cube_indices = {
    0, 2, 1, 1, 2, 3,
    4, 5, 6, 5, 7, 6,
    8, 10, 9, 9, 10, 11,
    12, 13, 14, 13, 15, 14,
    16, 18, 17, 17, 18, 19,
    20, 21, 22, 21, 23, 22,
};

void calc_tangents(std::vector<GeomVertex>& vertices, const std::vector<uint16_t>& indices)
{
    std::vector<glm::vec3> tan_accum(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitan_accum(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];

        const glm::vec3 p0(vertices[i0].position[0], vertices[i0].position[1], vertices[i0].position[2]);
        const glm::vec3 p1(vertices[i1].position[0], vertices[i1].position[1], vertices[i1].position[2]);
        const glm::vec3 p2(vertices[i2].position[0], vertices[i2].position[1], vertices[i2].position[2]);
        const glm::vec2 w0(vertices[i0].uv[0], vertices[i0].uv[1]);
        const glm::vec2 w1(vertices[i1].uv[0], vertices[i1].uv[1]);
        const glm::vec2 w2(vertices[i2].uv[0], vertices[i2].uv[1]);

        const glm::vec3 e1 = p1 - p0;
        const glm::vec3 e2 = p2 - p0;
        const glm::vec2 duv1 = w1 - w0;
        const glm::vec2 duv2 = w2 - w0;

        const float det = duv1.x * duv2.y - duv2.x * duv1.y;
        const float inv_det = det != 0.0f ? 1.0f / det : 0.0f;

        const glm::vec3 tangent = (e1 * duv2.y - e2 * duv1.y) * inv_det;
        const glm::vec3 bitangent = (e2 * duv1.x - e1 * duv2.x) * inv_det;

        for (uint32_t idx : { i0, i1, i2 })
        {
            tan_accum[idx] += tangent;
            bitan_accum[idx] += bitangent;
        }
    }

    for (size_t v = 0; v < vertices.size(); ++v)
    {
        const glm::vec3 normal(vertices[v].normal[0], vertices[v].normal[1], vertices[v].normal[2]);
        const glm::vec3 t = tan_accum[v];

        const glm::vec3 tangent = glm::normalize(t - normal * glm::dot(normal, t));
        const float w = glm::dot(glm::cross(normal, tangent), bitan_accum[v]) < 0.0f ? -1.0f : 1.0f;

        vertices[v].tangent = { tangent.x, tangent.y, tangent.z, w };
    }
}

// 屏幕 quad：角点 (0,0)..(1,1)，light pass 按 rect 定位、combine pass 全屏
struct CornerVertex
{
    std::array<float, 3> corner;
};

const std::vector<CornerVertex> corner_vertices = {
    { { 0.0f, 0.0f, 0.0f } },
    { { 1.0f, 0.0f, 0.0f } },
    { { 1.0f, 1.0f, 0.0f } },
    { { 0.0f, 1.0f, 0.0f } },
};
const std::vector<uint16_t> corner_indices = { 0, 2, 1, 0, 3, 2 };

// bx::mulH：列向量变换 + 透视除法
glm::vec3 mul_h(const glm::vec3& v, const glm::mat4& m)
{
    const glm::vec4 r = m * glm::vec4(v, 1.0f);
    return glm::vec3(r) / r.w;
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

void run_example_deferred()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(dfr_width, dfr_height, "Horizon Deferred [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    std::vector<GeomVertex> cube_vertices = build_cube_vertices();
    calc_tangents(cube_vertices, cube_indices);

    horizon::HardwareBuffer cube_vb = horizon::HardwareBuffer::vertex(cube_vertices, "example_deferred.cube.vb");
    horizon::HardwareBuffer cube_ib = horizon::HardwareBuffer::index(cube_indices, "example_deferred.cube.ib");
    horizon::HardwareBuffer quad_vb = horizon::HardwareBuffer::vertex(corner_vertices, "example_deferred.quad.vb");
    horizon::HardwareBuffer quad_ib = horizon::HardwareBuffer::index(corner_indices, "example_deferred.quad.ib");

    horizon::HardwareImage color_image = create_bc_texture(dfr_asset_root / "textures" / "fieldstone-rgba.dds", "example_deferred.texColor");
    horizon::HardwareImage normal_image = create_bc_texture(dfr_asset_root / "textures" / "fieldstone-n.dds", "example_deferred.texNormal");

    // G-buffer：albedo + 世界法线 + 器件深度（R32F 颜色目标）
    const auto gbuffer_usage = horizon::ImageUsage_ColorAttachment | horizon::ImageUsage_Sampled;
    horizon::HardwareImage gbuffer_albedo(horizon::HardwareImageDesc::texture_2d(
        dfr_width, dfr_height, horizon::Format::RGBA8_UNORM, gbuffer_usage, "example_deferred.gbuffer.albedo"));
    gbuffer_albedo.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    horizon::HardwareImage gbuffer_normal(horizon::HardwareImageDesc::texture_2d(
        dfr_width, dfr_height, horizon::Format::RGBA8_UNORM, gbuffer_usage, "example_deferred.gbuffer.normal"));
    gbuffer_normal.set_clear_color(0.5f, 0.5f, 0.5f, 1.0f);
    horizon::HardwareImage gbuffer_depth_val(horizon::HardwareImageDesc::texture_2d(
        dfr_width, dfr_height, horizon::Format::R32_FLOAT, gbuffer_usage, "example_deferred.gbuffer.depthval"));
    gbuffer_depth_val.set_clear_color(1.0f, 0.0f, 0.0f, 0.0f); // 远平面

    horizon::HardwareImage gbuffer_depth(horizon::HardwareImageDesc::depth_attachment(
        dfr_width, dfr_height, horizon::Format::D32, "example_deferred.gbuffer.depth"));
    gbuffer_depth.set_clear_depth(1.0f, 0);

    // 光照累加缓冲
    horizon::HardwareImage light_buffer(horizon::HardwareImageDesc::texture_2d(
        dfr_width, dfr_height, horizon::Format::RGBA8_UNORM, gbuffer_usage, "example_deferred.lightbuffer"));
    light_buffer.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    // 最终输出
    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        dfr_width, dfr_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_deferred.output"));
    final_output_image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);

    // Pass 1：几何 → G-buffer（MRT 单 pass：albedo/normal/depthval 三附件同时输出）
    horizon::RasterizerPipelineDesc geom_desc;
    geom_desc.blend_enabled = false;

    horizon::RasterizerPipeline geom_rasterizer(deferred_geom_vert_glsl, deferred_geom_mrt_frag_glsl, geom_desc);
    geom_rasterizer.outAlbedo = gbuffer_albedo;
    geom_rasterizer.outNormal = gbuffer_normal;
    geom_rasterizer.outDepthVal = gbuffer_depth_val;
    geom_rasterizer.bind_depth_target(gbuffer_depth);
    // 纹理/ G-buffer 存入 bindless combined-texture 表（set 0），索引经 push constant 传入。
    geom_rasterizer.model_pc.texColorIndex = color_image.store_descriptor();
    geom_rasterizer.model_pc.texNormalIndex = normal_image.store_descriptor();

    // Pass 2：光照累加（加法混合、无深度）
    horizon::RasterizerPipelineDesc light_desc;
    light_desc.depth_test_enabled = false;
    light_desc.depth_write_enabled = false;
    light_desc.blend_enabled = true;
    light_desc.src_color_blend_factor = horizon::BlendFactor::One;
    light_desc.dst_color_blend_factor = horizon::BlendFactor::One;
    light_desc.color_blend_op = horizon::BlendOp::Add;
    light_desc.src_alpha_blend_factor = horizon::BlendFactor::One;
    light_desc.dst_alpha_blend_factor = horizon::BlendFactor::One;
    light_desc.alpha_blend_op = horizon::BlendOp::Add;

    horizon::RasterizerPipeline light_rasterizer(deferred_light_vert_glsl, deferred_light_frag_glsl, light_desc);
    light_rasterizer.outColor = light_buffer;
    light_rasterizer.vpc.gNormalIndex = gbuffer_normal.store_descriptor();
    light_rasterizer.vpc.gDepthIndex = gbuffer_depth_val.store_descriptor();

    // Pass 3：合成
    horizon::RasterizerPipelineDesc combine_desc;
    combine_desc.depth_test_enabled = false;
    combine_desc.depth_write_enabled = false;
    combine_desc.blend_enabled = false;

    horizon::RasterizerPipeline combine_rasterizer(deferred_combine_vert_glsl, deferred_combine_frag_glsl, combine_desc);
    combine_rasterizer.outColor = final_output_image;
    combine_rasterizer.fpc.gAlbedoIndex = gbuffer_albedo.store_descriptor();
    combine_rasterizer.fpc.gLightIndex = light_buffer.store_descriptor();

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    cube_params.index_count = static_cast<uint32_t>(cube_indices.size());

    quad_params.index_count = static_cast<uint32_t>(corner_indices.size());

    constexpr float aspect = static_cast<float>(dfr_width) / static_cast<float>(dfr_height);
    // 原版 相机 (0,0,-15) 朝 +Z（垂直角 0）
    const glm::mat4 view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -15.0f), glm::vec3(0.0f, 0.0f, -14.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    const glm::mat4 view_proj = proj * view;
    const glm::mat4 inv_view_proj = glm::inverse(view_proj);

    HorizonImGuiLayer ui(window, dfr_width, dfr_height);

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
            std::snprintf(title, sizeof(title), "Horizon Deferred [Vulkan] - %d lights - %.1f FPS (%.2f ms)",
                          num_lights, fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        constexpr uint32_t dim = 11;
        constexpr float offset = (float(dim - 1) * 3.0f) * 0.5f; // 15

        // Pass 1a/1b/1c：11x11 立方体分别写入三张 G-buffer - Convert to indirect
        auto record_geometry = [&](auto& pipeline) {
            pipeline.clear_records();
            pipeline.vsp.view_proj = view_proj;

            std::vector<horizon::DrawIndexedIndirectCommand> indirect_cmds;
            indirect_cmds.reserve(dim * dim);
            for (uint32_t yy = 0; yy < dim; ++yy)
            {
                for (uint32_t xx = 0; xx < dim; ++xx)
                {
                    // 原版 mtxRotateXY（行向量 Rx·Ry）→ glm 列向量 Ry·Rx，等效角度取反
                    glm::mat4 model = glm::eulerAngleYX(-(time * 0.03f + yy * 0.37f), -(time * 1.023f + xx * 0.21f));
                    model[3] = glm::vec4(-offset + xx * 3.0f, -offset + yy * 3.0f, 0.0f, 1.0f);

                    pipeline.model_pc.model = model;

                    horizon::DrawIndexedIndirectCommand cmd;
                    cmd.index_count = cube_params.index_count;
                    cmd.first_index = cube_params.first_index;
                    cmd.vertex_offset = cube_params.vertex_offset;
                    cmd.instance_count = 1;
                    cmd.first_instance = static_cast<uint32_t>(indirect_cmds.size());
                    indirect_cmds.push_back(cmd);
                }
            }

            if (!indirect_cmds.empty())
            {
                horizon::HardwareBuffer indirect_buffer = horizon::HardwareBuffer::from_bytes(
                    std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(indirect_cmds.data()),
                        indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                    static_cast<uint32_t>(indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                    horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
                    "example_deferred.geom_indirect");

                horizon::DrawIndexedIndirectParams indirect_params;
                indirect_params.draw_count = static_cast<uint32_t>(indirect_cmds.size());
                indirect_params.indirect_offset = 0;
                indirect_params.stride = sizeof(horizon::DrawIndexedIndirectCommand);
                pipeline.record_indirect(cube_ib, cube_vb, indirect_buffer, indirect_params);
            }
        };
        record_geometry(geom_rasterizer);

        // Pass 2：512 光源逐个累加（quad 按光源包围盒 NDC rect 定位）- Convert to indirect
        light_rasterizer.clear_records();
        light_rasterizer.vsp.inv_mvp = inv_view_proj;
        light_rasterizer.vsp.view = view;

        std::vector<horizon::DrawIndexedIndirectCommand> light_indirect_cmds;
        for (int light = 0; light < num_lights; ++light)
        {
            const float light_time = time * light_animation_speed *
                                     (std::sin(light / float(num_lights) * pi_half) * 0.5f + 0.5f);
            const glm::vec3 center(
                std::sin((light_time + light * 0.47f) + pi_half * 1.37f) * offset,
                std::cos((light_time + light * 0.69f) + pi_half * 1.49f) * offset,
                std::sin((light_time + light * 0.37f) + pi_half * 1.57f) * 2.0f);
            const float radius = 2.0f;

            // AABB 8 角投影到 NDC 求屏幕范围
            glm::vec3 mn(0.0f);
            glm::vec3 mx(0.0f);
            bool first = true;
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 p = center + glm::vec3(
                    (corner & 1) ? radius : -radius,
                    (corner & 2) ? radius : -radius,
                    (corner & 4) ? radius : -radius);
                const glm::vec3 ndc = mul_h(p, view_proj);
                if (first)
                {
                    mn = mx = ndc;
                    first = false;
                }
                else
                {
                    mn = glm::min(mn, ndc);
                    mx = glm::max(mx, ndc);
                }
            }

            if (mx.z < 0.0f)
                continue; // 完全在相机后

            const glm::vec2 rect_min = glm::clamp(glm::vec2(mn), glm::vec2(-1.0f), glm::vec2(1.0f));
            const glm::vec2 rect_max = glm::clamp(glm::vec2(mx), glm::vec2(-1.0f), glm::vec2(1.0f));
            if (rect_min.x >= rect_max.x || rect_min.y >= rect_max.y)
                continue;

            const uint8_t val = light & 7;
            light_rasterizer.vpc.light_pos_radius = glm::vec4(center, radius);
            light_rasterizer.vpc.light_rgb_inner_r = glm::vec4(
                (val & 0x1) ? 1.0f : 0.25f,
                (val & 0x2) ? 1.0f : 0.25f,
                (val & 0x4) ? 1.0f : 0.25f,
                0.8f);
            light_rasterizer.vpc.rect = glm::vec4(rect_min, rect_max);

            horizon::DrawIndexedIndirectCommand cmd;
            cmd.index_count = quad_params.index_count;
            cmd.first_index = quad_params.first_index;
            cmd.vertex_offset = quad_params.vertex_offset;
            cmd.instance_count = 1;
            cmd.first_instance = static_cast<uint32_t>(light_indirect_cmds.size());
            light_indirect_cmds.push_back(cmd);
        }

        if (!light_indirect_cmds.empty())
        {
            horizon::HardwareBuffer light_indirect_buffer = horizon::HardwareBuffer::from_bytes(
                std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(light_indirect_cmds.data()),
                    light_indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                static_cast<uint32_t>(light_indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
                horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
                "example_deferred.light_indirect");

            horizon::DrawIndexedIndirectParams light_params;
            light_params.draw_count = static_cast<uint32_t>(light_indirect_cmds.size());
            light_params.indirect_offset = 0;
            light_params.stride = sizeof(horizon::DrawIndexedIndirectCommand);
            light_rasterizer.record_indirect(quad_ib, quad_vb, light_indirect_buffer, light_params);
        }

        // Pass 3：albedo × light 合成 - Convert to indirect
        combine_rasterizer.clear_records();
        horizon::DrawIndexedIndirectCommand combine_cmd;
        combine_cmd.index_count = quad_params.index_count;
        combine_cmd.first_index = quad_params.first_index;
        combine_cmd.vertex_offset = quad_params.vertex_offset;
        combine_cmd.instance_count = 1;
        combine_cmd.first_instance = 0;

        horizon::HardwareBuffer combine_indirect_buffer = horizon::HardwareBuffer::from_bytes(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(&combine_cmd),
                sizeof(horizon::DrawIndexedIndirectCommand)),
            sizeof(horizon::DrawIndexedIndirectCommand),
            horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
            "example_deferred.combine_indirect");

        horizon::DrawIndexedIndirectParams combine_params;
        combine_params.draw_count = 1;
        combine_params.indirect_offset = 0;
        combine_params.stride = 0;
        combine_rasterizer.record_indirect(quad_ib, quad_vb, combine_indirect_buffer, combine_params);

        horizon::SubmitReceipt render_receipt =
            render_executor << geom_rasterizer.extent(dfr_width, dfr_height)
                            << light_rasterizer.extent(dfr_width, dfr_height)
                            << combine_rasterizer.extent(dfr_width, dfr_height)
                            << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
