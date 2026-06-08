#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#include <tiny_obj_loader.h>

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include "common.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "horizon.h"
#include GLSL(shaders/edsl_header.glsl)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace H = Corona::Horizon;

struct BaselineEdslVertexProxy
{
    EmbeddedShader::Float3 pos;
    EmbeddedShader::Float3 color;
    EmbeddedShader::Float2 tex_coord;
};

namespace
{
constexpr uint32_t edsl_width = 800;
constexpr uint32_t edsl_height = 600;
constexpr float pi = 3.14159265358979323846f;

constexpr EmbeddedShader::BindingKey model_binding { 0, 64, 10, 0 };
constexpr EmbeddedShader::BindingKey view_binding { 64, 64, 10, 0 };
constexpr EmbeddedShader::BindingKey proj_binding { 128, 64, 10, 0 };

struct ReflectedBindingSlot
{
    uint64_t byte_offset = 0;
    uint32_t type_size = 0;
    int32_t bind_type = -1;
    uint32_t location = 0;
    uint32_t set = 0;
    uint32_t binding = 0;
};

constexpr ReflectedBindingSlot texture_binding {
    0,
    0,
    static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::sampledImages),
    0,
    1,
    0,
};

const std::filesystem::path viking_room_model_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "models" / "viking_room.obj";
const std::filesystem::path viking_room_texture_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "textures" / "viking_room.png";

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat4
{
    std::array<float, 16> value {};

    float& operator()(int row, int col)
    {
        return value[static_cast<size_t>(col * 4 + row)];
    }

    float operator()(int row, int col) const
    {
        return value[static_cast<size_t>(col * 4 + row)];
    }
};

struct BaselineEdslVertex
{
    std::array<float, 3> pos {};
    std::array<float, 3> color {};
    std::array<float, 2> tex_coord {};

    bool operator==(const BaselineEdslVertex& other) const
    {
        return pos == other.pos && color == other.color && tex_coord == other.tex_coord;
    }
};

struct BaselineEdslVertexHash
{
    size_t operator()(const BaselineEdslVertex& vertex) const
    {
        size_t seed = 0;
        auto combine = [&seed](float value) {
            seed ^= std::hash<float> {}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };

        for (float value : vertex.pos)
            combine(value);
        for (float value : vertex.color)
            combine(value);
        for (float value : vertex.tex_coord)
            combine(value);

        return seed;
    }
};

struct BaselineEdslMesh
{
    std::vector<BaselineEdslVertex> vertices;
    std::vector<uint32_t> indices;
};

struct UniformBufferObject
{
    alignas(16) Mat4 model;
    alignas(16) Mat4 view;
    alignas(16) Mat4 proj;
};

Mat4 identity()
{
    Mat4 result;
    result(0, 0) = 1.0f;
    result(1, 1) = 1.0f;
    result(2, 2) = 1.0f;
    result(3, 3) = 1.0f;
    return result;
}

