#include "BaselineSlang.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#undef STBI_ONLY_PNG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC
#include <tiny_obj_loader.h>

#include "Codegen/ControlFlows.h"
#include SLANG(baseline_vert.slang)
#include SLANG(baseline_frag.slang)

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
        vertex_buffer.createDeviceLocal(render_executor, BufferUsage::VertexBuffer,
            vertices.data(), vertices.size(), sizeof(vertices[0]));
        index_buffer.createDeviceLocal(render_executor, BufferUsage::IndexBuffer,
            indices.data(), indices.size(), sizeof(indices[0]));
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

    RasterizerPipeline<baseline_vert_slang_t, baseline_frag_slang_t> pipeline;
    pipeline.setRasterState({.compare = RasterCompare::LessEqual});
    pipeline.outColor = color;
    // 纹理存入 bindless combined-texture 表（set 0），拿到索引后经逐 draw 参数传入 shader。
    parameters.texIndex = texture_image.storeDescriptor();

    pipeline.vp.view = ktm::look_at_rh<float>({2, 2, 2}, {0, 0, 0}, {0, 0, 1});
    // KTM 0.2.16's frustum_rh gives positive clip W along the camera's -Z axis.
    constexpr float near_plane = 0.1f, far_plane = 10.0f;
    const float top = near_plane * ktm::tan(ktm::radians(45.0f) * 0.5f);
    const float right = top * (800.0f / 600.0f);
    auto projection = ktm::frustum_rh(-right, right, top, -top, near_plane, far_plane);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    pipeline.vp.proj = projection;
    rasterizer = std::move(pipeline);
}

void BaselineSlang::run()
{
    constexpr float fixed_dt = 0.0f;
    const auto now = std::chrono::steady_clock::now();
    if (frame == 0) start = now;
    const float seconds = fixed_dt ? static_cast<float>(frame) * fixed_dt : std::chrono::duration<float>(now - start).count();
    ++frame;
    parameters.model = ktm::rotate3d_z(seconds * ktm::radians(90.0f));

    auto batch = rasterizer.beginBatch();
    batch.draw(vertex_buffer, index_buffer, parameters);
    render_executor << std::move(batch);
    display.present(render_executor, color);
}
