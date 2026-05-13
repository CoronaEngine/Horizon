#pragma once

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>

#include <cstdint>
#include <string>
#include <vector>

#include "Horizon.h"
#include "common/config.hpp"
#include "corona/kernel/core/i_logger.h"

// 纹理加载结果
struct TextureLoadResult
{
    HardwareImage texture;
    uint32_t descriptorID = 0;
    bool success = false;
};

// BC1压缩辅助函数
inline std::vector<uint8_t> compressToBC1(const unsigned char *data, int width, int height, int channels)
{
    uint32_t blockCountX = (width + 3) / 4;
    uint32_t blockCountY = (height + 3) / 4;
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
                    int x = bx * 4 + px;
                    int y = by * 4 + py;

                    if (x < width && y < height)
                    {
                        int srcIdx = (y * width + x) * channels;
                        block[(py * 4 + px) * 4 + 0] = data[srcIdx + 0];
                        block[(py * 4 + px) * 4 + 1] = channels > 1 ? data[srcIdx + 1] : data[srcIdx];
                        block[(py * 4 + px) * 4 + 2] = channels > 2 ? data[srcIdx + 2] : data[srcIdx];
                        block[(py * 4 + px) * 4 + 3] = 255;
                    }
                    else
                    {
                        block[(py * 4 + px) * 4 + 0] = 0;
                        block[(py * 4 + px) * 4 + 1] = 0;
                        block[(py * 4 + px) * 4 + 2] = 0;
                        block[(py * 4 + px) * 4 + 3] = 255;
                    }
                }
            }

            uint8_t *outBlock = &compressedData[(by * blockCountX + bx) * 8];
            stb_compress_dxt_block(outBlock, block, 0, STB_DXT_NORMAL);
        }
    }

    return compressedData;
}

inline void testCompressedTextures()
{
    CFW_LOG_DEBUG("=== Start testing texture compression formats ===");

    int width, height, channels;
    unsigned char *data = stbi_load(defaultTexturePath.c_str(), &width, &height, &channels, 4);
    if (!data)
    {
        CFW_LOG_ERROR("Failed to load texture: {}", stbi_failure_reason());
        return;
    }

    CFW_LOG_DEBUG("Original texture size: {}x{}, channels: {}", width, height, channels);

    auto compressedData = compressToBC1(data, width, height, 4);
    CFW_LOG_DEBUG("BC1 compressed size: {} bytes", compressedData.size());
    CFW_LOG_DEBUG("Compression ratio: {}:1", (float)(width * height * 4) / compressedData.size());

    try
    {
        CFW_LOG_DEBUG("Testing BC1_RGB_UNORM format...");
        HardwareImageCreateInfo bc1UnormCreateInfo;
        bc1UnormCreateInfo.width = width;
        bc1UnormCreateInfo.height = height;
        bc1UnormCreateInfo.format = ImageFormat::BC1_RGB_UNORM;
        bc1UnormCreateInfo.usage = ImageUsage::SampledImage;
        bc1UnormCreateInfo.arrayLayers = 1;
        bc1UnormCreateInfo.mipLevels = 1;
        // bc1UnormCreateInfo.initialData = compressedData.data();

        HardwareImage textureBC1Unorm(bc1UnormCreateInfo);
        HardwareExecutor tempExecutor;
        tempExecutor << textureBC1Unorm.copyFrom(compressedData.data()) << tempExecutor.commit();
        CFW_LOG_DEBUG("BC1_RGB_UNORM texture created success, descriptor ID: {}", textureBC1Unorm.storeDescriptor());

        CFW_LOG_DEBUG("Testing BC1_RGB_SRGB format...");
        HardwareImageCreateInfo bc1SrgbCreateInfo;
        bc1SrgbCreateInfo.width = width;
        bc1SrgbCreateInfo.height = height;
        bc1SrgbCreateInfo.format = ImageFormat::BC1_RGB_SRGB;
        bc1SrgbCreateInfo.usage = ImageUsage::SampledImage;
        bc1SrgbCreateInfo.arrayLayers = 1;
        bc1SrgbCreateInfo.mipLevels = 1;
        // bc1SrgbCreateInfo.initialData = compressedData.data();

        HardwareImage textureBC1Srgb(bc1SrgbCreateInfo);
        tempExecutor << textureBC1Srgb.copyFrom(compressedData.data()) << tempExecutor.commit();
        CFW_LOG_DEBUG("BC1_RGB_SRGB texture created success, descriptor ID: {}", textureBC1Srgb.storeDescriptor());

        CFW_LOG_DEBUG("=== All compressed format tests passed ===");
    }
    catch (const std::exception &e)
    {
        CFW_LOG_ERROR("Compress texture failed: {}", e.what());
    }

    stbi_image_free(data);
}

