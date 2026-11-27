#include "DrawStressSlang.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Based on the single-threaded bgfx examples/17-drawstress workload.
DrawStressSlang::DrawStressSlang(void* native_window)
    : ExampleBase(native_window),
      display(native_window), final_output_image(800, 600, HardwareImage::Format::RGBA16_FLOAT),
      depth_image(800, 600, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage)
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
        std::vector<RasterPipeline<>::IndirectPacket> packets(draw_count);
        for (uint32_t i = 0; i < draw_count; ++i) {
            packets[i].vertexCount = static_cast<uint32_t>(cube_indices.size());
            packets[i].instanceCount = 1;
            packets[i].parameterWord = i * (sizeof(ktm::fmat4x4) / sizeof(uint32_t));
        }
        render_executor << vertex_buffer.createDeviceLocal(HardwareBuffer::Usage::VertexBuffer,
            cube_vertices.data(), cube_vertices.size(), sizeof(cube_vertices[0]));
        render_executor << index_buffer.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer,
            cube_indices.data(), cube_indices.size(), sizeof(cube_indices[0]));
        render_executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect,
            packets.data(), packets.size(), sizeof(packets[0]));
    }
    render_executor.commit().wait();
    for (auto& parameters : parameter_buffers)
        parameters = HardwareBuffer(draw_count, sizeof(ktm::fmat4x4), HardwareBuffer::Usage::StorageBuffer);

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
    pipeline.outColor = final_output_image;
}

void DrawStressSlang::run(float seconds)
{
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
    // Finish the previous pair of submissions before reusing their matrix buffers.
    if (next_parameter_buffer == parameter_buffers.size()) {
        render_executor.wait();
        next_parameter_buffer = 0;
    }
    auto& parameters = parameter_buffers[next_parameter_buffer++];
    auto* matrix_bytes = static_cast<std::byte*>(parameters.getMappedData());
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
    parameters.flushMapped();

    render_executor << pipeline(vertex_buffer, index_buffer, commands, draw_count, parameters, depth_image)
                    << display(final_output_image);
    render_executor.commit();
}
