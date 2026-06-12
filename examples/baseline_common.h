#pragma once

// =============================================================================
// baseline_common.h
// example_edsl 与 example_glsl 共用的 CPU 端数学 / 网格加载 / UBO 工具。
// 两个 baseline 示例此前各自维护一份逐字相同的拷贝，现集中到此处。
// =============================================================================

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#include <tiny_obj_loader.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace baseline
{

constexpr float pi = 3.14159265358979323846f;

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

inline Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
{
    return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

inline float dot(const Vec3& lhs, const Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline Vec3 cross(const Vec3& lhs, const Vec3& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

inline Vec3 normalize(const Vec3& value)
{
    const float length = std::sqrt(dot(value, value));
    return { value.x / length, value.y / length, value.z / length };
}

inline Mat4 identity()
{
    Mat4 result;
    result(0, 0) = 1.0f;
    result(1, 1) = 1.0f;
    result(2, 2) = 1.0f;
    result(3, 3) = 1.0f;
    return result;
}

inline Mat4 rotate_z(float radians)
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

inline Mat4 look_at_rh(const Vec3& eye, const Vec3& center, const Vec3& up)
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

inline Mat4 perspective_rh(float fovy_radians, float aspect, float near_plane, float far_plane)
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

inline Mat4 transpose(const Mat4& matrix)
{
    Mat4 result;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
            result(row, col) = matrix(col, row);
    }
    return result;
}

struct UniformBufferObject
{
    alignas(16) Mat4 model;
    alignas(16) Mat4 view;
    alignas(16) Mat4 proj;
};

struct Vertex
{
    std::array<float, 3> pos {};
    std::array<float, 3> color {};
    std::array<float, 2> tex_coord {};

    bool operator==(const Vertex& other) const
    {
        return pos == other.pos && color == other.color && tex_coord == other.tex_coord;
    }
};

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
        for (float value : vertex.color)
            combine(value);
        for (float value : vertex.tex_coord)
            combine(value);

        return seed;
    }
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
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

} // namespace baseline
