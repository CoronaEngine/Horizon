#include "scene.h"
#include "horizon/core/logging.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <tiny_obj_loader.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace baseline {
namespace {
const auto assets = std::filesystem::path(__FILE__).parent_path() / "assets";

struct Vertex
{
    std::array<float, 3> pos {};
    std::array<float, 2> tex_coord {};

    bool operator==(const Vertex&) const = default;
};
static_assert(sizeof(Vertex) == 5 * sizeof(float));

struct VertexHash
{
    size_t operator()(const Vertex& vertex) const
    {
        size_t seed = 0;
        auto combine = [&seed](float value) {
            seed ^= std::hash<float> {}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };

        for (float value : vertex.pos)
            combine(value);
        for (float value : vertex.tex_coord)
            combine(value);

        return seed;
    }
};

RasterGeometry load_geometry(const std::filesystem::path& model_path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    const std::string model_path_str = model_path.string();
    const std::string material_base_path =
        model_path.parent_path().string() + std::string(1, std::filesystem::path::preferred_separator);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, model_path_str.c_str(), material_base_path.c_str()))
        throw std::runtime_error(warn + err);
    if (!warn.empty())
        std::cerr << warn << '\n';

    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::unordered_map<Vertex, uint32_t, VertexHash> unique_vertices;
    for (const tinyobj::shape_t& shape : shapes)
    {
        for (const tinyobj::index_t& index : shape.mesh.indices)
        {
            Vertex vertex;
            vertex.pos = {
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 0],
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 1],
                attrib.vertices[3 * static_cast<size_t>(index.vertex_index) + 2],
            };
            if (index.texcoord_index >= 0)
            {
                vertex.tex_coord = {
                    attrib.texcoords[2 * static_cast<size_t>(index.texcoord_index) + 0],
                    1.0f - attrib.texcoords[2 * static_cast<size_t>(index.texcoord_index) + 1],
                };
            }

            auto [found, inserted] = unique_vertices.emplace(vertex, static_cast<uint32_t>(vertices.size()));
            if (inserted)
            {
                if (vertices.size() >= 0x10000u)
                    throw std::runtime_error("Mesh exceeds the 65536-vertex 16-bit index limit: " + model_path_str);
                vertices.push_back(vertex);
            }
            indices.push_back(static_cast<uint16_t>(found->second));
        }
    }

    return {HardwareBuffer::uploadDeviceLocal(vertices, BufferUsage::VertexBuffer),
            HardwareBuffer::uploadDeviceLocal(indices, BufferUsage::IndexBuffer)};
}

HardwareImage make_filtered_image(uint32_t width, uint32_t height, ImageFormat format, ImageUsage usage)
{
    HardwareImageCreateInfo info(width, height, format);
    info.usage = usage;
    info.linearFiltering = true;
    return HardwareImage(info);
}

HardwareImage load_texture(const std::filesystem::path& path)
{
    const auto filename = path.string();
    int width = 0;
    int height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data(
        stbi_load(filename.c_str(), &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    if (!data)
    {
        CFW_LOG_ERROR("Failed to load texture '{}': {}", filename, stbi_failure_reason());
        return {};
    }

    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    auto texture = make_filtered_image(width, height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage);
    HardwareBuffer staging(static_cast<uint32_t>(byteCount), BufferUsage::StorageBuffer, data.get());
    HardwareExecutor upload;
    upload << staging.copyTo(texture);
    upload.commit().waitForDeferredResources();
    return texture;
}

} // namespace

Scene::Scene()
    : geometry(load_geometry(assets / "models/viking_room.obj")),
      texture(load_texture(assets / "textures/viking_room.png")),
      color(make_filtered_image(width, height, ImageFormat::RGBA16_FLOAT, ImageUsage::SampledStorageImage)),
      depth(make_filtered_image(width, height, ImageFormat::D32_FLOAT, ImageUsage::DepthImage)),
      view(glm::lookAt(glm::vec3(2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f))),
      projection(glm::perspective(glm::pi<float>() * 0.25f, width / static_cast<float>(height), 0.1f, 10.0f))
{
    color.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y
}

glm::mat4 Animation::next_model(GLFWwindow* window, const char* title) {
    const auto now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - title_updated_).count();
    ++frames_;
    if (elapsed >= 0.5) {
        const double fps = frames_ / elapsed;
        char text[160];
        std::snprintf(text, sizeof(text), "%s %.1f FPS (%.2f ms)", title, fps, 1000.0 / fps);
        glfwSetWindowTitle(window, text);
        title_updated_ = now;
        frames_ = 0;
    }
    const float time = std::chrono::duration<float>(now - start_).count();
    return glm::rotate(glm::mat4(1.0f), time * glm::pi<float>() * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f));
}
} // namespace baseline
