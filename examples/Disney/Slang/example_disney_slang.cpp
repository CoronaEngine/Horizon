#include "DisneySlang.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

DisneySlang::DisneySlang(void* native_window)
    : ExampleBase(native_window),
      display(native_window, PresentDevicePreference::RenderDevice), final_output_image(800, 600, HardwareImage::Format::RGBA16_FLOAT)
{
    // Upload the six faces and all prefiltered mips of each environment map.
    const auto environment_dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "env";
    for (const auto& [filename, face_size, mip_count, cubemap] : {
            std::tuple{"bolonga_lod.dds", 256u, 9u, &radiance},
            std::tuple{"bolonga_irr.dds", 128u, 1u, &irradiance}}) {
        uint32_t texels_per_face = 0;
        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            const uint32_t dim = face_size >> mip;
            texels_per_face += dim * dim;
        }
        std::vector<uint64_t> texels(6 * texels_per_face);
        std::ifstream file(environment_dir / filename, std::ios::binary);
        file.seekg(148); // The bundled DX10 DDS files store RGBA16F texels after the header.
        file.read(reinterpret_cast<char*>(texels.data()), texels.size() * sizeof(uint64_t));

        HardwareImage::CreateInfo desc(face_size, face_size, HardwareImage::Format::RGBA16_FLOAT);
        desc.arrayLayers = 6;
        desc.cube = true;
        desc.linearFiltering = true;
        desc.mipLevels = static_cast<int>(mip_count);
        *cubemap = HardwareImage(desc);
        HardwareBuffer staging(texels, HardwareBuffer::Usage::StorageBuffer);
        uint64_t offset = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t mip = 0; mip < mip_count; ++mip) {
                const uint32_t dim = face_size >> mip;
                render_executor << staging.copyTo(*cubemap, offset, face, mip);
                offset += static_cast<uint64_t>(dim) * dim * sizeof(uint64_t);
            }
        }
    }

    // Each triangle-strip vertex also supplies the sphere's object-space normal.
    {
        constexpr int slices = 48, stacks = 24;
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

        constexpr uint32_t parameter_words = sizeof(DrawParameters) / sizeof(uint32_t);
        std::array<RasterIndirectPacket, rows * columns> packets;
        for (uint32_t i = 0; i < packets.size(); ++i) {
            packets[i] = {.vertexCount = static_cast<uint32_t>(indices.size()), .instanceCount = 1,
                          .parameterWord = i * parameter_words};
        }
        render_executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect,
            packets.data(), packets.size(), sizeof(packets[0]));
    }
    std::array<DrawParameters, rows * columns> parameters;
    constexpr float radius = 0.42f, spacing = 1.12f;
    for (int col = 0; col < columns; ++col) {
        const float x = (col - (columns - 1) * 0.5f) * spacing;
        const float t = static_cast<float>(col) / (columns - 1);
        const DrawParameters column[rows] = {
            {.baseColorMetallic = {1.00f, 1.00f, 1.00f, 0.f}, .subsurface = t},
            {.baseColorMetallic = {1.00f, 0.86f, 0.12f, t}},
            {.baseColorMetallic = {0.92f, 0.12f, 0.12f, 0.f}, .specular = t},
            {.baseColorMetallic = {0.92f, 0.12f, 0.12f, 0.f}, .specularTint = t},
            {.baseColorMetallic = {0.42f, 0.32f, 0.92f, 0.f}, .roughness = t},
            {.baseColorMetallic = {0.92f, 0.18f, 0.78f, 0.f}, .anisotropic = t},
            {.baseColorMetallic = {0.62f, 0.18f, 0.14f, 0.f}, .sheen = t},
            {.baseColorMetallic = {0.62f, 0.18f, 0.14f, 0.f}, .sheenTint = t},
            {.baseColorMetallic = {0.10f, 0.34f, 0.40f, 0.f}, .clearcoat = t},
            {.baseColorMetallic = {0.10f, 0.34f, 0.40f, 0.f}, .clearcoat = 1.f, .clearcoatGloss = t},
        };
        for (int row = 0; row < rows; ++row) {
            const float y = ((rows - 1) * 0.5f - row) * spacing;
            auto& draw = parameters[row * columns + col];
            draw = column[row];
            draw.model = ktm::translate3d(ktm::fvec3(x, y, 0.0f)) * ktm::scale3d(ktm::fvec3(radius));
        }
    }
    render_executor << parameter_buffer.createDeviceLocal(HardwareBuffer::Usage::StorageBuffer,
        parameters.data(), parameters.size(), sizeof(DrawParameters));
    render_executor.commit().wait();

    final_output_image.setClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    pipeline.outColor = final_output_image;
    pipeline.setRasterState({.topology = RasterTopology::TriangleStrip});
    pipeline.uses({{radiance, ResourceAccess::Read}, {irradiance, ResourceAccess::Read}});

    constexpr float yaw = ktm::pi<float>, pitch = 0.42f * ktm::pi<float>;
    const auto eye = 13.5f * ktm::fvec3(ktm::sin(pitch) * ktm::sin(yaw), ktm::cos(pitch), ktm::sin(pitch) * ktm::cos(yaw));
    const auto view = ktm::look_at_lh(eye, ktm::fvec3(0.0f), ktm::fvec3(0, 1, 0));
    auto projection = ktm::perspective_lh(ktm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    pipeline.vsp.proj_view = projection * view;
    pipeline.vsp.camPos = ktm::fvec4(eye, 1.0f);
    pipeline.vsp.lightDir = ktm::fvec4(ktm::normalize(ktm::fvec3(-0.35f, 0.55f, -0.75f)), 0.0f);
    pipeline.vsp.texCubeIndex = radiance;
    pipeline.vsp.texCubeIrrIndex = irradiance;
}

void DisneySlang::run(float)
{
    render_executor << pipeline.drawIndirect(vertex_buffer, index_buffer, commands, rows * columns, parameter_buffer);
    display.present(render_executor, final_output_image);
}
