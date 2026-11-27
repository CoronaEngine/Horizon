#include "DrawStressEdsl.h"

#include "Codegen/BuiltinVariate.h"

#include <array>
#include <cstdint>
#include <vector>

// Based on the single-threaded bgfx examples/17-drawstress workload.
DrawStressEdsl::DrawStressEdsl(void* native_window)
    : ExampleBase(native_window), display(native_window)
{
    constexpr int width = 800, height = 600;
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
        std::vector<RasterPipeline<>::IndirectPacket> packets(DrawStressTransforms::cube_count);
        for (uint32_t i = 0; i < DrawStressTransforms::cube_count; ++i) {
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
    using EmbeddedShader::Float, EmbeddedShader::Float3, EmbeddedShader::Float4, EmbeddedShader::Uint;
    output_image = HardwareImage(width, height, HardwareImage::Format::RGBA16_FLOAT);
    depth_image = HardwareImage(width, height, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage);
    output_image.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);
    const auto vertex_shader = []() {
        const auto corner = EmbeddedShader::vertexIndex();
        Float3 color(corner & Uint(1), (corner >> Uint(1)) & Uint(1), (corner >> Uint(2)) & Uint(1));
        Float3 pos = color * Float3(2.0f, -2.0f, -2.0f) + Float3(-1.0f, 1.0f, 1.0f);
        auto mvp = EmbeddedShader::rasterDrawParameter<ktm::fmat4x4>(0);
        EmbeddedShader::position() = EmbeddedShader::mul(mvp, Float4(pos, Float(1)));
        return color;
    };
    const auto fragment_shader = [&](Float3 color) { output << Float4(color, Float(1)); };
    raster = RasterPipeline(vertex_shader, fragment_shader, {}, {.enableMatrixColumnMajor = true});
}

void DrawStressEdsl::run(float seconds)
{
    const auto& parameters = transforms.update(seconds, render_executor);
    render_executor << raster(vertex_buffer, index_buffer, commands, DrawStressTransforms::cube_count, parameters, depth_image)
                    << display(output_image);
    render_executor.commit();
}