// 加载普通纹理
inline TextureLoadResult loadTexture(const std::string &texturePath)
{
    TextureLoadResult result;

    int width, height, channels;
    unsigned char *data = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    if (!data)
    {
        CFW_LOG_ERROR("Failed to load texture '{}': {}", texturePath, stbi_failure_reason());
        return result;
    }

    //CFW_LOG_DEBUG("Loaded texture: {}x{}, channels: {}", width, height, channels);

    HardwareImageCreateInfo createInfo;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.format = ImageFormat::RGBA8_SRGB;
    createInfo.usage = ImageUsage::SampledImage;
    createInfo.arrayLayers = 1;
    createInfo.mipLevels = 1;
    // createInfo.initialData = data;

    result.texture = HardwareImage(createInfo);
    HardwareExecutor tempExecutor;
    tempExecutor << result.texture.copyFrom(data) << tempExecutor.commit();
    result.descriptorID = result.texture.storeDescriptor();
    result.success = true;

    stbi_image_free(data);
    // CFW_LOG_INFO("Texture loaded successfully, descriptor ID: {}", result.descriptorID);

    return result;
}

// 加载BC1压缩纹理
inline TextureLoadResult loadCompressedTexture(const std::string &texturePath, bool useSRGB = true)
{
    TextureLoadResult result;

    int width, height, channels;
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
                  (float)(width * height * 4) / compressedData.size());

    HardwareImageCreateInfo createInfo;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.format = useSRGB ? ImageFormat::BC1_RGB_SRGB : ImageFormat::BC1_RGB_UNORM;
    createInfo.usage = ImageUsage::SampledImage;
    createInfo.arrayLayers = 1;
    createInfo.mipLevels = 1;
    // createInfo.initialData = compressedData.data();

    result.texture = HardwareImage(createInfo);
    HardwareExecutor tempExecutor;
    tempExecutor << result.texture.copyFrom(compressedData.data()) << tempExecutor.commit();
    result.descriptorID = result.texture.storeDescriptor();
    result.success = true;

    //CFW_LOG_INFO("Compressed texture loaded successfully, descriptor ID: {}", result.descriptorID);

    return result;
}

// 加载带有 mipmap 和 array layers 的纹理
inline TextureLoadResult loadTextureWithMipmapAndLayers(const std::string &texturePath,
                                                        int arrayLayers,
                                                        int mipLevels,
                                                        int viewLayer = 0,
                                                        int viewMip = 0)
{
    TextureLoadResult result;

    int width, height, channels;
    unsigned char *data = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    if (!data)
    {
        CFW_LOG_ERROR("Failed to load texture '{}': {}", texturePath, stbi_failure_reason());
        return result;
    }

    // CFW_LOG_DEBUG("Loaded texture: {}x{}, creating {} layers with {} mip levels",
    //               width, height, arrayLayers, mipLevels);

    std::vector<unsigned char> layerData;
    size_t layerSize = width * height * 4;
    layerData.resize(layerSize * arrayLayers);

    for (int i = 0; i < arrayLayers; i++)
    {
        memcpy(layerData.data() + layerSize * i, data, layerSize);
    }
    stbi_image_free(data);

    HardwareImageCreateInfo createInfo;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.format = ImageFormat::RGBA8_SRGB;
    createInfo.usage = ImageUsage::SampledImage;
    createInfo.arrayLayers = arrayLayers;
    createInfo.mipLevels = mipLevels;
    // createInfo.initialData = layerData.data();

    result.texture = HardwareImage(createInfo);

    // 获取指定 layer 和 mip 的视图
    auto textureView = result.texture[viewLayer][viewMip];
    HardwareExecutor tempExecutor;
    tempExecutor << textureView.copyFrom(layerData.data()) << tempExecutor.commit();
    result.descriptorID = textureView.storeDescriptor();
    result.success = true;

    CFW_LOG_INFO("Texture with mipmap/layers loaded successfully, descriptor ID: {}", result.descriptorID);

    return result;
}
