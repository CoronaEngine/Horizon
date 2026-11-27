#include "DisneySlang.h"

#include <array>
#include <vector>

DisneySlang::DisneySlang(void* native_window)
    : ExampleBase(native_window), scene(render_executor), display(native_window),
      pipeline({.topology = RasterPipeline<>::Topology::TriangleStrip})
{
    // Each triangle-strip vertex also supplies the sphere's object-space normal.
    constexpr int slices = DisneyScene::slices, stacks = DisneyScene::stacks;
    std::vector<std::array<float, 3>> vertices;
    std::vector<uint16_t> indices;
    vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));
    indices.reserve(static_cast<size_t>(stacks * (slices + 1) * 2 + (stacks - 1) * 2));

    for (int y = 0; y <= stacks; ++y) {
        const float phi = (static_cast<float>(y) / stacks) * ktm::pi<float>;
        const float sin_phi = ktm::sin(phi);
        const float cos_phi = ktm::cos(phi);

        for (int x = 0; x <= slices; ++x) {
            const float theta = (static_cast<float>(x) / slices) * ktm::two_pi<float>;
            vertices.push_back({ sin_phi * ktm::cos(theta), cos_phi, sin_phi * ktm::sin(theta) });
        }
    }

    for (int y = 0; y < stacks; ++y) {
        const auto first = static_cast<uint16_t>(y * (slices + 1));
        if (y != 0) {
            indices.push_back(indices.back());
            indices.push_back(first);
        }
        for (int x = 0; x <= slices; ++x) {
            indices.push_back(static_cast<uint16_t>(first + x));
            indices.push_back(static_cast<uint16_t>(first + slices + 1 + x));
        }
    }

    render_executor << vertex_buffer.createDeviceLocal(HardwareBuffer::Usage::VertexBuffer,
        vertices.data(), vertices.size(), sizeof(vertices[0]));
    render_executor << index_buffer.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer,
        indices.data(), indices.size(), sizeof(indices[0]));

    const auto parameter_words = static_cast<uint32_t>(scene.parameters.getElementSize() / sizeof(uint32_t));
    std::array<RasterPipeline<>::IndirectPacket, DisneyScene::rows * DisneyScene::columns> packets;
    for (uint32_t i = 0; i < packets.size(); ++i) {
        packets[i] = {.vertexCount = static_cast<uint32_t>(indices.size()), .instanceCount = 1,
                      .parameterWord = i * parameter_words};
    }
    render_executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect,
        packets.data(), packets.size(), sizeof(packets[0]));
    scene.bind(pipeline);
}

void DisneySlang::run(float)
{
    render_executor << pipeline(vertex_buffer, index_buffer, commands, DisneyScene::rows * DisneyScene::columns,
                                scene.parameters, scene.depth) << display(scene.color);
    render_executor.commit();
}
