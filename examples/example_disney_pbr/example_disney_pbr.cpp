// Disney Principled PBR demo using the same scene as example_ibl (orbs / bunny + IBL probes).

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Codegen/ControlFlows.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
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
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t disney_width = 1280;
constexpr uint32_t disney_height = 720;
constexpr size_t orb_count = 25;

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

Corona::Horizon::HardwareImage create_cubemap_image(const CubeMapData& dds, const std::string& name)
{
    constexpr uint32_t bytes_per_pixel = 8;

    Corona::Horizon::HardwareImageDesc desc = Corona::Horizon::HardwareImageDesc::cube(
        dds.size, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferDst, name);
    desc.mip_levels = dds.mip_count;

    Corona::Horizon::HardwareImage image(desc);

    Corona::Horizon::HardwareBufferDesc staging_desc;
    staging_desc.element_count = dds.payload.size();
    staging_desc.element_size = 1;
    staging_desc.usage = Corona::Horizon::BufferUsageFlags::TransferSrc;
    staging_desc.cpu_access = Corona::Horizon::CpuAccessMode::Write;
    Corona::Horizon::HardwareBuffer staging(staging_desc, std::span<const std::byte>(dds.payload));

    Corona::Horizon::HardwareExecutor executor;
    Corona::Horizon::HardwareStream stream = executor.stream();
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
    (void)(stream << Corona::Horizon::commit());

    return image;
}

struct LightProbe
{
    Corona::Horizon::HardwareImage lod;
    Corona::Horizon::HardwareImage irr;
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

    constexpr uint16_t attrib_id_position = 0x0001;
    constexpr uint16_t attrib_id_normal = 0x0002;
    constexpr uint16_t attrib_type_id_uint8 = 0x0001;
    constexpr uint16_t attrib_type_id_float = 0x0004;

    IblMesh mesh;
    uint32_t group_base_vertex = 0;
    size_t cursor = 0;

