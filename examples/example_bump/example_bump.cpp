// 移植自参考示例 06-bump（法线贴图 + 4 动态点光源，3x3 立方体阵列），
// 参照 example_glsl 的 GLSL 预编译路线，走非实例化路径（逐 draw 更新 model）。
// 与原版对齐：相机 (0,0,-7) 看原点 fovy 60°、光源轨迹/颜色表相同、
// fieldstone-rgba.dds(DXT3/BC2) + fieldstone-n.dds(ATI2/BC5) 按压缩格式
// 直接上传（含全 mip 链），切线用与 原版 calcTangents 相同的 Lengyel 算法计算。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/bump_vert.glsl)
#include GLSL(shaders/bump_frag.glsl)

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
constexpr uint32_t bump_width = 1280;
constexpr uint32_t bump_height = 720;

const std::filesystem::path bump_asset_root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

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

// 传统 DDS 头（128 字节）+ BC 压缩 payload 直接上传（含全 mip 链）
Corona::Horizon::HardwareImage create_bc_texture(const std::filesystem::path& path, const std::string& name)
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
    Corona::Horizon::Format format;
    if (four_cc == fourcc_dxt3)
        format = Corona::Horizon::Format::BC2_UNORM;
    else if (four_cc == fourcc_ati2)
        format = Corona::Horizon::Format::BC5_UNORM;
    else
        throw std::runtime_error("Unsupported DDS fourcc: " + path.string());

    Corona::Horizon::HardwareImageDesc desc = Corona::Horizon::HardwareImageDesc::texture_2d(
        width, height, format,
        Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferDst, name);
    desc.mip_levels = mip_count;

    Corona::Horizon::HardwareImage image(desc);

    const std::span<const std::byte> payload(bytes.data() + 128, bytes.size() - 128);
    Corona::Horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = Corona::Horizon::BufferUsageFlags::TransferSrc;
    staging_desc.cpu_access = Corona::Horizon::CpuAccessMode::Write;
    Corona::Horizon::HardwareBuffer staging(staging_desc, payload);

    Corona::Horizon::HardwareExecutor executor;
    Corona::Horizon::HardwareStream stream = executor.stream();
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
    (void)(stream << Corona::Horizon::commit());

    return image;
}

// ============================================================================
// 立方体顶点（24 顶点 / 每面独立 UV）+ Lengyel 切线计算（对齐 原版 calcTangents）
// ============================================================================

struct BumpVertex
{
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 4> tangent;
    std::array<float, 2> uv;
};

std::vector<BumpVertex> build_cube_vertices()
{
    // 原版 s_cubeVertices：6 面 × 4 顶点，法线逐面，UV 0/1
    const float N = 1.0f;
    struct Face
    {
        glm::vec3 normal;
        glm::vec3 corners[4]; // uv(0,0) (1,0) (0,1) (1,1)
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

    std::vector<BumpVertex> vertices;
    vertices.reserve(24);
    for (const Face& face : faces)
    {
        for (int c = 0; c < 4; ++c)
        {
            BumpVertex v {};
            v.position = { face.corners[c].x, face.corners[c].y, face.corners[c].z };
            v.normal = { face.normal.x, face.normal.y, face.normal.z };
            v.uv = { uvs[c][0], uvs[c][1] };
            vertices.push_back(v);
        }
    }
    return vertices;
}

const std::vector<uint32_t> cube_indices = {
    0, 2, 1, 1, 2, 3,
    4, 5, 6, 5, 7, 6,
    8, 10, 9, 9, 10, 11,
    12, 13, 14, 13, 15, 14,
    16, 18, 17, 17, 18, 19,
    20, 21, 22, 21, 23, 22,
};

void calc_tangents(std::vector<BumpVertex>& vertices, const std::vector<uint32_t>& indices)
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

        // Gram-Schmidt 正交化 + 手性符号（Lengyel）
        const glm::vec3 tangent = glm::normalize(t - normal * glm::dot(normal, t));
        const float w = glm::dot(glm::cross(normal, tangent), bitan_accum[v]) < 0.0f ? -1.0f : 1.0f;

        vertices[v].tangent = { tangent.x, tangent.y, tangent.z, w };
    }
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace

