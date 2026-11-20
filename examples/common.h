#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#include <tiny_obj_loader.h>

#include "Horizon.h"

namespace baseline
{
HardwareImage loadTexture(const std::string& path);

inline HardwareImageCreateInfo imageInfo(uint32_t width, uint32_t height, ImageFormat format, ImageUsage usage)
{
    HardwareImageCreateInfo info(width, height, format);
    info.usage = usage;
    info.linearFiltering = true;
    return info;
}

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

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

inline Mesh load_mesh(const std::filesystem::path& model_path)
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

    Mesh mesh;
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

            auto [found, inserted] = unique_vertices.emplace(vertex, static_cast<uint32_t>(mesh.vertices.size()));
            if (inserted)
            {
                if (mesh.vertices.size() >= 0x10000u)
                    throw std::runtime_error("Mesh exceeds the 65536-vertex 16-bit index limit: " + model_path_str);
                mesh.vertices.push_back(vertex);
            }
            mesh.indices.push_back(static_cast<uint16_t>(found->second));
        }
    }

    return mesh;
}

} // namespace baseline

