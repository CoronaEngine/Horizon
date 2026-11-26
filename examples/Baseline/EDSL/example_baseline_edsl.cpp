#include "BaselineEdsl.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include <tiny_obj_loader.h>

#include "Codegen/BuiltinVariate.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <ktm/ktm.h>
#include <map>
#include <memory>
#include <vector>

BaselineEdsl::BaselineEdsl(void* native_window)
    : ExampleBase(native_window), display(native_window, PresentDevicePreference::RenderDevice)
{
    constexpr int width = 800, height = 600;
    const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
    {
        const auto filename = (assets / "models/viking_room.obj").string();
        const auto material_dir = (assets / "models/").string();
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, filename.c_str(), material_dir.c_str());

        using Vertex = std::array<float, 5>; // Position (x, y, z), then texture coordinates (u, v).
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
        render_executor << vertex_buffer.createDeviceLocal(HardwareBuffer::Usage::VertexBuffer,
            vertices.data(), vertices.size(), sizeof(vertices[0]));
        render_executor << index_buffer.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer,
            indices.data(), indices.size(), sizeof(indices[0]));
        const RasterIndirectPacket command{.vertexCount = static_cast<uint32_t>(indices.size()), .instanceCount = 1};
        render_executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect, &command, 1, sizeof(command));
    }

    {
        const auto filename = (assets / "textures/viking_room.png").string();
        int texture_width = 0, texture_height = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load(filename.c_str(), &texture_width, &texture_height, nullptr, STBI_rgb_alpha), stbi_image_free);

        HardwareImage::CreateInfo texture_info(texture_width, texture_height, HardwareImage::Format::RGBA8_SRGB);
        texture_info.linearFiltering = true;
        albedo_image = HardwareImage(texture_info);
        render_executor << albedo_image.copyFrom(pixels.get());
        render_executor.commit().wait();
    }

    using EmbeddedShader::Float2, EmbeddedShader::Float4;
    using EmbeddedShader::mul;

    color_image = HardwareImage(width, height, HardwareImage::Format::RGBA16_FLOAT);

    auto vertex_shader = [&](EmbeddedShader::Aggregate<VertexInput> vertex) -> Float2 {
        EmbeddedShader::position() = mul(proj, mul(view, mul(model, Float4(vertex->pos, 1.0f))));
        return vertex->tex_coord;
    };

    auto fragment_shader = [&](Float2 uv) {
        color << EmbeddedShader::texture(albedo, uv);
    };

    rasterizer = RasterizerPipeline(vertex_shader, fragment_shader, {.enableMatrixColumnMajor = true});
    rasterizer.setRasterState({.compare = RasterCompare::LessEqual});

    view = ktm::look_at_rh<float>({2, 2, 2}, {0, 0, 0}, {0, 0, 1});
    // KTM 0.2.16's frustum_rh gives positive clip W along the camera's -Z axis.
    constexpr float near_plane = 0.1f, far_plane = 10.0f;
    const float top = near_plane * ktm::tan(ktm::radians(45.0f) * 0.5f);
    const float right = top * (static_cast<float>(width) / height);
    auto projection = ktm::frustum_rh(-right, right, top, -top, near_plane, far_plane);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    proj = projection;
}

void BaselineEdsl::run(float seconds)
{
    model = ktm::rotate3d_z(seconds * ktm::radians(90.0f));

    render_executor << rasterizer.drawIndirect(vertex_buffer, index_buffer, commands, 1);
    display.present(render_executor, color_image);
}
