#include "DrawStressMesh.h"

#include <cstring>
#include <vector>

DrawStressMesh::DrawStressMesh(void* native_window)
    : ExampleBase(native_window), display(native_window),
      color(800, 600, HardwareImage::Format::RGBA16_FLOAT),
      depth(800, 600, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage)
{
    // One indirect draw per cube, with its transform indexed by SV_DrawIndex.
    const std::vector<MeshPipeline<>::IndirectPacket> packets(draw_count, {1, 1, 1});
    executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect,
                                           packets.data(), packets.size(), sizeof(packets[0]));
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

    color.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);
    pipeline.outColor = color;
}

void DrawStressMesh::run(float seconds)
{
    // Each axis has only dim distinct angles. The columns below encode Rz * Ry * Rx.
    std::array<ktm::fvec3, dim> sine, cosine;
    for (int i = 0; i < dim; ++i) {
        const auto angles = seconds + float(i) * ktm::fvec3(0.21f, 0.37f, 0.13f);
        sine[i] = ktm::sin(angles);
        cosine[i] = ktm::cos(angles);
    }
    // Finish the previous pair of submissions before reusing their matrix buffers.
    if (next_parameter_buffer == parameter_buffers.size()) {
        executor.wait();
        next_parameter_buffer = 0;
    }
    auto& parameters = parameter_buffers[next_parameter_buffer++];
    auto* matrix_bytes = static_cast<std::byte*>(parameters.getMappedData());
    for (int z = 0; z < dim; ++z) {
        const float sz = sine[z].z, cz = cosine[z].z;
        for (int y = 0; y < dim; ++y) {
            const float sy = sine[y].y, cy = cosine[y].y;
            const auto axisX = axis_projection * (0.25f * ktm::fvec4(cz * cy, cy * sz, -sy, -sy));
            ktm::fvec4 translation = translations[z];
            translation.y = translations[y].y;
            for (int x = 0; x < dim; ++x) {
                const float sx = sine[x].x, cx = cosine[x].x;
                translation.x = translations[x].x;
                const auto axisY = ktm::fvec4(cz * sy * sx - cx * sz, cz * cx + sz * sy * sx, cy * sx, cy * sx);
                const auto axisZ = ktm::fvec4(sz * sx + cz * cx * sy, cx * sz * sy - cz * sx, cy * cx, cy * cx);
                const ktm::fmat4x4 matrix(axisX, axis_projection * (0.25f * axisY),
                                          axis_projection * (0.25f * axisZ), translation);
                std::memcpy(matrix_bytes, &matrix, sizeof(matrix));
                matrix_bytes += sizeof(matrix);
            }
        }
    }
    parameters.flushMapped();

    pipeline.transforms = parameters;
    executor << pipeline(commands, draw_count, depth) << display(color);
    executor.commit();
}