Mat4 rotate_z(float radians)
{
    Mat4 result = identity();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result(0, 0) = c;
    result(0, 1) = -s;
    result(1, 0) = s;
    result(1, 1) = c;
    return result;
}

Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
{
    return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

float dot(const Vec3& lhs, const Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 cross(const Vec3& lhs, const Vec3& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Vec3 normalize(const Vec3& value)
{
    const float length = std::sqrt(dot(value, value));
    return { value.x / length, value.y / length, value.z / length };
}

Mat4 look_at_rh(const Vec3& eye, const Vec3& center, const Vec3& up)
{
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    Mat4 result = identity();
    result(0, 0) = s.x;
    result(0, 1) = s.y;
    result(0, 2) = s.z;
    result(1, 0) = u.x;
    result(1, 1) = u.y;
    result(1, 2) = u.z;
    result(2, 0) = -f.x;
    result(2, 1) = -f.y;
    result(2, 2) = -f.z;
    result(0, 3) = -dot(s, eye);
    result(1, 3) = -dot(u, eye);
    result(2, 3) = dot(f, eye);
    return result;
}

Mat4 perspective_rh(float fovy_radians, float aspect, float near_plane, float far_plane)
{
    const float f = 1.0f / std::tan(fovy_radians / 2.0f);
    Mat4 result;
    result(0, 0) = f / aspect;
    result(1, 1) = -f;
    result(2, 2) = far_plane / (near_plane - far_plane);
    result(3, 2) = -1.0f;
    result(2, 3) = (far_plane * near_plane) / (near_plane - far_plane);
    return result;
}

Mat4 transpose(const Mat4& matrix)
{
    Mat4 result;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
            result(row, col) = matrix(col, row);
    }
    return result;
}

void check_assets()
{
    if (std::filesystem::exists(viking_room_model_path) && std::filesystem::exists(viking_room_texture_path))
        return;

    std::string message = "EDSL baseline assets are missing.";
    if (!std::filesystem::exists(viking_room_model_path))
        message += "\n  Missing model: " + viking_room_model_path.string();
    if (!std::filesystem::exists(viking_room_texture_path))
        message += "\n  Missing texture: " + viking_room_texture_path.string();
    throw std::runtime_error(message);
}

BaselineEdslMesh load_mesh()
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    const std::string model_path = viking_room_model_path.string();
    const std::string material_base_path =
        viking_room_model_path.parent_path().string() + std::string(1, std::filesystem::path::preferred_separator);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model_path.c_str(), material_base_path.c_str()))
        throw std::runtime_error(warn + err);
    if (!warn.empty())
        std::cerr << warn << '\n';

    BaselineEdslMesh mesh;
    std::unordered_map<BaselineEdslVertex, uint32_t, BaselineEdslVertexHash> unique_vertices;
    for (const tinyobj::shape_t& shape : shapes)
    {
        for (const tinyobj::index_t& index : shape.mesh.indices)
        {
            BaselineEdslVertex vertex;
            vertex.pos = {
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 0],
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 1],
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 2],
            };
            vertex.color = { 1.0f, 1.0f, 1.0f };

            if (index.texcoord_index >= 0)
            {
                vertex.tex_coord = {
                    attrib.texcoords[2 * static_cast<size_t>(index.texcoord_index) + 0],
                    1.0f - attrib.texcoords[2 * static_cast<size_t>(index.texcoord_index) + 1],
                };
            }

            auto [found, inserted] = unique_vertices.emplace(vertex, static_cast<uint32_t>(mesh.vertices.size()));
            if (inserted)
                mesh.vertices.push_back(vertex);
            mesh.indices.push_back(found->second);
        }
    }

    return mesh;
}

UniformBufferObject make_ubo(float time_seconds)
{
    UniformBufferObject ubo;
    ubo.model = transpose(rotate_z(time_seconds * pi * 0.5f));
    ubo.view = transpose(look_at_rh({ 2.0f, 2.0f, 2.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }));
    ubo.proj = transpose(perspective_rh(pi * 0.25f, edsl_width / static_cast<float>(edsl_height), 0.1f, 10.0f));
    return ubo;
}

H::HardwareImage create_output_image()
{
    H::HardwareImage image(
        H::HardwareImageDesc::texture_2d(edsl_width,
                                         edsl_height,
                                         H::Format::RGBA16_FLOAT,
                                         H::ImageUsageFlags::Storage | H::ImageUsageFlags::ColorAttachment |
                                             H::ImageUsageFlags::Sampled | H::ImageUsageFlags::TransferSrc |
                                             H::ImageUsageFlags::TransferDst,
                                         "example_edsl.output"));
    image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    return image;
}

H::HardwareImage create_depth_image()
{
    H::HardwareImage image(
        H::HardwareImageDesc::depth_attachment(edsl_width, edsl_height, H::Format::D32, "example_edsl.depth"));
    image.set_clear_depth(1.0f, 0);
    return image;
}

