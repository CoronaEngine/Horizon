#include "BaselineRayTrace.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#include <tiny_obj_loader.h>

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

BaselineRayTrace::BaselineRayTrace(void* native_window)
    : ExampleBase(native_window), display(native_window)
{
    const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
    const auto filename = (assets / "models/viking_room.obj").string();
    const auto material_dir = (assets / "models/").string();
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, filename.c_str(), material_dir.c_str());

    std::vector<uint32_t> indices;
    std::vector<std::array<float, 2>> cornerUVs;
    for (const auto& shape : shapes) {
        for (const auto& corner : shape.mesh.indices) {
            indices.push_back(static_cast<uint32_t>(corner.vertex_index));
            std::array<float, 2> uv{};
            if (corner.texcoord_index >= 0) {
                const size_t i = 2 * static_cast<size_t>(corner.texcoord_index);
                uv = {attrib.texcoords[i], 1.0f - attrib.texcoords[i + 1]};
            }
            cornerUVs.push_back(uv);
        }
    }
    HardwareBuffer positions, triangles, uvs;
    executor << positions.createDeviceLocal(HardwareBuffer::Usage::VertexBuffer, attrib.vertices.data(),
                                            attrib.vertices.size() / 3, 3 * sizeof(float))
             << triangles.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer, indices.data(), indices.size(), sizeof(indices[0]))
             << uvs.createDeviceLocal(HardwareBuffer::Usage::StorageBuffer, cornerUVs.data(), cornerUVs.size(), sizeof(cornerUVs[0]));

    RayTracePipeline<>::AccelerationStructure room, scene;
    executor << room.build(positions, triangles);
    const std::array instances{RayTracePipeline<>::Instance{.geometry = room}};
    executor << scene.build(instances);

    const auto texture_path = (assets / "textures/viking_room.png").string();
    int width = 0, height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> texels(
        stbi_load(texture_path.c_str(), &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    HardwareImage::CreateInfo texture_info(width, height, HardwareImage::Format::RGBA8_SRGB);
    texture_info.linearFiltering = true;
    HardwareImage texture(texture_info);
    executor << texture.copyFrom(texels.get());
    constexpr float near_plane = 0.1f, far_plane = 10.0f;
    const float top = near_plane * ktm::tan(ktm::radians(45.0f) * 0.5f);
    const float right = top * (800.0f / 600.0f);
    auto projection = ktm::frustum_rh(-right, right, top, -top, near_plane, far_plane);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    viewProjection = projection * ktm::look_at_rh<float>({2, 2, 2}, {0, 0, 0}, {0, 0, 1});
    pipeline.scene = scene;
    pipeline.output = color;
    pipeline.cornerUVs = uvs;
    pipeline.albedo = texture;
}

void BaselineRayTrace::run(float seconds)
{
    pipeline.camera.inverseViewProjection = ktm::inverse(viewProjection * ktm::rotate3d_z(seconds * ktm::radians(90.0f)));
    executor << pipeline(width, height) << display(color);
    executor.commit();
}
