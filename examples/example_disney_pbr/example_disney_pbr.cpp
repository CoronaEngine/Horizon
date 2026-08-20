// Disney Principled PBR chart: 10 parameter rows x 11 value columns (0.0 .. 1.0).

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "horizon.h"
#include "imgui_horizon.h"

#include <imgui.h>

#include GLSL(shaders/disney_pbr_vert.glsl)
#include GLSL(shaders/disney_pbr_frag.glsl)

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t disney_width = 1280;
constexpr uint32_t disney_height = 720;
constexpr int chart_row_count = 10;
constexpr int chart_col_count = 11;
constexpr int chart_sphere_count = chart_row_count * chart_col_count;

const std::filesystem::path asset_root =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";

struct CubeMapData
{
    uint32_t size = 0;
    uint32_t mip_count = 0;
    std::vector<std::byte> payload;
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

    constexpr uint32_t dds_magic = 0x20534444;
    constexpr uint32_t fourcc_dx10 = 0x30315844;
    constexpr uint32_t dxgi_format_rgba16_float = 10;
    constexpr uint32_t caps2_cubemap = 0x200;
    constexpr size_t dx10_payload_offset = 148;

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
    constexpr uint32_t bytes_per_pixel = 8;

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
    probe.lod = create_cubemap_image(load_dds_cube_rgba16f(asset_root / "env" / (name + "_lod.dds")),
                                     "example_disney_pbr." + name + ".lod");
    probe.irr = create_cubemap_image(load_dds_cube_rgba16f(asset_root / "env" / (name + "_irr.dds")),
                                     "example_disney_pbr." + name + ".irr");
    return probe;
}

struct IblVertex
{
    std::array<float, 3> position {};
    std::array<float, 3> normal {};
};

struct IblMesh
{
    std::vector<IblVertex> vertices;
    std::vector<uint16_t> indices;
};

IblMesh make_uv_sphere(int slices, int stacks)
{
    IblMesh mesh;
    mesh.vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));
    mesh.indices.reserve(static_cast<size_t>(slices * stacks * 6));

    for (int y = 0; y <= stacks; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(stacks);
        const float phi = v * glm::pi<float>();
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);

        for (int x = 0; x <= slices; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(slices);
            const float theta = u * glm::two_pi<float>();
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            IblVertex vertex;
            vertex.normal = { sin_phi * cos_theta, cos_phi, sin_phi * sin_theta };
            vertex.position = vertex.normal;
            mesh.vertices.push_back(vertex);
        }
    }

    for (int y = 0; y < stacks; ++y)
    {
        for (int x = 0; x < slices; ++x)
        {
            const uint16_t i0 = static_cast<uint16_t>(y * (slices + 1) + x);
            const uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            const uint16_t i2 = static_cast<uint16_t>(i0 + slices + 1);
            const uint16_t i3 = static_cast<uint16_t>(i2 + 1);

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
        }
    }

    return mesh;
}

struct DisneyMaterial
{
    glm::vec3 base_color { 0.82f, 0.67f, 0.16f };
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    float specular_tint = 0.0f;
    float subsurface = 0.0f;
    float anisotropic = 0.0f;
    float sheen = 0.0f;
    float sheen_tint = 0.5f;
    float clearcoat = 0.0f;
    float clearcoat_gloss = 1.0f;
};

enum class ChartParam : int
{
    Subsurface = 0,
    Metallic,
    Specular,
    SpecularTint,
    Roughness,
    Anisotropic,
    Sheen,
    SheenTint,
    Clearcoat,
    ClearcoatGloss,
};

constexpr const char* chart_param_names[] = {
    "subsurface",     "metallic",      "specular",    "specularTint", "roughness",
    "anisotropic",    "sheen",         "sheenTint",   "clearcoat",    "clearcoatGloss",
};

constexpr glm::vec3 chart_row_colors[chart_row_count] = {
    glm::vec3(1.0f, 1.0f, 1.0f),       // subsurface
    glm::vec3(1.0f, 0.86f, 0.12f),     // metallic
    glm::vec3(0.92f, 0.12f, 0.12f),    // specular
    glm::vec3(0.92f, 0.12f, 0.12f),    // specularTint
    glm::vec3(0.42f, 0.32f, 0.92f),    // roughness
    glm::vec3(0.92f, 0.18f, 0.78f),    // anisotropic
    glm::vec3(0.62f, 0.18f, 0.14f),    // sheen
    glm::vec3(0.62f, 0.18f, 0.14f),    // sheenTint
    glm::vec3(0.10f, 0.34f, 0.40f),    // clearcoat
    glm::vec3(0.10f, 0.34f, 0.40f),    // clearcoatGloss
};

