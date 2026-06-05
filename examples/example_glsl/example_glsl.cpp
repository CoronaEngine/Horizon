#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <tiny_obj_loader.h>

#include <horizon.h>
#include "hardware_wrapper_vulkan/hardware/execution.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <span>
#include <unordered_map>
#include <vector>

#include <stb_image.h>

using namespace Corona::Horizon;

namespace
{

constexpr uint32_t baseline_width = 1920;
constexpr uint32_t baseline_height = 1080;
constexpr float pi = 3.14159265358979323846f;

const std::filesystem::path viking_room_model_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "models" / "viking_room.obj";
const std::filesystem::path viking_room_texture_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "textures" / "viking_room.png";

constexpr char baseline_model_vertex_shader[] = R"GLSL(
#version 460

layout(push_constant) uniform TransformPushConstant {
    mat4 model;
    mat4 view;
    mat4 proj;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main()
{
    gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
)GLSL";

constexpr char baseline_model_fragment_shader[] = R"GLSL(
#version 460

layout(set = 0, binding = 1) uniform sampler2D texSampler;
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(texSampler, fragTexCoord);
}
)GLSL";

constexpr BindingSlot model_binding{0, 64, 0, 0};
constexpr BindingSlot view_binding{64, 64, 0, 0};
constexpr BindingSlot proj_binding{128, 64, 0, 0};
constexpr BindingSlot texture_image_binding{0, 0, 4, 1, 0, 1};
constexpr BindingSlot output_binding{0, 16, 2, 0};

struct vec3
{
    float x;
    float y;
    float z;
};

struct mat4
{
    std::array<float, 16> value{};

    float &operator()(int row, int col)
    {
        return value[static_cast<size_t>(col * 4 + row)];
    }

    float operator()(int row, int col) const
    {
        return value[static_cast<size_t>(col * 4 + row)];
    }
};

struct baseline_vertex
{
    std::array<float, 3> pos{};
    std::array<float, 3> color{};
    std::array<float, 2> tex_coord{};

    bool operator==(const baseline_vertex &other) const
    {
        return pos == other.pos && color == other.color && tex_coord == other.tex_coord;
    }
};

