#include "DrawStressRayTrace.h"
#include <array>

DrawStressRayTrace::DrawStressRayTrace(void* native_window)
    : ExampleBase(native_window), display(native_window), instances(dim * dim * dim)
{
    constexpr std::array<std::array<float, 3>, 8> positions{{
        {-1,  1,  1}, {1,  1,  1}, {-1, -1,  1}, {1, -1,  1},
        {-1,  1, -1}, {1,  1, -1}, {-1, -1, -1}, {1, -1, -1},
    }};
    constexpr std::array<uint16_t, 36> triangles{
        0, 1, 2, 1, 3, 2,   4, 6, 5, 5, 6, 7,
        0, 2, 4, 4, 2, 6,   1, 5, 3, 5, 7, 3,
        0, 4, 1, 4, 5, 1,   2, 3, 6, 6, 3, 7,
    };
    HardwareBuffer vertices, indices;
    executor << vertices.createDeviceLocal(HardwareBuffer::Usage::VertexBuffer, positions.data(), positions.size(), sizeof(positions[0]))
             << indices.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer, triangles.data(), triangles.size(), sizeof(triangles[0]));
    RayTracePipeline<>::AccelerationStructure cube;
    executor << cube.build(vertices, indices);
    constexpr float step = 0.6f;
    for (int z = 0; z < dim; ++z) {
        for (int y = 0; y < dim; ++y) {
            for (int x = 0; x < dim; ++x) {
                auto& instance = instances[(z * dim + y) * dim + x];
                instance.geometry = cube;
                instance.transform[3] = ktm::fvec4(step * (x - dim / 2), step * (y - dim / 2), -15.0f + step * z, 1);
            }
        }
    }

    const auto view = ktm::look_at_lh(ktm::fvec3(0, 0, -35), ktm::fvec3(0), ktm::fvec3(0, 1, 0));
    auto projection = ktm::perspective_lh(ktm::radians(60.0f), float(width) / height, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    pipeline.camera.inverseViewProjection = ktm::inverse(projection * view);
    pipeline.output = color;
}

void DrawStressRayTrace::run(float seconds)
{
    std::array<ktm::fvec3, dim> sine, cosine;
    for (int i = 0; i < dim; ++i) {
        const auto angles = seconds + float(i) * ktm::fvec3(0.21f, 0.37f, 0.13f);
        sine[i] = ktm::sin(angles);
        cosine[i] = ktm::cos(angles);
    }
    // The translation is fixed. Update the three columns of 0.25 * Rz * Ry * Rx.
    for (int z = 0; z < dim; ++z) {
        const float sz = sine[z].z, cz = cosine[z].z;
        for (int y = 0; y < dim; ++y) {
            const float sy = sine[y].y, cy = cosine[y].y;
            const auto axisX = 0.25f * ktm::fvec4(cz * cy, cy * sz, -sy, 0);
            for (int x = 0; x < dim; ++x) {
                const float sx = sine[x].x, cx = cosine[x].x;
                auto& model = instances[(z * dim + y) * dim + x].transform;
                model[0] = axisX;
                model[1] = 0.25f * ktm::fvec4(cz * sy * sx - cx * sz, cz * cx + sz * sy * sx, cy * sx, 0);
                model[2] = 0.25f * ktm::fvec4(sz * sx + cz * cx * sy, cx * sz * sy - cz * sx, cy * cx, 0);
            }
        }
    }
    RayTracePipeline<>::AccelerationStructure scene;
    executor << scene.build(instances);
    pipeline.scene = scene;
    executor << pipeline(width, height) << display(color);
    executor.commit();
}
