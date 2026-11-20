#include "common.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <memory>

#include "horizon/core/logging.h"

HardwareImage baseline::loadTexture(const std::string& texturePath)
{
    int width = 0;
    int height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data(
        stbi_load(texturePath.c_str(), &width, &height, nullptr, STBI_rgb_alpha), stbi_image_free);
    if (!data)
    {
        CFW_LOG_ERROR("Failed to load texture '{}': {}", texturePath, stbi_failure_reason());
        return {};
    }

    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    HardwareImage texture(imageInfo(width, height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage));
    HardwareBuffer staging(static_cast<uint32_t>(byteCount), BufferUsage::StorageBuffer, data.get());
    HardwareExecutor upload;
    upload << staging.copyTo(texture);
    upload.commit().waitForDeferredResources();
    return texture;
}