struct baseline_vertex_hash
{
    size_t operator()(const baseline_vertex &vertex) const
    {
        size_t seed = 0;
        auto combine = [&seed](float value) {
            seed ^= std::hash<float>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        for (float value : vertex.pos)
        {
            combine(value);
        }
        for (float value : vertex.color)
        {
            combine(value);
        }
        for (float value : vertex.tex_coord)
        {
            combine(value);
        }
        return seed;
    }
};

struct uniform_buffer_object
{
    alignas(16) mat4 model;
    alignas(16) mat4 view;
    alignas(16) mat4 proj;
};

struct baseline_mesh
{
    std::vector<baseline_vertex> vertices;
    std::vector<uint32_t> indices;
};

mat4 identity()
{
    mat4 result{};
    result(0, 0) = 1.0f;
    result(1, 1) = 1.0f;
    result(2, 2) = 1.0f;
    result(3, 3) = 1.0f;
    return result;
}

mat4 rotate_z(float radians)
{
    mat4 result = identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    result(0, 0) = c;
    result(0, 1) = -s;
    result(1, 0) = s;
    result(1, 1) = c;
    return result;
}

vec3 operator-(const vec3 &lhs, const vec3 &rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

float dot(const vec3 &lhs, const vec3 &rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

vec3 cross(const vec3 &lhs, const vec3 &rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

vec3 normalize(const vec3 &value)
{
    float length = std::sqrt(dot(value, value));
    return {value.x / length, value.y / length, value.z / length};
}

mat4 look_at_rh(const vec3 &eye, const vec3 &center, const vec3 &up)
{
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);

    mat4 result = identity();
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

mat4 perspective_rh(float fovy_radians, float aspect, float near_plane, float far_plane)
{
    float f = 1.0f / std::tan(fovy_radians / 2.0f);
    mat4 result{};
    result(0, 0) = f / aspect;
    result(1, 1) = -f;
    result(2, 2) = far_plane / (near_plane - far_plane);
    result(3, 2) = -1.0f;
    result(2, 3) = (far_plane * near_plane) / (near_plane - far_plane);
    return result;
}

bool check_assets()
{
    bool model_exists = std::filesystem::exists(viking_room_model_path);
    bool texture_exists = std::filesystem::exists(viking_room_texture_path);
    if (model_exists && texture_exists)
    {
        return true;
    }

    std::cerr << "GLSL model baseline assets are missing.\n";
    if (!model_exists)
    {
        std::cerr << "  Missing model: " << viking_room_model_path << '\n';
    }
    if (!texture_exists)
    {
        std::cerr << "  Missing texture: " << viking_room_texture_path << '\n';
    }
    return false;
}

baseline_mesh load_mesh()
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
    {
        throw std::runtime_error(warn + err);
    }
    if (!warn.empty())
    {
        std::cerr << warn << '\n';
    }

    baseline_mesh mesh;
    std::unordered_map<baseline_vertex, uint32_t, baseline_vertex_hash> unique_vertices;
    for (const auto &shape : shapes)
    {
        for (const auto &index : shape.mesh.indices)
        {
            baseline_vertex vertex{};
            vertex.pos = {
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 0],
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 1],
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 2],
            };
            vertex.color = {1.0f, 1.0f, 1.0f};

            if (index.texcoord_index >= 0)
            {
                vertex.tex_coord = {
                    attrib.texcoords[2 * static_cast<size_t>(index.texcoord_index) + 0],
                    1.0f - attrib.texcoords[2 * static_cast<size_t>(index.texcoord_index) + 1],
                };
            }

            if (!unique_vertices.contains(vertex))
            {
                unique_vertices[vertex] = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
            }
            mesh.indices.push_back(unique_vertices[vertex]);
        }
    }

    return mesh;
}

HardwareImage load_texture_image()
{
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::string texture_path = viking_room_texture_path.string();
    stbi_uc *pixels = stbi_load(texture_path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        throw std::runtime_error("failed to load texture image: " + texture_path);
    }

    HardwareImageDesc create_info =
        HardwareImageDesc::texture_2d(static_cast<uint32_t>(width),
                                      static_cast<uint32_t>(height),
                                      Format::SRGBA8_UNORM,
                                      ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst);
    HardwareImage texture_image(create_info);
    const auto pixel_bytes =
        std::as_bytes(std::span<const stbi_uc>(pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4u));
    HardwareBufferDesc staging_desc;
    staging_desc.element_count = pixel_bytes.size_bytes();
    staging_desc.element_size = 1;
    staging_desc.usage = BufferUsageFlags::TransferSrc;
    staging_desc.cpu_access = CpuAccessMode::Write;
    HardwareBuffer staging(staging_desc, pixel_bytes);
    HardwareExecutor upload_executor;
    auto upload_receipt = upload_executor.stream()
        << texture_image.copy_from(staging)
        << commit();
    (void)upload_receipt;
    stbi_image_free(pixels);

    return texture_image;
}

HardwareImage create_render_target(uint32_t width, uint32_t height)
{
    HardwareImageDesc create_info =
        HardwareImageDesc::texture_2d(width,
                                      height,
                                      Format::SBGRA8_UNORM,
                                      ImageUsageFlags::ColorAttachment | ImageUsageFlags::Sampled | ImageUsageFlags::TransferSrc |
                                          ImageUsageFlags::TransferDst);

    HardwareImage image(create_info);
    image.set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    return image;
}

HardwareImage create_depth_target(uint32_t width, uint32_t height)
{
    HardwareImage image(HardwareImageDesc::depth_attachment(width, height, Format::D32, "example_glsl.depth"));
    image.set_clear_depth(1.0f, 0);
    return image;
}

