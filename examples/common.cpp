#include "scene_pass.h"
#include "common.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <exception>
#include <span>

#include "horizon/core/logging.h"

TextureLoadResult loadTexture(const std::string &texturePath)
{
    TextureLoadResult result;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    if (!data)
    {
        CFW_LOG_ERROR("Failed to load texture '{}': {}", texturePath, stbi_failure_reason());
        return result;
    }

    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    result.texture = HardwareImage(example::imageInfo(width, height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage));
    HardwareBuffer staging(static_cast<uint32_t>(byteCount), BufferUsage::StorageBuffer, data);
    HardwareExecutor upload;
    upload << staging.copyTo(result.texture);
    upload.commit().waitForDeferredResources();
    result.descriptorID = result.texture.storeDescriptor();
    result.success = true;

    stbi_image_free(data);

    return result;
}
