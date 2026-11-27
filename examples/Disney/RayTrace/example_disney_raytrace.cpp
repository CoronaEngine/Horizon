#include "DisneyRayTrace.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

DisneyRayTrace::DisneyRayTrace(void* native_window)
    : ExampleBase(native_window), display(native_window)
{
    // Upload the six faces and all prefiltered mips of each environment map.
    HardwareImage radiance, irradiance;
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
                executor << staging.copyTo(*cubemap, offset, face, mip);
                offset += static_cast<uint64_t>(dim) * dim * sizeof(uint64_t);
            }
        }
    }

    // All 110 instances share one latitude-longitude sphere.
    constexpr uint32_t slices = 48, stacks = 24;
    std::vector<std::array<float, 3>> positions;
    std::vector<uint16_t> indices;
    for (uint32_t y = 0; y <= stacks; ++y) {
        const float phi = ktm::pi<float> * float(y) / stacks;
        for (uint32_t x = 0; x <= slices; ++x) {
            const float theta = ktm::two_pi<float> * float(x) / slices;
            positions.push_back({ktm::sin(phi) * ktm::cos(theta), ktm::cos(phi), ktm::sin(phi) * ktm::sin(theta)});
            if (x == slices || y == stacks) continue;
            const auto a = static_cast<uint16_t>(y * (slices + 1) + x);
            const auto b = static_cast<uint16_t>(a + slices + 1);
            const auto c = static_cast<uint16_t>(a + 1), d = static_cast<uint16_t>(b + 1);
            indices.insert(indices.end(), {a, b, c, c, b, d});
        }
    }
    HardwareBuffer vertices, triangles;
    executor << vertices.createDeviceLocal(HardwareBuffer::Usage::VertexBuffer, positions.data(), positions.size(), sizeof(positions[0]))
             << triangles.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer, indices.data(), indices.size(), sizeof(indices[0]));
    RayTracePipeline<>::AccelerationStructure sphere, scene;
    executor << sphere.build(vertices, triangles);

    struct alignas(16) Material {
        ktm::fvec4 baseColorMetallic;
        float roughness = 0.5f, specular = 0.5f, specularTint = 0.0f, subsurface = 0.0f;
        float anisotropic = 0.0f, sheen = 0.0f, sheenTint = 0.5f, clearcoat = 0.0f;
        float clearcoatGloss = 1.0f;
    };
    static_assert(sizeof(Material) == 64 && offsetof(Material, clearcoatGloss) == 48);
    constexpr int rows = 10, columns = 11;
    std::array<RayTracePipeline<>::Instance, rows * columns> instances;
    std::array<Material, rows * columns> materials;
    constexpr float radius = 0.42f, spacing = 1.12f;
    for (int col = 0; col < columns; ++col) {
        const float x = (col - (columns - 1) * 0.5f) * spacing;
        const float t = static_cast<float>(col) / (columns - 1);
        const Material column[rows] = {
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
            const auto id = static_cast<uint32_t>(row * columns + col);
            materials[id] = column[row];
            instances[id] = {.geometry = sphere,
                             .transform = ktm::translate3d(ktm::fvec3(x, y, 0)) * ktm::scale3d(ktm::fvec3(radius)),
                             .id = id};
        }
    }
    HardwareBuffer parameters;
    executor << parameters.createDeviceLocal(HardwareBuffer::Usage::StorageBuffer,
        materials.data(), materials.size(), sizeof(Material));
    executor << scene.build(instances);

    constexpr float yaw = ktm::pi<float>, pitch = 0.42f * ktm::pi<float>;
    const auto eye = 13.5f * ktm::fvec3(ktm::sin(pitch) * ktm::sin(yaw), ktm::cos(pitch), ktm::sin(pitch) * ktm::cos(yaw));
    const auto view = ktm::look_at_lh(eye, ktm::fvec3(0), ktm::fvec3(0, 1, 0));
    auto projection = ktm::perspective_lh(ktm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    pipeline.camera.inverseViewProjection = ktm::inverse(projection * view);
    pipeline.scene = scene;
    pipeline.output = color;
    pipeline.materials = parameters;
    pipeline.environment[0] = radiance;
    pipeline.environment[1] = irradiance;
}

void DisneyRayTrace::run(float)
{
    executor << pipeline(width, height) << display(color);
    executor.commit();
}
