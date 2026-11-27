#include "BaselineAssets.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#include <tiny_obj_loader.h>

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

BaselineAssets loadBaselineAssets(HardwareExecutor& executor, HardwareBuffer::Usage vertexUsage,
                                  HardwareBuffer::Usage indexUsage)
{
    const auto assets = std::filesystem::path(__FILE__).parent_path() / "assets";
    const auto filename = (assets / "models/viking_room.obj").string();
    const auto material_dir = (assets / "models/").string();
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, filename.c_str(), material_dir.c_str());

    using Vertex = std::array<float, 5>; // xyz, uv
    std::map<Vertex, uint32_t> unique_vertices;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            const size_t p = 3 * static_cast<size_t>(index.vertex_index);
            Vertex vertex{attrib.vertices[p], attrib.vertices[p + 1], attrib.vertices[p + 2]};
            if (index.texcoord_index >= 0) {
                const size_t uv = 2 * static_cast<size_t>(index.texcoord_index);
                vertex[3] = attrib.texcoords[uv];
                vertex[4] = 1.0f - attrib.texcoords[uv + 1];
            }
            const auto [found, inserted] = unique_vertices.emplace(vertex, static_cast<uint32_t>(vertices.size()));
            if (inserted) vertices.push_back(vertex);
            indices.push_back(found->second);
        }
    }
    BaselineAssets model;
    executor << model.vertices.createDeviceLocal(vertexUsage, vertices.data(), vertices.size(), sizeof(Vertex))
             << model.indices.createDeviceLocal(indexUsage, indices.data(), indices.size(), sizeof(uint32_t));

    const auto texture_path = (assets / "textures/viking_room.png").string();
    int width = 0, height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> texels(
        stbi_load(texture_path.c_str(), &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    HardwareImage::CreateInfo texture_info(width, height, HardwareImage::Format::RGBA8_SRGB);
    texture_info.linearFiltering = true;
    model.texture = HardwareImage(texture_info);
    executor << model.texture.copyFrom(texels.get());
    return model;
}

ktm::fmat4x4 baselineProjection()
{
    constexpr float near_plane = 0.1f, far_plane = 10.0f;
    const float top = near_plane * ktm::tan(ktm::radians(45.0f) * 0.5f);
    const float right = top * (800.0f / 600.0f);
    auto projection = ktm::frustum_rh(-right, right, top, -top, near_plane, far_plane);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    return projection;
}
