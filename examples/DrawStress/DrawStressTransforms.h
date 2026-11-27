#pragma once

#include "Horizon.h"
#include <ktm/ktm.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// All DrawStress graphics pipelines use the same animated clip-space transforms.
class DrawStressTransforms {
public:
    static constexpr int dim = 40;
    static constexpr uint32_t cube_count = dim * dim * dim;

    DrawStressTransforms()
    {
        for (auto& parameters : parameter_buffers)
            parameters = HardwareBuffer(cube_count, sizeof(ktm::fmat4x4), HardwareBuffer::Usage::StorageBuffer);

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
    }

    const HardwareBuffer& update(float seconds, HardwareExecutor& render_executor)
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
        return parameters;
    }

private:
    std::array<HardwareBuffer, 2> parameter_buffers;
    std::size_t next_parameter_buffer{};
    ktm::fvec4 axis_projection;
    std::array<ktm::fvec4, dim> translations;
};
