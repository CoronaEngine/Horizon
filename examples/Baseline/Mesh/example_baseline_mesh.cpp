#include "BaselineMesh.h"
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

BaselineMesh::BaselineMesh(void* native_window)
    : ExampleBase(native_window), display(native_window),
      color(800, 600, HardwareImage::Format::RGBA16_FLOAT),
      depth(800, 600, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage),
      pipeline({.compare = RasterPipeline<>::Compare::LessEqual})
{
    const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
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
    HardwareBuffer vertexBuffer, indexBuffer;
    executor << vertexBuffer.createDeviceLocal(HardwareBuffer::Usage::StorageBuffer, vertices.data(), vertices.size(), sizeof(Vertex))
             << indexBuffer.createDeviceLocal(HardwareBuffer::Usage::StorageBuffer, indices.data(), indices.size(), sizeof(uint32_t));

    const auto texture_path = (assets / "textures/viking_room.png").string();
    int width = 0, height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> texels(
        stbi_load(texture_path.c_str(), &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    HardwareImage::CreateInfo texture_info(width, height, HardwareImage::Format::RGBA8_SRGB);
    texture_info.linearFiltering = true;
    HardwareImage texture(texture_info);
    executor << texture.copyFrom(texels.get());
    const auto triangles = static_cast<uint32_t>(indexBuffer.getElementCount() / 3);
    const MeshPipeline<>::IndirectPacket draw{(triangles + 31) / 32, 1, 1};
    executor << groups.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect, &draw, 1, sizeof(draw));

    pipeline.geometry[0] = vertexBuffer;
    pipeline.geometry[1] = indexBuffer;
    pipeline.vp.texIndex = texture.storeSampledDescriptor();
    constexpr float near_plane = 0.1f, far_plane = 10.0f;
    const float top = near_plane * ktm::tan(ktm::radians(45.0f) * 0.5f);
    const float right = top * (800.0f / 600.0f);
    auto projection = ktm::frustum_rh(-right, right, top, -top, near_plane, far_plane);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    pipeline.vp.proj = projection;
    pipeline.outColor = color;
}

void BaselineMesh::run(float seconds)
{
    pipeline.vp.view = view * ktm::rotate3d_z(seconds * ktm::radians(90.0f));
    executor << pipeline(groups, 1, depth) << display(color);
    executor.commit();
}
