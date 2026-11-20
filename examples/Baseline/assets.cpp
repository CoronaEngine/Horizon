#include "assets.h"
#include "Horizon.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <tiny_obj_loader.h>

#include <array>
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

static_assert(sizeof(Vertex) == 5 * sizeof(float));

size_t hash_vertex(const Vertex& vertex) {
    size_t seed = 0;
    auto combine = [&seed](float value) {
        seed ^= std::hash<float>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };
    for (float value : vertex.pos) combine(value);
    for (float value : vertex.tex_coord) combine(value);
    return seed;
}
} // namespace

ModelData read_model(const char* asset_name)
{
    const auto model_path = assets / asset_name;
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
    std::unordered_map<Vertex, uint32_t, decltype(&hash_vertex)> unique_vertices(0, hash_vertex);
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

    return {std::move(vertices), std::move(indices)};
}

RasterGeometry load_model(const char* asset_name) {
    const auto mesh = read_model(asset_name);
    return {HardwareBuffer::uploadDeviceLocal(mesh.vertices, BufferUsage::VertexBuffer),
            HardwareBuffer::uploadDeviceLocal(mesh.indices, BufferUsage::IndexBuffer)};
}

TextureData read_texture(const char* asset_name)
{
    const auto filename = (assets / asset_name).string();
    int width = 0;
    int height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data(
        stbi_load(filename.c_str(), &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    if (!data)
        throw std::runtime_error("Failed to load texture " + filename + ": " + stbi_failure_reason());

    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    return {width, height, {data.get(), data.get() + byteCount}};
}

HardwareImage load_texture(const char* asset_name) {
    const auto data = read_texture(asset_name);
    HardwareImageCreateInfo info(data.width, data.height, ImageFormat::RGBA8_SRGB);
    info.linearFiltering = true;
    HardwareImage texture(info);
    HardwareBuffer staging(static_cast<uint32_t>(data.rgba.size()), BufferUsage::StorageBuffer, data.rgba.data());
    HardwareExecutor upload;
    upload << staging.copyTo(texture);
    upload.commit().waitForDeferredResources();
    return texture;
}

} // namespace baseline
