#include "DisneyScene.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

DisneyScene::DisneyScene(HardwareExecutor& executor)
{
    // Upload the six faces and all prefiltered mips of each environment map.
    const auto environment_dir = std::filesystem::path(__FILE__).parent_path() / "assets" / "env";
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

    struct alignas(16) DrawParameters {
        ktm::fmat4x4 model = ktm::fmat4x4::from_eye();
        ktm::fvec4 baseColorMetallic;
        float roughness = 0.5f, specular = 0.5f, specularTint = 0.0f, subsurface = 0.0f;
        float anisotropic = 0.0f, sheen = 0.0f, sheenTint = 0.5f, clearcoat = 0.0f;
        float clearcoatGloss = 1.0f;
    };
    static_assert(sizeof(DrawParameters) == 128 && offsetof(DrawParameters, baseColorMetallic) == 64 &&
                  offsetof(DrawParameters, roughness) == 80 && offsetof(DrawParameters, anisotropic) == 96 &&
                  offsetof(DrawParameters, clearcoatGloss) == 112);
    std::array<DrawParameters, rows * columns> materials;
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
            auto& draw = materials[row * columns + col];
            draw = column[row];
            draw.model = ktm::translate3d(ktm::fvec3(x, y, 0.0f)) * ktm::scale3d(ktm::fvec3(radius));
        }
    }
    executor << parameters.createDeviceLocal(HardwareBuffer::Usage::StorageBuffer,
        materials.data(), materials.size(), sizeof(DrawParameters));
    color.setClearColor(0.5f, 0.5f, 0.5f, 1);
}
