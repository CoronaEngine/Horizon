#include "common.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_DXT_STATIC
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>

#include <cstring>
#include <exception>
#include <span>

#include "corona/kernel/core/i_logger.h"
#include "hardware_wrapper_vulkan/hardware/execution.h"

using namespace Corona::Horizon;

namespace {

HardwareImageDesc make_texture_desc(uint32_t width, uint32_t height, Format format)
{
    return HardwareImageDesc::texture_2d(width,
                                         height,
                                         format,
                                         ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst);
}

std::span<const std::byte> byte_span(const unsigned char *data, size_t byte_count)
{
    return std::as_bytes(std::span<const unsigned char>(data, byte_count));
}

std::span<const std::byte> byte_span(const std::vector<uint8_t> &data)
{
    return std::as_bytes(std::span<const uint8_t>(data));
}

HardwareBuffer make_staging_buffer(std::span<const std::byte> data)
{
    HardwareBufferDesc desc;
    desc.element_count = data.size_bytes();
    desc.element_size = 1;
    desc.usage = BufferUsageFlags::TransferSrc;
    desc.cpu_access = CpuAccessMode::Write;
    return HardwareBuffer(desc, data);
}

HardwareImage create_uploaded_image(const HardwareImageDesc &desc, std::span<const std::byte> data)
{
    HardwareImage image(desc);
    if (!data.empty())
    {
        HardwareBuffer staging = make_staging_buffer(data);
        HardwareExecutor executor;
        auto receipt = executor.stream()
            << image.copy_from(staging)
            << commit();
        (void)receipt;
    }
    return image;
}

std::vector<uint8_t> compressToBC1(const unsigned char *data, int width, int height, int channels)
{
    const uint32_t blockCountX = (width + 3) / 4;
    const uint32_t blockCountY = (height + 3) / 4;
    std::vector<uint8_t> compressedData(blockCountX * blockCountY * 8);

    for (uint32_t by = 0; by < blockCountY; ++by)
    {
        for (uint32_t bx = 0; bx < blockCountX; ++bx)
        {
            uint8_t block[64];

            for (int py = 0; py < 4; ++py)
            {
                for (int px = 0; px < 4; ++px)
                {
                    const int x = static_cast<int>(bx) * 4 + px;
                    const int y = static_cast<int>(by) * 4 + py;
                    const int dstIdx = (py * 4 + px) * 4;

                    if (x < width && y < height)
                    {
                        const int srcIdx = (y * width + x) * channels;
                        block[dstIdx + 0] = data[srcIdx + 0];
                        block[dstIdx + 1] = channels > 1 ? data[srcIdx + 1] : data[srcIdx];
                        block[dstIdx + 2] = channels > 2 ? data[srcIdx + 2] : data[srcIdx];
                        block[dstIdx + 3] = 255;
                    }
                    else
                    {
                        block[dstIdx + 0] = 0;
                        block[dstIdx + 1] = 0;
                        block[dstIdx + 2] = 0;
                        block[dstIdx + 3] = 255;
                    }
                }
            }

            uint8_t *outBlock = &compressedData[(by * blockCountX + bx) * 8];
            stb_compress_dxt_block(outBlock, block, 0, STB_DXT_NORMAL);
        }
    }

    return compressedData;
}

} // namespace

void testCompressedTextures()
{
    CFW_LOG_DEBUG("=== Start testing texture compression formats ===");

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data = stbi_load(defaultTexturePath.c_str(), &width, &height, &channels, 4);
    if (!data)
    {
        CFW_LOG_ERROR("Failed to load texture: {}", stbi_failure_reason());
        return;
    }

    CFW_LOG_DEBUG("Original texture size: {}x{}, channels: {}", width, height, channels);

    auto compressedData = compressToBC1(data, width, height, 4);
    CFW_LOG_DEBUG("BC1 compressed size: {} bytes", compressedData.size());
    CFW_LOG_DEBUG("Compression ratio: {}:1", static_cast<float>(width * height * 4) / compressedData.size());

    try
    {
        CFW_LOG_DEBUG("Testing BC1_RGB_UNORM format...");
        HardwareImage textureBC1Unorm =
            create_uploaded_image(make_texture_desc(static_cast<uint32_t>(width),
                                                    static_cast<uint32_t>(height),
                                                    Format::BC1_UNORM),
                                  byte_span(compressedData));
        CFW_LOG_DEBUG("BC1_RGB_UNORM texture created success, descriptor ID: {}", textureBC1Unorm.store_descriptor());

        CFW_LOG_DEBUG("Testing BC1_RGB_SRGB format...");
        HardwareImage textureBC1Srgb =
            create_uploaded_image(make_texture_desc(static_cast<uint32_t>(width),
                                                    static_cast<uint32_t>(height),
                                                    Format::BC1_UNORM_SRGB),
                                  byte_span(compressedData));
        CFW_LOG_DEBUG("BC1_RGB_SRGB texture created success, descriptor ID: {}", textureBC1Srgb.store_descriptor());

        CFW_LOG_DEBUG("=== All compressed format tests passed ===");
    }
    catch (const std::exception &e)
    {
        CFW_LOG_ERROR("Compress texture failed: {}", e.what());
    }

    stbi_image_free(data);
}

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
    result.texture = create_uploaded_image(
        make_texture_desc(static_cast<uint32_t>(width), static_cast<uint32_t>(height), Format::SRGBA8_UNORM),
        byte_span(data, byteCount));
    result.descriptorID = result.texture.store_descriptor();
    result.success = true;

    stbi_image_free(data);

    return result;
}

TextureLoadResult loadCompressedTexture(const std::string &texturePath, bool useSRGB)
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

    CFW_LOG_DEBUG("Compressing texture: {}x{}", width, height);

    auto compressedData = compressToBC1(data, width, height, 4);
    stbi_image_free(data);

    CFW_LOG_DEBUG("BC1 compressed size: {} bytes, ratio: {}:1",
                  compressedData.size(),
                  static_cast<float>(width * height * 4) / compressedData.size());

    result.texture = create_uploaded_image(
        make_texture_desc(static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height),
                          useSRGB ? Format::BC1_UNORM_SRGB : Format::BC1_UNORM),
        byte_span(compressedData));
    result.descriptorID = result.texture.store_descriptor();
    result.success = true;

    return result;
}

TextureLoadResult loadTextureWithMipmapAndLayers(const std::string &texturePath,
                                                 int arrayLayers,
                                                 int mipLevels,
                                                 int viewLayer,
                                                 int viewMip)
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

    std::vector<unsigned char> layerData;
    const size_t layerSize = static_cast<size_t>(width) * height * 4;
    layerData.resize(layerSize * arrayLayers);

    for (int i = 0; i < arrayLayers; ++i)
    {
        std::memcpy(layerData.data() + layerSize * i, data, layerSize);
    }
    stbi_image_free(data);

    HardwareImageDesc createInfo =
        HardwareImageDesc::texture_2d_array(static_cast<uint32_t>(width),
                                            static_cast<uint32_t>(height),
                                            static_cast<uint32_t>(arrayLayers),
                                            Format::SRGBA8_UNORM,
                                            ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst);
    createInfo.mip_levels = static_cast<uint32_t>(mipLevels);

    result.texture = create_uploaded_image(createInfo, byte_span(layerData));

    auto textureView = result.texture[static_cast<uint32_t>(viewLayer)][static_cast<uint32_t>(viewMip)];
    result.descriptorID = textureView.store_descriptor();
    result.success = true;

    CFW_LOG_INFO("Texture with mipmap/layers loaded successfully, descriptor ID: {}", result.descriptorID);

    return result;
}
