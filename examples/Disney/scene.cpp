// Disney Principled PBR chart: 10 parameter rows x 11 values from 0 to 1.
#include "scene.h"
#include "Horizon.h"
#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace disney {
namespace {
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

HardwareImage create_cubemap_image(const CubeMapData& dds)
{
    HardwareImageCreateInfo desc(dds.size, dds.size, ImageFormat::RGBA16_FLOAT);
    desc.arrayLayers = 6;
    desc.cube = true;
    desc.linearFiltering = true;
    desc.mipLevels = static_cast<int>(dds.mip_count);
    desc.usage = ImageUsage::SampledImage;
    HardwareImage image(desc);
    HardwareBuffer staging(static_cast<uint32_t>(dds.payload.size()), BufferUsage::StorageBuffer, dds.payload.data());
    HardwareExecutor executor;
    uint64_t offset = 0;
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t mip = 0; mip < dds.mip_count; ++mip) {
            const uint32_t dim = std::max(1u, dds.size >> mip);
            const uint64_t bytes = static_cast<uint64_t>(dim) * dim * 8;
            if (offset + bytes > dds.payload.size()) throw std::runtime_error("Truncated DDS payload");
            executor << staging.copyTo(image, offset, face, mip);
            offset += bytes;
        }
    }
    executor.commit().waitForDeferredResources();
    return image;
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

DisneyMaterial make_chart_material(int row, int col)
{
    const float t = static_cast<float>(col) / static_cast<float>(chart_col_count - 1);
    DisneyMaterial material{.base_color = chart_row_colors[row]};

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

void zoom(OrbitCamera& camera, float amount) {
    camera.dist_dest = std::clamp(camera.dist_dest + camera.dist_dest * amount, 4.0f, 40.0f);
}

void cursor_callback(GLFWwindow* window, double x, double y)
{
    auto* context = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
    if (!context->has_prev)
    {
        context->prev_x = x;
        context->prev_y = y;
        context->has_prev = true;
        return;
    }

    int width, height;
    glfwGetWindowSize(window, &width, &height);
    const float dx = static_cast<float>(x - context->prev_x) / std::max(width, 1);
    const float dy = static_cast<float>(y - context->prev_y) / std::max(height, 1);
    context->prev_x = x;
    context->prev_y = y;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        auto& camera = context->camera;
        camera.yaw_dest += dx * glm::two_pi<float>();
        camera.pitch_dest = std::clamp(camera.pitch_dest - dy * glm::pi<float>(),
                                       0.15f * glm::pi<float>(), 0.85f * glm::pi<float>());
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
        zoom(context->camera, dx + dy);
    else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
        context->settings.env_rot_dest += dx * 2.0f;
}

void scroll_callback(GLFWwindow* window, double /*dx*/, double dy)
{
    auto* context = static_cast<InputContext*>(glfwGetWindowUserPointer(window));
    zoom(context->camera, static_cast<float>(-dy) * 0.05f);
}

} // namespace

CubeMapData read_cubemap(const char* filename) {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "assets" / "env" / filename;
    return load_dds_cube_rgba16f(path);
}

HardwareImage load_cubemap(const char* filename) {
    return create_cubemap_image(read_cubemap(filename));
}

IblMesh make_uv_sphere(int slices, int stacks)
{
    IblMesh mesh;
    mesh.vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));
    mesh.indices.reserve(static_cast<size_t>(stacks * (slices + 1) * 2 + (stacks - 1) * 2));

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

            mesh.vertices.push_back({ sin_phi * cos_theta, cos_phi, sin_phi * sin_theta });
        }
    }

    for (int y = 0; y < stacks; ++y)
    {
        const auto first = static_cast<uint16_t>(y * (slices + 1));
        if (y != 0) {
            mesh.indices.push_back(mesh.indices.back());
            mesh.indices.push_back(first);
        }
        for (int x = 0; x <= slices; ++x) {
            mesh.indices.push_back(static_cast<uint16_t>(first + x));
            mesh.indices.push_back(static_cast<uint16_t>(first + slices + 1 + x));
        }
    }

    return mesh;
}

namespace {
glm::vec3 chart_sphere_position(int row, int col)
{
    constexpr float spacing = 1.12f;
    const float x = (static_cast<float>(col) - (chart_col_count - 1) * 0.5f) * spacing;
    const float y = ((chart_row_count - 1) * 0.5f - static_cast<float>(row)) * spacing;
    return glm::vec3(x, y, 0.0f);
}

} // namespace

RasterGeometry make_sphere() {
    const auto mesh = make_uv_sphere(48, 24);
    return RasterGeometry::upload(mesh.vertices, mesh.indices);
}

DisneyDrawParameters sphere_parameters(int row, int col)
{
    constexpr float sphere_scale = 0.42f;
    DisneyDrawParameters draw{};
    const auto material = make_chart_material(row, col);
    draw.model = glm::translate(glm::mat4(1.0f), chart_sphere_position(row, col))
               * glm::scale(glm::mat4(1.0f), glm::vec3(sphere_scale));
    draw.mat0 = glm::vec4(material.base_color, material.metallic);
    draw.mat1 =
        glm::vec4(material.roughness, material.specular, material.specular_tint, material.subsurface);
    draw.mat2 =
        glm::vec4(material.anisotropic, material.sheen, material.sheen_tint, material.clearcoat);
    draw.clearcoatGloss = material.clearcoat_gloss;
    return draw;
}

FrameUniforms update_frame(InputContext& input, float dt) {
    static const glm::mat4 projection = [] {
        constexpr float aspect = static_cast<float>(disney_width) / disney_height;
        auto p = glm::perspectiveLH(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        p[1][1] *= -1.0f;
        return p;
    }();
    auto& camera = input.camera;
    auto& s = input.settings;
    const float amount = std::min(dt / 0.12f, 1.0f);
    camera.yaw_curr = glm::mix(camera.yaw_curr, camera.yaw_dest, amount);
    camera.pitch_curr = glm::mix(camera.pitch_curr, camera.pitch_dest, amount);
    camera.dist_curr = glm::mix(camera.dist_curr, camera.dist_dest, amount);
    s.env_rot_curr = glm::mix(s.env_rot_curr, s.env_rot_dest, amount);
    const glm::vec3 direction(std::sin(camera.pitch_curr) * std::sin(camera.yaw_curr), std::cos(camera.pitch_curr),
                              std::sin(camera.pitch_curr) * std::cos(camera.yaw_curr));
    const auto position = camera.target + direction * camera.dist_curr;
    FrameUniforms frame;
    frame.proj_view = projection * glm::lookAtLH(position, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
    frame.env_rotation = glm::rotate(glm::mat4(1.0f), s.env_rot_curr, glm::vec3(0.0f, 1.0f, 0.0f));
    frame.camera = glm::vec4(position, 1.0f);
    frame.flags = glm::vec4(s.do_diffuse, s.do_specular, s.do_diffuse_ibl, s.do_specular_ibl);
    frame.light_direction = glm::vec4(glm::normalize(s.light_dir), 0.0f);
    frame.light_color = glm::vec4(s.light_col, 0.0f);
    frame.exposure = glm::vec4(s.exposure, 0.0f, 0.0f, 0.0f);
    return frame;
}

void attach_input(GLFWwindow* window, InputContext& input) {
    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetScrollCallback(window, scroll_callback);
}
} // namespace disney
