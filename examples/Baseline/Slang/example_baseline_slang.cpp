#include "BaselineSlang.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include <tiny_obj_loader.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <utility>
#include <vector>

BaselineSlang::BaselineSlang(void* native_window)
    : ExampleBase(native_window), display(native_window, PresentDevicePreference::RenderDevice),
      color(800, 600, ImageFormat::RGBA16_FLOAT)
{
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
        render_executor << vertex_buffer.createDeviceLocal(BufferUsage::VertexBuffer,
            vertices.data(), vertices.size(), sizeof(vertices[0]));
        render_executor << index_buffer.createDeviceLocal(BufferUsage::IndexBuffer,
            indices.data(), indices.size(), sizeof(indices[0]));
        const RasterIndirectPacket command{.vertexCount = static_cast<uint32_t>(indices.size()), .instanceCount = 1};
        render_executor << commands.createDeviceLocal(BufferUsage::RasterIndirect, &command, 1, sizeof(command));
    }

    {
        const auto filename = (assets / "textures/viking_room.png").string();
        int texture_width = 0, texture_height = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> texels(
            stbi_load(filename.c_str(), &texture_width, &texture_height, nullptr, STBI_rgb_alpha), stbi_image_free);

        HardwareImageCreateInfo texture_info(texture_width, texture_height, ImageFormat::RGBA8_SRGB);
        texture_info.linearFiltering = true;
        texture_image = HardwareImage(texture_info);
        render_executor << texture_image.copyFrom(texels.get());
        render_executor.commit().waitForDeferredResources();
    }

    pipeline.setRasterState({.compare = RasterCompare::LessEqual});
    pipeline.outColor = color;
    // 纹理存入 bindless combined-texture 表（set 0），拿到索引后经逐 draw 参数传入 shader。
    parameters.texIndex = texture_image.storeDescriptor();
    for (auto& buffer : parameter_buffers)
        buffer = HardwareBuffer(sizeof(DrawParameters), BufferUsage::StorageBuffer);

    pipeline.vp.view = ktm::look_at_rh<float>({2, 2, 2}, {0, 0, 0}, {0, 0, 1});
    // KTM 0.2.16's frustum_rh gives positive clip W along the camera's -Z axis.
    constexpr float near_plane = 0.1f, far_plane = 10.0f;
    const float top = near_plane * ktm::tan(ktm::radians(45.0f) * 0.5f);
    const float right = top * (800.0f / 600.0f);
    auto projection = ktm::frustum_rh(-right, right, top, -top, near_plane, far_plane);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    pipeline.vp.proj = projection;
}

void BaselineSlang::run(float seconds)
{
    parameters.model = ktm::rotate3d_z(seconds * ktm::radians(90.0f));
    if (next_parameter_buffer == parameter_buffers.size()) {
        render_executor.waitForDeferredResources();
        next_parameter_buffer = 0;
    }
    auto& parameter_buffer = parameter_buffers[next_parameter_buffer++];
    parameter_buffer.copyFromData(&parameters, sizeof(parameters));

    auto batch = pipeline.beginBatch();
    batch.drawIndirect(vertex_buffer, index_buffer, commands, 1, parameter_buffer);
    render_executor << std::move(batch);
    display.present(render_executor, color);
}