float example_time_seconds(std::chrono::high_resolution_clock::time_point start_time)
{
    if (const char *fixed_time = std::getenv("HORIZON_EXAMPLE_FIXED_TIME_SECONDS"))
    {
        char *end = nullptr;
        const float value = std::strtof(fixed_time, &end);
        if (end != fixed_time)
        {
            return value;
        }
    }

    return std::chrono::duration<float, std::chrono::seconds::period>(
               std::chrono::high_resolution_clock::now() - start_time)
        .count();
}

uniform_buffer_object make_ubo(float time_seconds)
{
    uniform_buffer_object ubo{};
    ubo.model = rotate_z(time_seconds * pi * 0.5f);
    ubo.view = look_at_rh({2.0f, 2.0f, 2.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    ubo.proj = perspective_rh(pi * 0.25f,
                              baseline_width / static_cast<float>(baseline_height),
                              0.1f,
                              10.0f);
    return ubo;
}

} // namespace

void run_example_glsl()
{
    if (!check_assets())
    {
        return;
    }

    if (glfwInit() < 0)
    {
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(baseline_width, baseline_height, "Horizon Baseline [Backend GLSL]", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return;
    }

    try
    {
        baseline_mesh mesh = load_mesh();
        HardwareBuffer vertex_buffer = HardwareBuffer::vertex(mesh.vertices);
        HardwareBuffer index_buffer = HardwareBuffer::index(mesh.indices);
        HardwareImage texture_image = load_texture_image();
        HardwareImage render_target = create_render_target(baseline_width, baseline_height);
        HardwareImage depth_target = create_depth_target(baseline_width, baseline_height);
        HardwareExecutor render_executor;
        HardwareDisplayer displayer(glfwGetWin32Window(window));

        EmbeddedShader::CompilerOption glsl_compiler_options;
        glsl_compiler_options.enableBindless = false;
        glsl_compiler_options.compileGLSL = false;
        glsl_compiler_options.compileHLSL = false;
        glsl_compiler_options.compileDXIL = false;
        glsl_compiler_options.compileDXBC = false;
        RasterizerPipelineDesc rasterizer_desc =
            RasterizerPipelineDesc::from_source(baseline_model_vertex_shader,
                                                baseline_model_fragment_shader,
                                                EmbeddedShader::ShaderLanguage::GLSL,
                                                EmbeddedShader::ShaderLanguage::GLSL,
                                                glsl_compiler_options);
        DepthStencilStateDesc baseline_depth;
        baseline_depth.depth_compare_op = CompareOp::Less;
        rasterizer_desc.set_depth_stencil(baseline_depth);
        rasterizer_desc.set_depth_attachment(DepthAttachmentDesc::with_format(Format::D32, "example_glsl.depth"));
        BlendStateDesc baseline_blend;
        baseline_blend.set_opaque();
        rasterizer_desc.set_blend(baseline_blend);
        RasterizerPipeline rasterizer(rasterizer_desc);
        rasterizer[output_binding] = render_target;
        rasterizer[texture_image_binding] = texture_image;
        rasterizer.bind_depth_target(depth_target);

        auto start_time = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            if (glfwWindowShouldClose(window))
            {
                break;
            }

            float time_seconds = example_time_seconds(start_time);
            uniform_buffer_object ubo = make_ubo(time_seconds);
            rasterizer[model_binding] = ubo.model;
            rasterizer[view_binding] = ubo.view;
            rasterizer[proj_binding] = ubo.proj;

            DrawIndexedParams draw_params;
            draw_params.index_type = IndexType::UInt32;
            draw_params.index_count = static_cast<uint32_t>(mesh.indices.size());

            rasterizer.clear_records();
            rasterizer(baseline_width, baseline_height);
            rasterizer.record(index_buffer, vertex_buffer, draw_params);
            auto receipt = render_executor.stream()
                << rasterizer.command_batch()
                << present(displayer, render_target)
                << commit();
            (void)receipt;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
