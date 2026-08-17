// 移植自 bgfx examples/18-ibl（Image-based lighting），参照 example_glsl 的
// GLSL 预编译路线实现：shader 见 examples/shaders/ibl_vert.glsl / ibl_frag.glsl。
//
// 与 bgfx 原版的结构差异：
//  - Horizon 的每个 RasterizerPipeline pass 都会 clear 颜色附件，因此天空盒与
//    网格共用同一个 pipeline（shader 内用 isSkybox 分支），单 pass 完成；
//  - record() 会为每个 draw 快照当前 uniform 值，orbs 模式下 5x5 球体逐球
//    改 glossiness / reflectivity 后各 record 一次即可；
//  - imgui 面板换成键盘交互（启动时在控制台打印按键说明）。

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/ibl_vert.glsl)
#include GLSL(shaders/ibl_frag.glsl)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t ibl_width = 1280;
constexpr uint32_t ibl_height = 720;

const std::filesystem::path asset_root = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

// ============================================================================
// DDS cubemap 加载（bolonga/kyoto 均为 DX10 头 + RGBA16F + 全 mip 链）
// ============================================================================

struct CubeMapData
{
    uint32_t size = 0;
    uint32_t mip_count = 0;
    std::vector<std::byte> payload; // face-major：face0 全部 mip，face1 全部 mip……
};

uint32_t read_u32(const std::vector<std::byte>& bytes, size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

CubeMapData load_dds_cube_rgba16f(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open DDS file: " + path.string());

    const std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);

    constexpr uint32_t dds_magic = 0x20534444;            // "DDS "
    constexpr uint32_t fourcc_dx10 = 0x30315844;          // "DX10"
    constexpr uint32_t dxgi_format_rgba16_float = 10;     // DXGI_FORMAT_R16G16B16A16_FLOAT
    constexpr uint32_t caps2_cubemap = 0x200;
    constexpr size_t dx10_payload_offset = 148;           // 4 magic + 124 header + 20 DX10 header

    if (bytes.size() < dx10_payload_offset || read_u32(bytes, 0) != dds_magic)
        throw std::runtime_error("Not a DDS file: " + path.string());

    const uint32_t height = read_u32(bytes, 12);
    const uint32_t width = read_u32(bytes, 16);
    const uint32_t mip_count = std::max(1u, read_u32(bytes, 28));
    const uint32_t fourcc = read_u32(bytes, 84);
    const uint32_t caps2 = read_u32(bytes, 112);

    if (fourcc != fourcc_dx10 || read_u32(bytes, 128) != dxgi_format_rgba16_float)
        throw std::runtime_error("Expected DX10 RGBA16F DDS: " + path.string());
    if ((caps2 & caps2_cubemap) == 0 || width != height)
        throw std::runtime_error("Expected cubemap DDS: " + path.string());

    CubeMapData data;
    data.size = width;
    data.mip_count = mip_count;
    data.payload.assign(bytes.begin() + dx10_payload_offset, bytes.end());
    return data;
}

horizon::HardwareImage create_cubemap_image(const CubeMapData& dds, const std::string& name)
{
    constexpr uint32_t bytes_per_pixel = 8; // RGBA16F

    horizon::HardwareImageDesc desc = horizon::HardwareImageDesc::cube(
        dds.size, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferDst, name);
    desc.mip_levels = dds.mip_count;

    horizon::HardwareImage image(desc);

    horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = dds.payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = horizon::BufferUsage_TransferSrc;
    staging_desc.cpu_access = horizon::CpuAccessMode::Write;
    horizon::HardwareBuffer staging(staging_desc, std::span<const std::byte>(dds.payload));

    horizon::HardwareExecutor executor;
    horizon::HardwareStream stream = executor.stream();
    uint64_t offset = 0;
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t mip = 0; mip < dds.mip_count; ++mip)
        {
            const uint32_t dim = std::max(1u, dds.size >> mip);
            stream << image.copy_from(staging, offset, face, mip);
            offset += static_cast<uint64_t>(dim) * dim * bytes_per_pixel;
        }
    }
    (void)(stream << horizon::commit());

    return image;
}

struct LightProbe
{
    horizon::HardwareImage lod;
    horizon::HardwareImage irr;
};