    while (cursor + sizeof(uint32_t) <= bytes.size())
    {
        const uint32_t chunk = read_pod<uint32_t>(bytes, cursor);
        if (chunk == chunk_vb)
        {
            cursor += 16 + 24 + 64;

            const uint8_t num_attrs = read_pod<uint8_t>(bytes, cursor);
            const uint16_t stride = read_pod<uint16_t>(bytes, cursor);

            int32_t position_offset = -1;
            int32_t normal_offset = -1;
            uint16_t normal_type = 0;
            for (uint8_t i = 0; i < num_attrs; ++i)
            {
                const uint16_t attr_offset = read_pod<uint16_t>(bytes, cursor);
                const uint16_t attr_id = read_pod<uint16_t>(bytes, cursor);
                cursor += 1;
                const uint16_t type_id = read_pod<uint16_t>(bytes, cursor);
                cursor += 2;

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
                cursor += 16 + 16 + 24 + 64;
            }
        }
        else
        {
            throw std::runtime_error("Unsupported chunk in mesh (compressed?): " + path.string());
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

struct Settings
{
    float exposure = 0.0f;
    float bg_type = 3.0f;
    float env_rot_curr = 0.0f;
    float env_rot_dest = 0.0f;
    bool do_diffuse = true;
    bool do_specular = true;
    bool do_diffuse_ibl = true;
    bool do_specular_ibl = true;
    int mesh_selection = 1; // 0: bunny, 1: orbs
    int current_probe = 0;
    int selected_orb = 0;
    glm::vec3 light_dir { -0.8f, 0.2f, -0.5f };
    glm::vec3 light_col { 1.0f, 1.0f, 1.0f };
    DisneyMaterial bunny_material {};
    std::array<DisneyMaterial, orb_count> orb_materials {};
};

struct OrbitCamera
{
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
        pitch_dest = std::clamp(pitch_dest - dy * glm::pi<float>(), 0.02f * glm::pi<float>(),
                                0.98f * glm::pi<float>());
    }

    void dolly(float dz) { dist_dest = std::clamp(dist_dest + dist_dest * dz, 1.0f, 100.0f); }

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

void init_orb_materials(std::array<DisneyMaterial, orb_count>& materials)
{
    for (size_t i = 0; i < orb_count; ++i)
    {
        const float u = static_cast<float>(i % 5) / 4.0f;
        const float v = static_cast<float>(i / 5) / 4.0f;
        materials[i].roughness = u;
        materials[i].metallic = v;
        materials[i].base_color = glm::mix(glm::vec3(0.9f, 0.1f, 0.1f), glm::vec3(0.9f, 0.85f, 0.2f), v);
    }
}

template <typename VertShader, typename FragShader>
void apply_material_to_pipeline(Corona::Horizon::RasterizerPipeline<VertShader, FragShader>& rasterizer,
                                const DisneyMaterial& material, float is_skybox, float aspect, float bg_type)
{
    rasterizer.vpc.mat0 = glm::vec4(material.base_color, material.metallic);
    rasterizer.vpc.mat1 =
        glm::vec4(material.roughness, material.specular, material.specular_tint, material.subsurface);
    rasterizer.vpc.mat2 =
        glm::vec4(material.anisotropic, material.sheen, material.sheen_tint, material.clearcoat);
    rasterizer.vpc.mat3 = glm::vec4(material.clearcoat_gloss, is_skybox, aspect, bg_type);
}

void apply_preset(DisneyMaterial& material, int preset)
{
    switch (preset)
    {
    case 0: // gold
        material = DisneyMaterial {};
        break;
    case 1: // plastic
        material.base_color = glm::vec3(0.05f, 0.5f, 0.9f);
        material.metallic = 0.0f;
        material.roughness = 0.35f;
        material.specular = 0.5f;
        material.clearcoat = 0.0f;
        break;
    case 2: // car paint
        material.base_color = glm::vec3(0.8f, 0.05f, 0.05f);
        material.metallic = 0.0f;
        material.roughness = 0.25f;
        material.clearcoat = 1.0f;
        material.clearcoat_gloss = 0.9f;
        break;
    case 3: // fabric
        material.base_color = glm::vec3(0.6f, 0.55f, 0.5f);
        material.metallic = 0.0f;
        material.roughness = 0.9f;
        material.sheen = 1.0f;
        material.sheen_tint = 0.5f;
        break;
    default: break;
    }
}

void draw_material_editor(DisneyMaterial& material)
{
    ImGui::ColorEdit3("Base Color", &material.base_color.x);
    ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
    ImGui::SliderFloat("Specular", &material.specular, 0.0f, 1.0f);
    ImGui::SliderFloat("Specular Tint", &material.specular_tint, 0.0f, 1.0f);
    ImGui::SliderFloat("Subsurface", &material.subsurface, 0.0f, 1.0f);
    ImGui::SliderFloat("Anisotropic", &material.anisotropic, 0.0f, 1.0f);
    ImGui::SliderFloat("Sheen", &material.sheen, 0.0f, 1.0f);
    ImGui::SliderFloat("Sheen Tint", &material.sheen_tint, 0.0f, 1.0f);
    ImGui::SliderFloat("Clearcoat", &material.clearcoat, 0.0f, 1.0f);
    ImGui::SliderFloat("Clearcoat Gloss", &material.clearcoat_gloss, 0.0f, 1.0f);

    if (ImGui::Button("Gold"))
        apply_preset(material, 0);
    ImGui::SameLine();
    if (ImGui::Button("Plastic"))
        apply_preset(material, 1);
    ImGui::SameLine();
    if (ImGui::Button("Car Paint"))
        apply_preset(material, 2);
    ImGui::SameLine();
    if (ImGui::Button("Fabric"))
        apply_preset(material, 3);
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
    init_orb_materials(input.settings.orb_materials);
    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetScrollCallback(window, scroll_callback);

    LightProbe probes[2] = { load_light_probe("bolonga"), load_light_probe("kyoto") };

    IblMesh bunny_mesh = load_bgfx_mesh(asset_root / "meshes" / "bunny.bin");
    IblMesh orb_mesh = load_bgfx_mesh(asset_root / "meshes" / "orb.bin");

    const std::vector<IblVertex> sky_vertices = {
        { { -1.0f, -1.0f, 1.0f }, {} },
        { { 3.0f, -1.0f, 1.0f }, {} },
        { { -1.0f, 3.0f, 1.0f }, {} },
    };
    const std::vector<uint32_t> sky_indices = { 0, 1, 2 };

    Corona::Horizon::HardwareBuffer bunny_vb =
        Corona::Horizon::HardwareBuffer::vertex(bunny_mesh.vertices, "example_disney_pbr.bunny.vb");
    Corona::Horizon::HardwareBuffer bunny_ib =
        Corona::Horizon::HardwareBuffer::index(bunny_mesh.indices, "example_disney_pbr.bunny.ib");
    Corona::Horizon::HardwareBuffer orb_vb =
        Corona::Horizon::HardwareBuffer::vertex(orb_mesh.vertices, "example_disney_pbr.orb.vb");
    Corona::Horizon::HardwareBuffer orb_ib =
        Corona::Horizon::HardwareBuffer::index(orb_mesh.indices, "example_disney_pbr.orb.ib");
    Corona::Horizon::HardwareBuffer sky_vb =
        Corona::Horizon::HardwareBuffer::vertex(sky_vertices, "example_disney_pbr.sky.vb");
    Corona::Horizon::HardwareBuffer sky_ib =
        Corona::Horizon::HardwareBuffer::index(sky_indices, "example_disney_pbr.sky.ib");

    Corona::Horizon::HardwareImage final_output_image(Corona::Horizon::HardwareImageDesc::texture_2d(
        disney_width, disney_height, Corona::Horizon::Format::RGBA16_FLOAT,
        Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
            Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
            Corona::Horizon::ImageUsageFlags::TransferDst,
        "example_disney_pbr.output"));
    final_output_image.set_clear_color(0.19f, 0.19f, 0.19f, 1.0f);

    Corona::Horizon::HardwareImage depth_image(Corona::Horizon::HardwareImageDesc::depth_attachment(
        disney_width, disney_height, Corona::Horizon::Format::D32, "example_disney_pbr.depth"));
    depth_image.set_clear_depth(1.0f, 0);

    Corona::Horizon::RasterizerPipelineDesc desc;
    desc.blend.attachments = { Corona::Horizon::BlendStateDesc::opaque_attachment() };

    Corona::Horizon::RasterizerPipeline rasterizer(disney_pbr_vert_glsl, disney_pbr_frag_glsl, desc);
    rasterizer.outColor = final_output_image;
    rasterizer.bind_depth_target(depth_image);

    Corona::Horizon::HardwareExecutor render_executor;
    Corona::Horizon::HardwareExecutor display_executor;
    Corona::Horizon::HardwareDisplayer display(glfwGetWin32Window(window));

    Corona::Horizon::DrawIndexedParams bunny_params;
    bunny_params.index_type = Corona::Horizon::IndexType::UInt32;
    bunny_params.index_count = static_cast<uint32_t>(bunny_mesh.indices.size());

    Corona::Horizon::DrawIndexedParams orb_params;
    orb_params.index_type = Corona::Horizon::IndexType::UInt32;
    orb_params.index_count = static_cast<uint32_t>(orb_mesh.indices.size());

    Corona::Horizon::DrawIndexedParams sky_params;
    sky_params.index_type = Corona::Horizon::IndexType::UInt32;
    sky_params.index_count = static_cast<uint32_t>(sky_indices.size());

    constexpr float aspect = static_cast<float>(disney_width) / static_cast<float>(disney_height);
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
        ImGui::Begin("Disney PBR");
        ImGui::Text("Scene (same as example_ibl)");
        const char* mesh_names[] = { "Bunny", "Orbs (5x5)" };
        ImGui::Combo("Mesh", &s.mesh_selection, mesh_names, IM_ARRAYSIZE(mesh_names));
        const char* probe_names[] = { "Bolonga", "Kyoto" };
        ImGui::Combo("Light Probe", &s.current_probe, probe_names, IM_ARRAYSIZE(probe_names));
        ImGui::SliderFloat("Exposure", &s.exposure, -4.0f, 4.0f);
        ImGui::SliderFloat("Background Mip", &s.bg_type, 0.0f, 7.0f, "%.0f (7 = irradiance)");
        ImGui::Checkbox("Direct Diffuse", &s.do_diffuse);
        ImGui::Checkbox("Direct Specular", &s.do_specular);
        ImGui::Checkbox("IBL Diffuse", &s.do_diffuse_ibl);
        ImGui::Checkbox("IBL Specular", &s.do_specular_ibl);
        ImGui::ColorEdit3("Light Color", &s.light_col.x);
        ImGui::SliderFloat3("Light Direction", &s.light_dir.x, -1.0f, 1.0f);

        ImGui::Separator();
        ImGui::Text("Material");

        DisneyMaterial* active_material = &s.bunny_material;
        if (s.mesh_selection == 1)
        {
            ImGui::SliderInt("Object Index", &s.selected_orb, 0, static_cast<int>(orb_count) - 1);
            active_material = &s.orb_materials[static_cast<size_t>(s.selected_orb)];
            if (ImGui::Button("Apply to All Orbs"))
            {
                const DisneyMaterial copy = *active_material;
                for (auto& orb_mat : s.orb_materials)
                    orb_mat = copy;
            }
        }

        draw_material_editor(*active_material);
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

        if (s.mesh_selection == 0)
        {
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.8f, 0.0f)) *
                                    glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            rasterizer.vpc.model = model;
            apply_material_to_pipeline(rasterizer, s.bunny_material, 0.0f, aspect, s.bg_type);
            rasterizer.record(bunny_ib, bunny_vb, bunny_params);
        }
        else
        {
            constexpr float grid = 5.0f;
            constexpr float scale = 1.2f;
            constexpr float spacing = 2.2f;
            constexpr float y_adj = -0.8f;

            for (float yy = 0.0f; yy < grid; yy += 1.0f)
            {
                for (float xx = 0.0f; xx < grid; xx += 1.0f)
                {
                    const size_t orb_index = static_cast<size_t>(yy * grid + xx);
                    const float tx =
                        (xx / grid) * spacing - (1.0f + (scale - 1.0f) * 0.5f - 1.0f / grid);
                    const float ty =
                        y_adj / grid + (yy / grid) * spacing - (1.0f + (scale - 1.0f) * 0.5f - 1.0f / grid);
                    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(tx, ty, 0.0f)) *
                                            glm::scale(glm::mat4(1.0f), glm::vec3(scale / grid));

                    rasterizer.vpc.model = model;
                    apply_material_to_pipeline(rasterizer, s.orb_materials[orb_index], 0.0f, aspect, s.bg_type);
                    rasterizer.record(orb_ib, orb_vb, orb_params);
                }
            }
        }

        DisneyMaterial sky_dummy {};
        rasterizer.vpc.model = glm::mat4(1.0f);
        apply_material_to_pipeline(rasterizer, sky_dummy, 1.0f, aspect, s.bg_type);
        rasterizer.record(sky_ib, sky_vb, sky_params);

        Corona::Horizon::SubmitReceipt render_receipt =
            render_executor << rasterizer(disney_width, disney_height) << Corona::Horizon::submit;

        ui.draw_overlay(display_executor, final_output_image, render_receipt);
        display_executor.wait(render_receipt);
        (void)(display_executor.stream() << Corona::Horizon::present(display, final_output_image)
                                         << Corona::Horizon::commit());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