void run_example_bump()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(bump_width, bump_height, "Horizon Bump [Vulkan]", nullptr, nullptr);
    glfwSetKeyCallback(window, key_callback);

    std::vector<BumpVertex> cube_vertices = build_cube_vertices();
    calc_tangents(cube_vertices, cube_indices);

    Corona::Horizon::HardwareBuffer cube_vb = Corona::Horizon::HardwareBuffer::vertex(cube_vertices, "example_bump.vb");
    Corona::Horizon::HardwareBuffer cube_ib = Corona::Horizon::HardwareBuffer::index(cube_indices, "example_bump.ib");

    Corona::Horizon::HardwareImage color_image = create_bc_texture(bump_asset_root / "textures" / "fieldstone-rgba.dds", "example_bump.texColor");
    Corona::Horizon::HardwareImage normal_image = create_bc_texture(bump_asset_root / "textures" / "fieldstone-n.dds", "example_bump.texNormal");

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        bump_width, bump_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_bump.output"));
    final_output_image.set_clear_color(0x30 / 255.0f, 0x30 / 255.0f, 0x30 / 255.0f, 1.0f); // 原版 0x303030ff

    Corona::Horizon::HardwareImage depth_image(Corona::Horizon::HardwareImageDesc::depth_attachment(
        bump_width, bump_height, Corona::Horizon::Format::D32, "example_bump.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    Corona::Horizon::RasterizerPipelineDesc desc;
    desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };
    desc.depth_stencil.depth_compare_op = Corona::Horizon::CompareOp::Less; // 原版 depth test 为严格 LESS

    Corona::Horizon::RasterizerPipeline rasterizer(bump_vert_glsl, bump_frag_glsl, desc);
    rasterizer.outColor = final_output_image;
    rasterizer.bind_depth_target(depth_image);
    rasterizer.texColor = color_image;
    rasterizer.texNormal = normal_image;

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    Corona::Horizon::DrawIndexedParams draw_params;
    draw_params.index_type = Corona::Horizon::IndexType::UInt32;
    draw_params.index_count = static_cast<uint32_t>(cube_indices.size());

    constexpr float aspect = static_cast<float>(bump_width) / static_cast<float>(bump_height);
    // 原版左手系：mtxLookAt (0,0,-7)->(0,0,0)、mtxProj fovy 60°
    const glm::vec3 eye(0.0f, 0.0f, -7.0f);
    const glm::mat4 view = glm::lookAtLH(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    const glm::mat4 view_proj = proj * view;

    constexpr float pi_half = 1.5707963f;
    const glm::vec4 light_rgb_inner_r[4] = {
        { 1.0f, 0.7f, 0.2f, 0.8f },
        { 0.7f, 0.2f, 1.0f, 0.8f },
        { 0.2f, 1.0f, 0.7f, 0.8f },
        { 1.0f, 0.4f, 0.2f, 0.8f },
    };

    HorizonImGuiLayer ui(window, bump_width, bump_height);

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
            char title[128];
            std::snprintf(title, sizeof(title), "Horizon Bump [Vulkan] - %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        glm::vec4 light_pos_radius[4];
        for (int i = 0; i < 4; ++i)
        {
            light_pos_radius[i] = glm::vec4(
                std::sin(time * (0.1f + i * 0.17f) + i * pi_half * 1.37f) * 3.0f,
                std::cos(time * (0.2f + i * 0.29f) + i * pi_half * 1.49f) * 3.0f,
                -2.5f, 3.0f);
        }

        rasterizer.clear_records();
        rasterizer.vsp.view_proj = view_proj;
        rasterizer.vsp.eye_pos = glm::vec4(eye, 1.0f);
        rasterizer.vsp.light0_pos_radius = light_pos_radius[0];
        rasterizer.vsp.light1_pos_radius = light_pos_radius[1];
        rasterizer.vsp.light2_pos_radius = light_pos_radius[2];
        rasterizer.vsp.light3_pos_radius = light_pos_radius[3];
        rasterizer.vsp.light0_rgb_inner_r = light_rgb_inner_r[0];
        rasterizer.vsp.light1_rgb_inner_r = light_rgb_inner_r[1];
        rasterizer.vsp.light2_rgb_inner_r = light_rgb_inner_r[2];
        rasterizer.vsp.light3_rgb_inner_r = light_rgb_inner_r[3];

        for (uint32_t yy = 0; yy < 3; ++yy)
        {
            for (uint32_t xx = 0; xx < 3; ++xx)
            {
                // 原版 mtxRotateXY（行向量 Rx·Ry）→ glm 列向量 Ry·Rx，等效角度取反
                glm::mat4 model = glm::eulerAngleYX(-(time * 0.03f + yy * 0.37f), -(time * 0.023f + xx * 0.21f));
                model[3] = glm::vec4(-3.0f + xx * 3.0f, -3.0f + yy * 3.0f, 0.0f, 1.0f);

                rasterizer.model_pc.model = model;
                rasterizer.record(cube_ib, cube_vb, draw_params);
            }
        }

        Corona::Horizon::SubmitReceipt render_receipt =
            render_executor << rasterizer(bump_width, bump_height) << Corona::Horizon::submit;

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