LightProbe load_light_probe(const std::string& name)
{
    LightProbe probe;
    probe.lod = create_cubemap_image(load_dds_cube_rgba16f(asset_root / "env" / (name + "_lod.dds")), "example_ibl." + name + ".lod");
    probe.irr = create_cubemap_image(load_dds_cube_rgba16f(asset_root / "env" / (name + "_irr.dds")), "example_ibl." + name + ".irr");
    return probe;
}

// ============================================================================
// bgfx 二进制网格加载（bunny.bin / orb.bin：未压缩 VB/IB chunk）
// ============================================================================

struct IblVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

struct IblMesh
{
    std::vector<IblVertex> vertices;
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

IblMesh load_bgfx_mesh(const std::filesystem::path& path)
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

    constexpr uint16_t attrib_id_position = 0x0001; // bgfx s_attribToId
    constexpr uint16_t attrib_id_normal = 0x0002;
    constexpr uint16_t attrib_type_id_uint8 = 0x0001; // bgfx s_attribTypeToId
    constexpr uint16_t attrib_type_id_float = 0x0004;

    IblMesh mesh;
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

                IblVertex vertex;
                std::memcpy(vertex.position.data(), vertex_data + position_offset, sizeof(float) * 3);

                if (normal_type == attrib_type_id_uint8)
                {
                    // Uint8 归一化到 [0,1]，bgfx shader 里再 *2-1；此处直接在 CPU 解码。
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
// 交互状态：轨道相机 + 设置（对应 bgfx 版的 imgui 面板）
// ============================================================================

struct Settings
{
    float glossiness = 0.7f;
    float reflectivity = 0.85f;
    float exposure = 0.0f;
    float bg_type = 3.0f; // 0..6: radiance mip, 7: irradiance
    float env_rot_curr = 0.0f;
    float env_rot_dest = 0.0f;
    bool do_diffuse = false;
    bool do_specular = false;
    bool do_diffuse_ibl = true;
    bool do_specular_ibl = true;
    int metal_or_spec = 0;   // 0: metalness 工作流, 1: specular 工作流
    int mesh_selection = 1;  // 0: bunny, 1: orbs
    int current_probe = 0;   // 0: bolonga, 1: kyoto
    glm::vec3 rgb_diff { 1.0f, 1.0f, 1.0f };
    glm::vec3 rgb_spec { 1.0f, 1.0f, 1.0f };
    glm::vec3 light_dir { -0.8f, 0.2f, -0.5f };
    glm::vec3 light_col { 1.0f, 1.0f, 1.0f };
};

struct OrbitCamera
{
    // 初始状态等价于 bgfx 版：pos (0,0,-3) 看向原点。
    float yaw_curr = glm::pi<float>();
    float yaw_dest = glm::pi<float>();
    float pitch_curr = glm::half_pi<float>();
    float pitch_dest = glm::half_pi<float>();
    float dist_curr = 3.0f;
    float dist_dest = 3.0f;
    glm::vec3 target { 0.0f };

    void orbit(float dx, float dy)
    {
        yaw_dest += dx * glm::two_pi<float>();
        pitch_dest = std::clamp(pitch_dest - dy * glm::pi<float>(),
                                0.02f * glm::pi<float>(),
                                0.98f * glm::pi<float>());
    }

    void dolly(float dz)
    {
        dist_dest = std::clamp(dist_dest + dist_dest * dz, 1.0f, 100.0f);
    }

    void update(float dt)
    {
        const float amount = std::min(dt / 0.12f, 1.0f);
        yaw_curr = glm::mix(yaw_curr, yaw_dest, amount);
        pitch_curr = glm::mix(pitch_curr, pitch_dest, amount);
        dist_curr = glm::mix(dist_curr, dist_dest, amount);
    }

    [[nodiscard]] glm::vec3 position() const
    {
        const glm::vec3 dir(std::sin(pitch_curr) * std::sin(yaw_curr),
                            std::cos(pitch_curr),
                            std::sin(pitch_curr) * std::cos(yaw_curr));
        return target + dir * dist_curr;
    }

    // bgfx Camera::envViewMtx：以相机朝向为基构造矩阵（列为 right/up/forward）。
    [[nodiscard]] glm::mat4 env_view_mtx() const
    {
        const glm::vec3 forward = glm::normalize(target - position());
        const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
        const glm::vec3 up = glm::normalize(glm::cross(forward, right));
        return glm::mat4(glm::vec4(right, 0.0f), glm::vec4(up, 0.0f), glm::vec4(forward, 0.0f),
                         glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
};

struct InputContext
{
    Settings settings;
    OrbitCamera camera;
    double prev_x = 0.0;
    double prev_y = 0.0;
    bool has_prev = false;
};

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (ImGui::GetIO().WantCaptureKeyboard)
        return;
    if (action != GLFW_PRESS)
        return;

    auto* context = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
    Settings& s = context->settings;

    switch (key)
    {
    case GLFW_KEY_1: s.current_probe = 0; break;
    case GLFW_KEY_2: s.current_probe = 1; break;
    case GLFW_KEY_M: s.mesh_selection ^= 1; break;
    case GLFW_KEY_W: s.metal_or_spec ^= 1; break;
    case GLFW_KEY_I: s.do_diffuse_ibl = !s.do_diffuse_ibl; break;
    case GLFW_KEY_O: s.do_specular_ibl = !s.do_specular_ibl; break;
    case GLFW_KEY_D: s.do_diffuse = !s.do_diffuse; break;
    case GLFW_KEY_S: s.do_specular = !s.do_specular; break;
    case GLFW_KEY_B: s.bg_type = s.bg_type >= 7.0f ? 0.0f : s.bg_type + 1.0f; break;
    case GLFW_KEY_MINUS: s.exposure = std::max(s.exposure - 0.5f, -4.0f); break;
    case GLFW_KEY_EQUAL: s.exposure = std::min(s.exposure + 0.5f, 4.0f); break;
    case GLFW_KEY_UP: s.glossiness = std::min(s.glossiness + 0.05f, 1.0f); break;
    case GLFW_KEY_DOWN: s.glossiness = std::max(s.glossiness - 0.05f, 0.0f); break;
    case GLFW_KEY_RIGHT: s.reflectivity = std::min(s.reflectivity + 0.05f, 1.0f); break;
    case GLFW_KEY_LEFT: s.reflectivity = std::max(s.reflectivity - 0.05f, 0.0f); break;
    case GLFW_KEY_R: context->camera = OrbitCamera {}; s.env_rot_dest = 0.0f; break;
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
    default: break;
    }
}

void cursor_callback(GLFWwindow* window, double x, double y)
{
    auto* context = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
    // UI 捕获鼠标时不驱动相机，但仍更新 prev，避免离开 UI 后镜头跳变。
    if (ImGui::GetIO().WantCaptureMouse)
    {
        context->prev_x = x;
        context->prev_y = y;
        context->has_prev = true;
        return;
    }
    if (!context->has_prev)
    {
        context->prev_x = x;
        context->prev_y = y;
        context->has_prev = true;
        return;
    }

    const float dx = static_cast<float>(x - context->prev_x) / static_cast<float>(ibl_width);
    const float dy = static_cast<float>(y - context->prev_y) / static_cast<float>(ibl_height);
    context->prev_x = x;
    context->prev_y = y;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        context->camera.orbit(dx, dy);
    else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
        context->camera.dolly(dx + dy);
    else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
        context->settings.env_rot_dest += dx * 2.0f;
}

void scroll_callback(GLFWwindow* window, double /*dx*/, double dy)
{
    if (ImGui::GetIO().WantCaptureMouse)
        return;
    auto* context = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
    context->camera.dolly(static_cast<float>(-dy) * 0.05f);
}

void print_controls()
{
    std::cout << "== example_ibl controls ==\n"
              << "  mouse left drag   : orbit camera\n"
              << "  mouse right drag  : dolly\n"
              << "  mouse middle drag : rotate environment\n"
              << "  scroll            : dolly\n"
              << "  1 / 2             : light probe bolonga / kyoto\n"
              << "  M                 : mesh bunny / orbs\n"
              << "  W                 : metalness / specular workflow (bunny)\n"
              << "  I / O             : toggle IBL diffuse / specular\n"
              << "  D / S             : toggle direct diffuse / specular\n"
              << "  B                 : cycle background (radiance mip 0-6, irradiance)\n"
              << "  - / =             : exposure down / up\n"
              << "  Up / Down         : glossiness (bunny)\n"
              << "  Left / Right      : reflectivity (bunny)\n"
              << "  R                 : reset camera\n"
              << "  Esc               : quit\n";
}

} // namespace

void run_example_ibl()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(ibl_width, ibl_height, "Horizon IBL [Vulkan]", nullptr, nullptr);

    InputContext input;
    glfwSetWindowUserPointer(window, &input);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetScrollCallback(window, scroll_callback);
    print_controls();

    // ---- 资源 ----
    LightProbe probes[2] = { load_light_probe("bolonga"), load_light_probe("kyoto") };

    IblMesh bunny_mesh = load_bgfx_mesh(asset_root / "meshes" / "bunny.bin");
    IblMesh orb_mesh = load_bgfx_mesh(asset_root / "meshes" / "orb.bin");

    // 天空盒：一个覆盖全屏的 NDC 三角形，顶点直接钉在远平面。
    const std::vector<IblVertex> sky_vertices = {
        { { -1.0f, -1.0f, 1.0f }, {} },
        { { 3.0f, -1.0f, 1.0f }, {} },
        { { -1.0f, 3.0f, 1.0f }, {} },
    };
    const std::vector<uint32_t> sky_indices = { 0, 1, 2 };

    horizon::HardwareBuffer bunny_vb = horizon::HardwareBuffer::vertex(bunny_mesh.vertices, "example_ibl.bunny.vb");
    horizon::HardwareBuffer bunny_ib = horizon::HardwareBuffer::index(bunny_mesh.indices, "example_ibl.bunny.ib");
    horizon::HardwareBuffer orb_vb = horizon::HardwareBuffer::vertex(orb_mesh.vertices, "example_ibl.orb.vb");
    horizon::HardwareBuffer orb_ib = horizon::HardwareBuffer::index(orb_mesh.indices, "example_ibl.orb.ib");
    horizon::HardwareBuffer sky_vb = horizon::HardwareBuffer::vertex(sky_vertices, "example_ibl.sky.vb");
    horizon::HardwareBuffer sky_ib = horizon::HardwareBuffer::index(sky_indices, "example_ibl.sky.ib");

    // ---- 渲染目标 ----
    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        ibl_width, ibl_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_ibl.output"));
    final_output_image.set_clear_color(0.19f, 0.19f, 0.19f, 1.0f);

    horizon::HardwareImage depth_image(horizon::HardwareImageDesc::depth_attachment(
        ibl_width, ibl_height, horizon::Format::D32, "example_ibl.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    // ---- Pipeline ----
    horizon::RasterizerPipelineDesc desc;
    desc.blend_enabled = false;

    horizon::RasterizerPipeline rasterizer(ibl_vert_glsl, ibl_frag_glsl, desc);
    rasterizer.outColor = final_output_image;
    rasterizer.bind_depth_target(depth_image);

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    horizon::DrawIndexedParams bunny_params;
    bunny_params.index_count = static_cast<uint32_t>(bunny_mesh.indices.size());

    horizon::DrawIndexedParams orb_params;
    orb_params.index_count = static_cast<uint32_t>(orb_mesh.indices.size());

    horizon::DrawIndexedParams sky_params;
    sky_params.index_count = static_cast<uint32_t>(sky_indices.size());

    constexpr float aspect = static_cast<float>(ibl_width) / static_cast<float>(ibl_height);
    // bgfx 是左手系（屏幕右 = 世界 +x），跟随之以保证天空盒方向基与网格反射一致。
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f; // GLM 面向 OpenGL，翻转 Y 以适配 Vulkan 裁剪空间
        return m;
    }();

    HorizonImGuiLayer ui(window, ibl_width, ibl_height);

    auto prev_time = std::chrono::high_resolution_clock::now();
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
        prev_time = now;

        // 帧率显示：每 0.5s 刷新一次窗口标题。
        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[96];
            std::snprintf(title, sizeof(title), "Horizon IBL [Vulkan] - %.1f FPS (%.2f ms)", fps, 1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        Settings& s = input.settings;
        OrbitCamera& camera = input.camera;
        camera.update(dt);
        s.env_rot_curr = glm::mix(s.env_rot_curr, s.env_rot_dest, std::min(dt / 0.12f, 1.0f));

        const glm::vec3 cam_pos = camera.position();
        const glm::mat4 view = glm::lookAtLH(cam_pos, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 env_rot = glm::rotate(glm::mat4(1.0f), s.env_rot_curr, glm::vec3(0.0f, 1.0f, 0.0f));
        const LightProbe& probe = probes[s.current_probe];

        rasterizer.clear_records();

        // 帧内共享的绑定与 uniform（UBO，record 前写入一次即可）。
        // VS/FS 共用同一个 binding=0 的 IblShared block，经 vsp 代理写入。
        // per-draw 数据（model、params0、misc）通过 push constant pc 写入。
        // cube 图存入 bindless combined-texture 表（set 0），索引经 push constant 传入。
        // store_descriptor 幂等（首帧写入后缓存索引），逐帧调用无额外开销。
        rasterizer.vpc.texCubeIndex = probe.lod.store_descriptor();
        rasterizer.vpc.texCubeIrrIndex = probe.irr.store_descriptor();
        rasterizer.vsp.proj_view = proj * view;   // NEW：VS 内算 mvp = proj_view * pc.model
        rasterizer.vsp.camPos = glm::vec4(cam_pos, 1.0f);
        rasterizer.vsp.flags = glm::vec4(s.do_diffuse ? 1.0f : 0.0f, s.do_specular ? 1.0f : 0.0f,
                                         s.do_diffuse_ibl ? 1.0f : 0.0f, s.do_specular_ibl ? 1.0f : 0.0f);
        rasterizer.vsp.rgbDiff = glm::vec4(s.rgb_diff, 1.0f);
        rasterizer.vsp.rgbSpec = glm::vec4(s.rgb_spec, 1.0f);
        rasterizer.vsp.lightDir = glm::vec4(s.light_dir, 0.0f);
        rasterizer.vsp.lightCol = glm::vec4(s.light_col, 0.0f);
        rasterizer.vsp.envMtx = env_rot;
        rasterizer.vsp.skyEnvMtx = env_rot * camera.env_view_mtx();

        // ---- 网格 draw（先画，深度 LessOrEqual 下天空盒只补背景）----
        if (s.mesh_selection == 0)
        {
            // bunny：等价于 bgfx mtxSRT(1,1,1, 0,π,0, 0,-0.8,0)
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.8f, 0.0f)) *
                                    glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            rasterizer.vpc.misc    = glm::vec4(0.0f, aspect, static_cast<float>(s.metal_or_spec), 0.0f);
            rasterizer.vpc.model   = model;
            rasterizer.vpc.params0 = glm::vec4(s.glossiness, s.reflectivity, s.exposure, s.bg_type);
            rasterizer.record(bunny_ib, bunny_vb, bunny_params);
        }
        else
        {
            // orbs：5x5 球体阵列，逐球变化 glossiness / reflectivity（push constant per-draw）。
            constexpr float grid = 5.0f;
            constexpr float scale = 1.2f;
            constexpr float spacing = 2.2f;
            constexpr float y_adj = -0.8f;

            for (float yy = 0.0f; yy < grid; yy += 1.0f)
            {
                for (float xx = 0.0f; xx < grid; xx += 1.0f)
                {
                    const float tx = (xx / grid) * spacing - (1.0f + (scale - 1.0f) * 0.5f - 1.0f / grid);
                    const float ty = y_adj / grid + (yy / grid) * spacing - (1.0f + (scale - 1.0f) * 0.5f - 1.0f / grid);
                    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(tx, ty, 0.0f)) *
                                            glm::scale(glm::mat4(1.0f), glm::vec3(scale / grid));

                    rasterizer.vpc.misc    = glm::vec4(0.0f, aspect, 0.0f, 0.0f);
                    rasterizer.vpc.model   = model;
                    rasterizer.vpc.params0 =
                        glm::vec4(xx * (1.0f / grid), (grid - yy) * (1.0f / grid), s.exposure, s.bg_type);
                    rasterizer.record(orb_ib, orb_vb, orb_params);
                }
            }
        }

        // ---- 天空盒 draw（pc.misc.x = 1 触发 skybox 路径）----
        rasterizer.vpc.misc    = glm::vec4(1.0f, aspect, 0.0f, 0.0f);
        rasterizer.vpc.model   = glm::mat4(1.0f); // identity，skybox 路径不使用 model
        rasterizer.vpc.params0 = glm::vec4(s.glossiness, s.reflectivity, s.exposure, s.bg_type);
        rasterizer.record(sky_ib, sky_vb, sky_params);

        horizon::SubmitReceipt render_receipt =
            render_executor << rasterizer.extent(ibl_width, ibl_height) << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