DisneyMaterial make_default_chart_material(glm::vec3 base_color)
{
    DisneyMaterial material;
    material.base_color = base_color;
    return material;
}

DisneyMaterial make_chart_material(int row, int col)
{
    const float t = static_cast<float>(col) / static_cast<float>(chart_col_count - 1);
    DisneyMaterial material = make_default_chart_material(chart_row_colors[row]);

    switch (static_cast<ChartParam>(row))
    {
    case ChartParam::Subsurface: material.subsurface = t; break;
    case ChartParam::Metallic: material.metallic = t; break;
    case ChartParam::Specular: material.specular = t; break;
    case ChartParam::SpecularTint: material.specular_tint = t; break;
    case ChartParam::Roughness: material.roughness = t; break;
    case ChartParam::Anisotropic: material.anisotropic = t; break;
    case ChartParam::Sheen: material.sheen = t; break;
    case ChartParam::SheenTint: material.sheen_tint = t; break;
    case ChartParam::Clearcoat: material.clearcoat = t; break;
    case ChartParam::ClearcoatGloss:
        material.clearcoat = 1.0f;
        material.clearcoat_gloss = t;
        break;
    }

    return material;
}

std::array<DisneyMaterial, chart_sphere_count> build_chart_materials()
{
    std::array<DisneyMaterial, chart_sphere_count> materials {};
    for (int row = 0; row < chart_row_count; ++row)
    {
        for (int col = 0; col < chart_col_count; ++col)
            materials[static_cast<size_t>(row * chart_col_count + col)] = make_chart_material(row, col);
    }
    return materials;
}

glm::vec3 chart_sphere_position(int row, int col)
{
    constexpr float spacing = 1.12f;
    const float x = (static_cast<float>(col) - (chart_col_count - 1) * 0.5f) * spacing;
    const float y = ((chart_row_count - 1) * 0.5f - static_cast<float>(row)) * spacing;
    return glm::vec3(x, y, 0.0f);
}

struct Settings
{
    float exposure = 0.0f;
    float env_rot_curr = 0.0f;
    float env_rot_dest = 0.0f;
    bool do_diffuse = true;
    bool do_specular = true;
    bool do_diffuse_ibl = true;
    bool do_specular_ibl = true;
    int current_probe = 0;
    glm::vec3 light_dir { -0.35f, 0.55f, -0.75f };
    glm::vec3 light_col { 1.0f, 1.0f, 1.0f };
};

struct OrbitCamera
{
    float yaw_curr = glm::pi<float>();
    float yaw_dest = glm::pi<float>();
    float pitch_curr = 0.42f * glm::pi<float>();
    float pitch_dest = 0.42f * glm::pi<float>();
    float dist_curr = 13.5f;
    float dist_dest = 13.5f;
    glm::vec3 target { 0.0f };

    void orbit(float dx, float dy)
    {
        yaw_dest += dx * glm::two_pi<float>();
        pitch_dest = std::clamp(pitch_dest - dy * glm::pi<float>(), 0.15f * glm::pi<float>(),
                                0.85f * glm::pi<float>());
    }

    void dolly(float dz) { dist_dest = std::clamp(dist_dest + dist_dest * dz, 4.0f, 40.0f); }

    void update(float dt)
    {
        const float amount = std::min(dt / 0.12f, 1.0f);
        yaw_curr = glm::mix(yaw_curr, yaw_dest, amount);
        pitch_curr = glm::mix(pitch_curr, pitch_dest, amount);
        dist_curr = glm::mix(dist_curr, dist_dest, amount);
    }

    [[nodiscard]] glm::vec3 position() const
    {
        const glm::vec3 dir(std::sin(pitch_curr) * std::sin(yaw_curr), std::cos(pitch_curr),
                            std::sin(pitch_curr) * std::cos(yaw_curr));
        return target + dir * dist_curr;
    }

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

template <typename VertShader, typename FragShader>
void apply_material_to_pipeline(horizon::RasterizerPipeline<VertShader, FragShader>& rasterizer,
                                const DisneyMaterial& material, float aspect)
{
    rasterizer.vpc.mat0 = glm::vec4(material.base_color, material.metallic);
    rasterizer.vpc.mat1 =
        glm::vec4(material.roughness, material.specular, material.specular_tint, material.subsurface);
    rasterizer.vpc.mat2 =
        glm::vec4(material.anisotropic, material.sheen, material.sheen_tint, material.clearcoat);
    rasterizer.vpc.mat3 = glm::vec4(material.clearcoat_gloss, 0.0f, aspect, 0.0f);
}

void cursor_callback(GLFWwindow* window, double x, double y)
{
    auto* context = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
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

    const float dx = static_cast<float>(x - context->prev_x) / static_cast<float>(disney_width);
    const float dy = static_cast<float>(y - context->prev_y) / static_cast<float>(disney_height);
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

} // namespace

void run_example_disney_pbr()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window =
        glfwCreateWindow(disney_width, disney_height, "Horizon Disney PBR [Vulkan]", nullptr, nullptr);

