#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <tiny_obj_loader.h>

#include <Codegen/BuiltinVariate.h>
#include <Codegen/CustomLibrary.h>
#include <Codegen/TypeAlias.h>
#include <Horizon.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <stb_image.h>

namespace
{

constexpr uint32_t baseline_width = 800;
constexpr uint32_t baseline_height = 600;
constexpr float pi = 3.14159265358979323846f;

const std::filesystem::path viking_room_model_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "models" / "viking_room.obj";
const std::filesystem::path viking_room_texture_path =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "textures" / "viking_room.png";

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

struct baseline_vertex_proxy
{
    EmbeddedShader::Float3 pos;
    EmbeddedShader::Float3 color;
    EmbeddedShader::Float2 tex_coord;
};

struct fragment_input_proxy
{
    EmbeddedShader::Float3 color;
    EmbeddedShader::Float2 tex_coord;
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

mat4 multiply(const mat4 &lhs, const mat4 &rhs)
{
    mat4 result{};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            for (int k = 0; k < 4; ++k)
            {
                result(row, col) += lhs(row, k) * rhs(k, col);
            }
        }
    }
    return result;
}

std::array<float, 4> multiply(const mat4 &lhs, const std::array<float, 4> &rhs)
{
    std::array<float, 4> result{};
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            result[static_cast<size_t>(row)] += lhs(row, col) * rhs[static_cast<size_t>(col)];
        }
    }
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

    std::cerr << "EDSL model baseline assets are missing.\n";
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

    HardwareImageCreateInfo create_info;
    create_info.width = static_cast<uint32_t>(width);
    create_info.height = static_cast<uint32_t>(height);
    create_info.format = ImageFormat::RGBA8_SRGB;
    create_info.usage = ImageUsage::SampledImage;
    create_info.arrayLayers = 1;
    create_info.mipLevels = 1;

    HardwareImage texture_image(create_info);
    HardwareExecutor upload_executor;
    upload_executor << texture_image.copyFrom(pixels) << upload_executor.commit();
    stbi_image_free(pixels);

    return texture_image;
}

HardwareImage create_render_target(uint32_t width, uint32_t height)
{
    HardwareImageCreateInfo create_info;
    create_info.width = width;
    create_info.height = height;
    create_info.format = ImageFormat::RGBA16_FLOAT;
    create_info.usage = ImageUsage::StorageImage;
    create_info.arrayLayers = 1;
    create_info.mipLevels = 1;

    HardwareImage image(create_info);
    image.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    return image;
}

mat4 make_mvp(float time_seconds)
{
    mat4 model = rotate_z(time_seconds * pi * 0.5f);
    mat4 view = look_at_rh({2.0f, 2.0f, 2.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    mat4 proj = perspective_rh(pi * 0.25f,
                               baseline_width / static_cast<float>(baseline_height),
                               0.1f,
                               10.0f);
    return multiply(proj, multiply(view, model));
}

std::vector<baseline_vertex> transform_vertices(const std::vector<baseline_vertex> &vertices, const mat4 &mvp)
{
    std::vector<baseline_vertex> transformed = vertices;
    for (auto &vertex : transformed)
    {
        std::array<float, 4> clip = multiply(mvp, {vertex.pos[0], vertex.pos[1], vertex.pos[2], 1.0f});
        const float inv_w = 1.0f / clip[3];
        vertex.pos = {clip[0] * inv_w, clip[1] * inv_w, clip[2] * inv_w};
    }
    return transformed;
}

} // namespace

void run_example_baseline_edsl()
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
    GLFWwindow *window = glfwCreateWindow(baseline_width, baseline_height, "Horizon Baseline [Backend EDSL]", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return;
    }

    try
    {
        baseline_mesh mesh = load_mesh();
        HardwareBuffer index_buffer(mesh.indices, BufferUsage::IndexBuffer);
        HardwareImage texture_image = load_texture_image();
        HardwareImage render_target = create_render_target(baseline_width, baseline_height);
        HardwareExecutor render_executor;
        HardwareDisplayer displayer(glfwGetWin32Window(window));

        using namespace EmbeddedShader;

        Texture2D<ktm::fvec4> texture_proxy = texture_image;

        auto vertex_shader = [](Aggregate<baseline_vertex_proxy> vertex) -> Aggregate<fragment_input_proxy>
        {
            position() = Float4(vertex->pos, 1.0f);
            Aggregate<fragment_input_proxy> output;
            output.color = vertex->color;
            output.tex_coord = vertex->tex_coord;
            return output;
        };

        auto fragment_shader = [&](Aggregate<fragment_input_proxy> input) -> Float4
        {
            return texture(texture_proxy, input->tex_coord) * Float4(input->color, 1.0f);
        };

        RasterizerPipeline rasterizer(vertex_shader, fragment_shader);
        // Input texture is sampled by the fragment shader; it must not be registered as a render target.
        texture_proxy = texture_image;

        Texture2D<ktm::fvec4> output_proxy = render_target;
        rasterizer.bindOutputTargets(output_proxy);

        auto start_time = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();

            float time_seconds = std::chrono::duration<float, std::chrono::seconds::period>(
                                     std::chrono::high_resolution_clock::now() - start_time)
                                     .count();
            std::vector<baseline_vertex> transformed_vertices = transform_vertices(mesh.vertices, make_mvp(time_seconds));
            HardwareBuffer vertex_buffer(transformed_vertices, BufferUsage::VertexBuffer);

            DrawIndexedParams draw_params;
            draw_params.indexType = IndexType::UInt32;
            draw_params.indexCount = static_cast<uint32_t>(mesh.indices.size());

            rasterizer.record(index_buffer, vertex_buffer, draw_params);
            render_executor << rasterizer(baseline_width, baseline_height)
                            << render_executor.commit();
            displayer.wait(render_executor) << render_target;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
