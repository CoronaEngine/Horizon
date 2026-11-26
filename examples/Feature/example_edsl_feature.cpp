#include "EdslFeature.h"

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include GLSL(Feature/edsl_feature_geometry.glsl)
#include HLSL(Feature/edsl_feature_lighting.hlsl)

#include <array>
#include <cstdint>
#include <utility>

EdslFeature::EdslFeature(void* native_window)
    : ExampleBase(native_window), display(native_window, PresentDevicePreference::RenderDevice)
{
    const std::array<ktm::fvec3, 6> face_normals{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    }};
    constexpr std::array<uint16_t, 6> quad_indices{0, 1, 2, 0, 2, 3};
    std::array<std::array<float, 6>, 24> vertices;
    std::array<uint16_t, 36> indices;
    for (size_t face = 0; face < face_normals.size(); ++face) {
        const auto n = face_normals[face];
        const ktm::fvec3 u = n.x == 0 ? ktm::fvec3(1, 0, 0) : ktm::fvec3(0, 1, 0);
        const auto v = ktm::cross(n, u);
        const std::array corners{n - u - v, n + u - v, n + u + v, n - u + v};
        for (size_t i = 0; i < corners.size(); ++i) {
            const auto p = corners[i];
            vertices[4 * face + i] = {p.x, p.y, p.z, n.x, n.y, n.z};
        }
        for (size_t i = 0; i < quad_indices.size(); ++i)
            indices[6 * face + i] = static_cast<uint16_t>(4 * face + quad_indices[i]);
    }
    vertex_buffer.createDeviceLocal(executor, BufferUsage::VertexBuffer, vertices.data(), vertices.size(), sizeof(vertices[0]));
    index_buffer.createDeviceLocal(executor, BufferUsage::IndexBuffer, indices.data(), indices.size(), sizeof(indices[0]));
    const RasterIndirectPacket command{.vertexCount = static_cast<uint32_t>(indices.size()), .instanceCount = 1};
    commands.createDeviceLocal(executor, BufferUsage::RasterIndirect, &command, 1, sizeof(command));
    executor.commit().waitForDeferredResources();

    using namespace EmbeddedShader;
    auto vertex_shader = [&](Aggregate<VertexInput> vertex) -> Float3 {
        position() = mul(view_projection, Float4(vertex->position, 1.0f));
        return vertex->normal;
    };
    auto fragment_shader = [&](Float3 normal) {
        albedo << Float4(edsl_feature_geometry_glsl.boxAlbedo(normal), 1.0f);
        normals << Float4(normal, 1.0f);
    };
    geometry = RasterizerPipeline(vertex_shader, fragment_shader, {.enableMatrixColumnMajor = true});
    geometry.setRasterState({.blend = RasterBlend::Opaque});
    albedo_image.setClearColor(0, 0, 0, 0);
    normal_image.setClearColor(0, 0, 0, 0);

    const auto view = ktm::look_at_rh<float>({3.4f, 2.6f, 4.4f}, {0, 0, 0}, {0, 1, 0});
    constexpr float near_plane = 0.1f;
    const float top = near_plane * ktm::tan(ktm::radians(45.0f) * 0.5f);
    const float right = top * (static_cast<float>(width) / height);
    auto projection = ktm::frustum_rh(-right, right, top, -top, near_plane, 20.0f);
    projection[1][1] *= -1.0f;
    view_projection = projection * view;

    auto compute_shader = [&] {
        const auto pixel = dispatchThreadID()->xy();
        Float4 material = albedo[pixel];
        Float4 normal = normals[pixel];
        const auto lit = edsl_feature_lighting_hlsl.shadeBox(material->xyz(), normal->xyz());
        const Float3 background(0.04f, 0.06f, 0.09f);
        shaded[pixel] = Float4(mix(background, lit, material->w), 1.0f);
    };
    lighting = ComputePipeline(compute_shader, ktm::uvec3(group_size, group_size, 1), {.compileHLSL = false});
    lighting.uses({{albedo_image, ResourceAccess::Read}, {normal_image, ResourceAccess::Read},
                   {shaded_image, ResourceAccess::Write}});
}

void EdslFeature::run(float)
{
    auto batch = geometry.beginBatch();
    batch.drawIndirect(vertex_buffer, index_buffer, commands, 1);
    executor << std::move(batch) << lighting(width / group_size, height / group_size);
    display.present(executor, shaded_image);
}