    InputContext input;
    const std::array<DisneyMaterial, chart_sphere_count> chart_materials = build_chart_materials();

    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetScrollCallback(window, scroll_callback);

    LightProbe probes[2] = { load_light_probe("bolonga"), load_light_probe("kyoto") };
    IblMesh sphere_mesh = make_uv_sphere(48, 24);

    horizon::HardwareBuffer sphere_vb =
        horizon::HardwareBuffer::vertex(sphere_mesh.vertices, "example_disney_pbr.sphere.vb");
    horizon::HardwareBuffer sphere_ib =
        horizon::HardwareBuffer::index(sphere_mesh.indices, "example_disney_pbr.sphere.ib");

    // Indirect args buffer for single draw
    horizon::DrawIndexedIndirectCommand indirect_cmd;
    indirect_cmd.index_count = static_cast<uint32_t>(sphere_mesh.indices.size());
    indirect_cmd.instance_count = 1;
    indirect_cmd.first_index = 0;
    indirect_cmd.vertex_offset = 0;
    indirect_cmd.first_instance = 0;

    horizon::HardwareBuffer indirect_args_buffer = horizon::HardwareBuffer::from_bytes(
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(&indirect_cmd),
            sizeof(horizon::DrawIndexedIndirectCommand)),
        static_cast<uint32_t>(sizeof(horizon::DrawIndexedIndirectCommand)),
        horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
        "example_disney_pbr.indirect_args");

    horizon::HardwareImage final_output_image(horizon::HardwareImageDesc::texture_2d(
        disney_width, disney_height, horizon::Format::RGBA16_FLOAT,
        horizon::ImageUsage_Storage | horizon::ImageUsage_ColorAttachment |
            horizon::ImageUsage_Sampled | horizon::ImageUsage_TransferSrc |
            horizon::ImageUsage_TransferDst,
        "example_disney_pbr.output"));
    final_output_image.set_clear_color(0.5f, 0.5f, 0.5f, 1.0f);

    horizon::HardwareImage depth_image(horizon::HardwareImageDesc::depth_attachment(
        disney_width, disney_height, horizon::Format::D32, "example_disney_pbr.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    horizon::RasterizerPipelineDesc desc;
    desc.blend_enabled = false;

    horizon::RasterizerPipeline rasterizer(disney_pbr_vert_glsl, disney_pbr_frag_glsl, desc);
    rasterizer.outColor = final_output_image;
    rasterizer.bind_depth_target(depth_image);

    horizon::HardwareExecutor render_executor;
    horizon::HardwareExecutor display_executor;
    horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    sphere_params.index_count = static_cast<uint32_t>(sphere_mesh.indices.size());

    constexpr float aspect = static_cast<float>(disney_width) / static_cast<float>(disney_height);
    constexpr float sphere_scale = 0.42f;
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f;
        return m;
    }();

    HorizonImGuiLayer ui(window, disney_width, disney_height);

    auto prev_time = std::chrono::high_resolution_clock::now();
    double fps_accum_seconds = 0.0;
    int fps_frame_count = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ui.new_frame();

        Settings& s = input.settings;
        ImGui::Begin("Disney PBR Chart");
        ImGui::Text("10 rows (parameter) x 11 columns (0.0 .. 1.0)");
        ImGui::Text("Rows: subsurface, metallic, specular, specularTint, roughness,");
        ImGui::Text("      anisotropic, sheen, sheenTint, clearcoat, clearcoatGloss");
        const char* probe_names[] = { "Bolonga", "Kyoto" };
        ImGui::Combo("Light Probe", &s.current_probe, probe_names, IM_ARRAYSIZE(probe_names));
        ImGui::SliderFloat("Exposure", &s.exposure, -4.0f, 4.0f);
        ImGui::Checkbox("Direct Diffuse", &s.do_diffuse);
        ImGui::Checkbox("Direct Specular", &s.do_specular);
        ImGui::Checkbox("IBL Diffuse", &s.do_diffuse_ibl);
        ImGui::Checkbox("IBL Specular", &s.do_specular_ibl);
        ImGui::ColorEdit3("Light Color", &s.light_col.x);
        ImGui::SliderFloat3("Light Direction", &s.light_dir.x, -1.0f, 1.0f);
        ImGui::Text("Camera: LMB orbit, RMB dolly, MMB rotate env, scroll zoom");
        ImGui::End();

        const auto now = std::chrono::high_resolution_clock::now();
        const float dt = std::chrono::duration<float>(now - prev_time).count();
        prev_time = now;

        fps_accum_seconds += dt;
        ++fps_frame_count;
        if (fps_accum_seconds >= 0.5)
        {
            const double fps = fps_frame_count / fps_accum_seconds;
            char title[96];
            std::snprintf(title, sizeof(title), "Horizon Disney PBR [Vulkan] - %.1f FPS (%.2f ms)", fps,
                          1000.0 / fps);
            glfwSetWindowTitle(window, title);
            fps_accum_seconds = 0.0;
            fps_frame_count = 0;
        }

        OrbitCamera& camera = input.camera;
        camera.update(dt);
        s.env_rot_curr = glm::mix(s.env_rot_curr, s.env_rot_dest, std::min(dt / 0.12f, 1.0f));

        const glm::vec3 cam_pos = camera.position();
        const glm::mat4 view = glm::lookAtLH(cam_pos, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 env_rot = glm::rotate(glm::mat4(1.0f), s.env_rot_curr, glm::vec3(0.0f, 1.0f, 0.0f));
        const LightProbe& probe = probes[s.current_probe];

        rasterizer.clear_records();

        rasterizer.vpc.texCubeIndex = probe.lod.store_descriptor();
        rasterizer.vpc.texCubeIrrIndex = probe.irr.store_descriptor();
        rasterizer.vsp.proj_view = proj * view;
        rasterizer.vsp.camPos = glm::vec4(cam_pos, 1.0f);
        rasterizer.vsp.flags = glm::vec4(s.do_diffuse ? 1.0f : 0.0f, s.do_specular ? 1.0f : 0.0f,
                                         s.do_diffuse_ibl ? 1.0f : 0.0f, s.do_specular_ibl ? 1.0f : 0.0f);
        rasterizer.vsp.lightDir = glm::vec4(glm::normalize(s.light_dir), 0.0f);
        rasterizer.vsp.lightCol = glm::vec4(s.light_col, 0.0f);
        rasterizer.vsp.envMtx = env_rot;
        rasterizer.vsp.skyEnvMtx = env_rot * camera.env_view_mtx();
        rasterizer.vsp.exposurePad = glm::vec4(s.exposure, 0.0f, 0.0f, 0.0f);

        // Build indirect commands for all spheres in the material chart
        std::vector<horizon::DrawIndexedIndirectCommand> indirect_cmds;
        indirect_cmds.reserve(chart_row_count * chart_col_count);

        for (int row = 0; row < chart_row_count; ++row)
        {
            for (int col = 0; col < chart_col_count; ++col)
            {
                const size_t index = static_cast<size_t>(row * chart_col_count + col);
                const glm::vec3 position = chart_sphere_position(row, col);
                const glm::mat4 model =
                    glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), glm::vec3(sphere_scale));

                rasterizer.vpc.model = model;
                apply_material_to_pipeline(rasterizer, chart_materials[index], aspect);

                horizon::DrawIndexedIndirectCommand cmd;
                cmd.index_count = static_cast<uint32_t>(sphere_mesh.indices.size());
                cmd.instance_count = 1;
                cmd.first_index = 0;
                cmd.vertex_offset = 0;
                cmd.first_instance = static_cast<uint32_t>(indirect_cmds.size());
                indirect_cmds.push_back(cmd);
            }
        }

        horizon::HardwareBuffer frame_indirect_buffer = horizon::HardwareBuffer::from_bytes(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(indirect_cmds.data()),
                indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
            static_cast<uint32_t>(indirect_cmds.size() * sizeof(horizon::DrawIndexedIndirectCommand)),
            horizon::BufferUsage_TransferDst | horizon::BufferUsage_Indirect,
            "example_disney_pbr.frame_indirect");

        horizon::DrawIndexedIndirectParams indirect_params;
        indirect_params.draw_count = static_cast<uint32_t>(indirect_cmds.size());
        indirect_params.indirect_offset = 0;
        indirect_params.stride = sizeof(horizon::DrawIndexedIndirectCommand);
        rasterizer.record_indirect(sphere_ib, sphere_vb, frame_indirect_buffer, indirect_params);

        horizon::SubmitReceipt render_receipt =
            render_executor << rasterizer.extent(disney_width, disney_height) << horizon::commit();

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << horizon::present(display, final_output_image)
                                         << horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