H::Queue& resolve_fixed_graphics_queue(H::DeviceId device, H::QueueCapability)
{
    if (device.value != 0)
        throw std::logic_error("example_edsl fixed queue resolver supports only the main device.");

    const std::vector<H::Queue*>& queues = H::device_manager().queues_for(H::QueueCapability::Graphics);
    const auto found = std::find_if(queues.begin(), queues.end(), [](const H::Queue* queue) {
        return queue != nullptr;
    });
    if (found == queues.end())
        throw std::runtime_error("example_edsl could not resolve a graphics queue.");

    return **found;
}
} // namespace

void run_example_edsl()
{
    check_assets();

    if (glfwInit() < 0)
        return;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(edsl_width, edsl_height, "Horizon Baseline [EDSL]", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return;
    }

    try
    {
        BaselineEdslMesh mesh = load_mesh();
        TextureLoadResult texture_result = loadTexture(viking_room_texture_path.string());
        if (!texture_result.success)
            throw std::runtime_error("failed to load EDSL baseline texture: " + viking_room_texture_path.string());

        H::HardwareImage texture_image = texture_result.texture;
        H::HardwareImage final_output_image = create_output_image();
        H::HardwareImage depth_image = create_depth_image();
        H::HardwareBuffer vertex_buffer = H::HardwareBuffer::vertex(mesh.vertices, "example_edsl.vertex");
        H::HardwareBuffer index_buffer = H::HardwareBuffer::index(mesh.indices, "example_edsl.index");
        H::HardwareExecutor render_executor(resolve_fixed_graphics_queue);
        H::HardwareExecutor display_executor;
        H::HardwareDisplayer display(glfwGetWin32Window(window));

        using namespace EmbeddedShader;
        using namespace ktm;

        Texture2D<fvec4> texture_proxy = texture_image;
        Float4x4 model;
        Float4x4 view;
        Float4x4 proj;

        auto vertex_shader = [&](Aggregate<BaselineEdslVertexProxy> vertex) -> Float4 {
            position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
            Float color_weight = edsl_header_glsl::get_color_weight(vertex->color);
            return Float4(vertex->tex_coord, color_weight, 1.0f);
        };

        auto fragment_shader = [&](Float4 input) -> Float4 {
            Float4 color = texture(texture_proxy, input->xy());
            return color * Float4(input->z, input->z, input->z, 1.0f);
        };

        H::RasterizerPipelineDesc rasterizer_desc =
            H::RasterizerPipelineDesc::from_edsl(vertex_shader, fragment_shader);
        rasterizer_desc.set_depth_attachment(H::DepthAttachmentDesc::with_format(H::Format::D32, "example_edsl.depth"));
        rasterizer_desc.set_debug_name("example_edsl.baseline_rasterizer");

        H::RasterizerPipeline rasterizer(std::move(rasterizer_desc));
        rasterizer.bind_render_target(0, final_output_image);
        rasterizer.bind_depth_target(depth_image);
        rasterizer[texture_binding] = texture_image;

        H::DrawIndexedParams draw_params;
        draw_params.index_type = H::IndexType::UInt32;
        draw_params.index_count = static_cast<uint32_t>(mesh.indices.size());

        const auto start_time = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            const float time_seconds =
                std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - start_time)
                    .count();
            UniformBufferObject ubo = make_ubo(time_seconds);
            rasterizer[model_binding] = ubo.model;
            rasterizer[view_binding] = ubo.view;
            rasterizer[proj_binding] = ubo.proj;

            rasterizer.clear_records();
            rasterizer.record(index_buffer, vertex_buffer, draw_params);

            H::SubmitReceipt render_receipt = render_executor.stream()
                << rasterizer(edsl_width, edsl_height).command_batch()
                << H::commit();

            display_executor.wait(render_receipt);
            (void)(display_executor.stream()
                << H::present(display, final_output_image)
                << H::commit());
        }
    }
    catch (...)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        throw;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
