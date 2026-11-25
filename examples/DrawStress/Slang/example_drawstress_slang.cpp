#include "DrawStressSlang.h"
#include "Codegen/ControlFlows.h"

#include SLANG(drawstress_vert.slang)
#include SLANG(drawstress_frag.slang)

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

// Based on the single-threaded bgfx examples/17-drawstress workload.
DrawStressSlang::DrawStressSlang(void* native_window)
    : ExampleBase(native_window),
      display(native_window, PresentDevicePreference::RenderDevice), final_output_image(800, 600, ImageFormat::RGBA16_FLOAT)
{
    // The buffer supplies the vertex count. Both shaders decode the indexed corner directly.
    constexpr std::array<uint32_t, 8> cube_vertices = { 0, 1, 2, 3, 4, 5, 6, 7 };

    constexpr std::array<uint16_t, 36> cube_indices = {
        0, 1, 2, 1, 3, 2,
        4, 6, 5, 5, 6, 7,
        0, 2, 4, 4, 2, 6,
        1, 5, 3, 5, 7, 3,
        0, 4, 1, 4, 5, 1,
        2, 3, 6, 6, 3, 7,
    };
    {
        // Each packet points to its cube's mat4 in the per-frame payload, in 32-bit words.
        std::vector<RasterIndirectPacket> packets(draw_count);
        for (uint32_t i = 0; i < draw_count; ++i) {
            packets[i].vertexCount = static_cast<uint32_t>(cube_indices.size());
            packets[i].instanceCount = 1;
            packets[i].parameterWord = i * (sizeof(ktm::fmat4x4) / sizeof(uint32_t));
        }
        vertex_buffer.createDeviceLocal(render_executor, BufferUsage::VertexBuffer,
            cube_vertices.data(), cube_vertices.size(), sizeof(cube_vertices[0]));
        index_buffer.createDeviceLocal(render_executor, BufferUsage::IndexBuffer,
            cube_indices.data(), cube_indices.size(), sizeof(cube_indices[0]));
        commands.createDeviceLocal(render_executor, BufferUsage::RasterIndirect,
            packets.data(), packets.size(), sizeof(packets[0]));
    }
    render_executor.commit().waitForDeferredResources();

    // Match the left-handed camera in the bgfx drawstress example.
    const auto view = ktm::look_at_lh(ktm::fvec3(0.0f, 0.0f, -35.0f), ktm::fvec3(0.0f), ktm::fvec3(0.0f, 1.0f, 0.0f));
    auto projection = ktm::perspective_lh(ktm::radians(60.0f), 800.0f / 600.0f, 0.1f, 100.0f);
    projection[1][1] *= -1.0f; // Vulkan clip-space Y.
    const auto view_projection = projection * view;
    axis_projection = {view_projection[0].x, view_projection[1].y, view_projection[2].z, view_projection[2].w};
    constexpr float step = 0.6f;
    const ktm::fvec3 base(-step * dim / 2.0f, -step * dim / 2.0f, -15.0f);
    for (int i = 0; i < dim; ++i)
        translations[i] = view_projection * ktm::fvec4(base + i * step, 1.0f);

    final_output_image.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);
    RasterizerPipeline<drawstress_vert_slang_t, drawstress_frag_slang_t> pipeline;
    pipeline.outColor = final_output_image;
    rasterizer = std::move(pipeline);
}

void DrawStressSlang::run()
{
    constexpr float fixed_dt = 0.0f;
    const auto now = std::chrono::steady_clock::now();
    if (frame == 0) start = now;
    const float seconds = fixed_dt ? static_cast<float>(frame) * fixed_dt : std::chrono::duration<float>(now - start).count();
    ++frame;

    auto batch = rasterizer.beginBatch();
    batch.drawIndirect(vertex_buffer, index_buffer, commands, draw_count, draw_count * sizeof(ktm::fmat4x4),
        [&](std::span<std::byte> destination) {
            // The fixed view faces +Z. Clip X and Y scale independently, and Z supplies both clip Z and W.
            const auto projectAxis = [&](float x, float y, float z) {
                return axis_projection * (0.25f * ktm::fvec4(x, y, z, z));
            };

            // Each axis has only dim distinct angles. The columns below encode Rz * Ry * Rx.
            std::array<ktm::fvec3, dim> sine, cosine;
            for (int i = 0; i < dim; ++i) {
                const auto angles = seconds + float(i) * ktm::fvec3(0.21f, 0.37f, 0.13f);
                sine[i] = ktm::sin(angles);
                cosine[i] = ktm::cos(angles);
            }
            auto* matrix_bytes = destination.data();
            for (int z = 0; z < dim; ++z) {
                const float sz = sine[z].z, cz = cosine[z].z;
                for (int y = 0; y < dim; ++y) {
                    const float sy = sine[y].y, cy = cosine[y].y;
                    const auto axisX = projectAxis(cz * cy, cy * sz, -sy);
                    ktm::fvec4 translation = translations[z];
                    translation.y = translations[y].y;
                    for (int x = 0; x < dim; ++x) {
                        const float sx = sine[x].x, cx = cosine[x].x;
                        translation.x = translations[x].x;
                        const ktm::fmat4x4 matrix(axisX,
                            projectAxis(cz * sy * sx - cx * sz, cz * cx + sz * sy * sx, cy * sx),
                            projectAxis(sz * sx + cz * cx * sy, cx * sz * sy - cz * sx, cy * cx),
                            translation);
                        std::memcpy(matrix_bytes, &matrix, sizeof(matrix));
                        matrix_bytes += sizeof(matrix);
                    }
                }
            }
        }, IndirectFirstInstance::AlwaysZero);

    render_executor << std::move(batch);
    display.present(render_executor, final_output_image);
}
