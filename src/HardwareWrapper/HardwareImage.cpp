#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareContext.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include <bit>

struct ImageFormatInfo
{
    VkFormat vkFormat;
    float pixelSize;
    bool isCompressed;
};

ImageFormatInfo convertImageFormat(HardwareImage::Format format)
{
    using enum HardwareImage::Format;
    switch (format)
    {
    // Uncompressed formats
    case RGBA8_UNORM: return {VK_FORMAT_R8G8B8A8_UNORM, 4, false};
    case R32_FLOAT: return {VK_FORMAT_R32_SFLOAT, 4, false};
    case BC7_UNORM: return {VK_FORMAT_BC7_UNORM_BLOCK, 1, true};
    case BC7_SRGB: return {VK_FORMAT_BC7_SRGB_BLOCK, 1, true};
    case RGBA8_UINT:
        return {VK_FORMAT_R8G8B8A8_UINT, 4, false};
    case RGBA8_SINT:
        return {VK_FORMAT_R8G8B8A8_SINT, 4, false};
    case RGBA8_SRGB:
        return {VK_FORMAT_R8G8B8A8_SRGB, 4, false};
    case RGBA16_UINT:
        return {VK_FORMAT_R16G16B16A16_UINT, 8, false};
    case RGBA16_SINT:
        return {VK_FORMAT_R16G16B16A16_SINT, 8, false};
    case RGBA16_FLOAT:
        return {VK_FORMAT_R16G16B16A16_SFLOAT, 8, false};
    case RGBA32_UINT:
        return {VK_FORMAT_R32G32B32A32_UINT, 16, false};
    case RGBA32_SINT:
        return {VK_FORMAT_R32G32B32A32_SINT, 16, false};
    case RGBA32_FLOAT:
        return {VK_FORMAT_R32G32B32A32_SFLOAT, 16, false};
    case RG32_FLOAT:
        return {VK_FORMAT_R32G32_SFLOAT, 8, false};
    case D16_UNORM:
        return {VK_FORMAT_D16_UNORM, 2, false};
    case D32_FLOAT:
        return {VK_FORMAT_D32_SFLOAT, 4, false};

    // BC compressed formats - Desktop
    case BC1_RGB_UNORM:
        return {VK_FORMAT_BC1_RGB_UNORM_BLOCK, 0.5f, true};
    case BC1_RGB_SRGB:
        return {VK_FORMAT_BC1_RGB_SRGB_BLOCK, 0.5f, true};
    case BC2_RGBA_UNORM:
        return {VK_FORMAT_BC2_UNORM_BLOCK, 1.0f, true};
    case BC2_RGBA_SRGB:
        return {VK_FORMAT_BC2_SRGB_BLOCK, 1.0f, true};
    case BC3_RGBA_UNORM:
        return {VK_FORMAT_BC3_UNORM_BLOCK, 1.0f, true};
    case BC3_RGBA_SRGB:
        return {VK_FORMAT_BC3_SRGB_BLOCK, 1.0f, true};
    case BC4_R_UNORM:
        return {VK_FORMAT_BC4_UNORM_BLOCK, 0.5f, true};
    case BC4_R_SNORM:
        return {VK_FORMAT_BC4_SNORM_BLOCK, 0.5f, true};
    case BC5_RG_UNORM:
        return {VK_FORMAT_BC5_UNORM_BLOCK, 1.0f, true};
    case BC5_RG_SNORM:
        return {VK_FORMAT_BC5_SNORM_BLOCK, 1.0f, true};

    // ASTC compressed formats - Mobile
    case ASTC_4x4_UNORM:
        return {VK_FORMAT_ASTC_4x4_UNORM_BLOCK, 1.0f, true};
    case ASTC_4x4_SRGB:
        return {VK_FORMAT_ASTC_4x4_SRGB_BLOCK, 1.0f, true};

    default:
        return {VK_FORMAT_R8G8B8A8_UNORM, 4, false};
    }
}

VkImageUsageFlags convertImageUsage(HardwareImage::Usage usage, bool isCompressed)
{
    using enum HardwareImage::Usage;
    // 所有图像默认支持传输
    VkImageUsageFlags vkUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    switch (usage)
    {
    case SampledImage:
        vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        // 压缩纹理不支持作为 Render Target
        if (!isCompressed)
        {
            vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        break;

    case SampledStorageImage:
        vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        [[fallthrough]];
    case StorageImage:
        // 修正：压缩纹理不支持 STORAGE_BIT (imageLoad/Store)
        // 如果强行请求 Storage 且是压缩格式，为了安全降级为 Sampled
        if (!isCompressed)
        {
            vkUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
            vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        else
        {
            // 压缩格式无法作为 Storage，降级以避免 Crash
            vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        break;

    case DepthImage:
        vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        break;
    default:
        break;
    }

    return vkUsage;
}

namespace {
uint32_t uploadByteCount(const ResourceManager::ImageHardwareWrap& image, uint32_t mip)
{
    const uint64_t width = std::max(1u, image.imageSize.x >> mip);
    const uint64_t height = std::max(1u, image.imageSize.y >> mip);
    const bool compressed = image.pixelSize < 2;
    const uint64_t columns = compressed ? (width + 3) / 4 : width;
    const uint64_t rows = compressed ? (height + 3) / 4 : height;
    const auto blockBytes = static_cast<uint32_t>(image.pixelSize * (compressed ? 16 : 1));
    if (!blockBytes || columns * rows > UINT32_MAX / blockBytes)
        throw std::length_error("Image upload exceeds the supported staging buffer size");
    return static_cast<uint32_t>(columns * rows * blockBytes);
}

void releaseImage(ResourceManager::ImageHardwareWrap* image)
{
    image->resourceManager->destroyImage(*image);
    delete image;
}
}

HardwareImage::HardwareImage(const CreateInfo& createInfo)
{
    if (!createInfo.width || !createInfo.height)
        throw std::invalid_argument("Image extent must be nonzero");
    if (createInfo.arrayLayers <= 0 || createInfo.mipLevels <= 0 ||
        static_cast<unsigned>(createInfo.mipLevels) > std::bit_width(std::max(createInfo.width, createInfo.height)))
        throw std::invalid_argument("Invalid image layer or mip count");
    if (createInfo.cube && (createInfo.width != createInfo.height || createInfo.arrayLayers != 6))
        throw std::invalid_argument("A cubemap requires six square faces");
    const auto [vkFormat, pixelSize, isCompressed] = convertImageFormat(createInfo.format);
    auto& manager = getHardwareContext().getMainDevice()->resourceManager;
    impl_.reset(new Impl(manager.createImage(
        ktm::uvec2(createInfo.width, createInfo.height), vkFormat, pixelSize,
        convertImageUsage(createInfo.usage, isCompressed), createInfo.arrayLayers, createInfo.mipLevels,
        VK_IMAGE_TILING_OPTIMAL, createInfo.cube)), releaseImage);
    impl_->linearFiltering = createInfo.linearFiltering;
}

HardwareImage::HardwareImage(uint32_t width, uint32_t height, Format format, Usage usage, int arrayLayers)
    : HardwareImage(CreateInfo{width, height, format, usage, arrayLayers}) {}

HardwareImage HardwareImage::operator[](uint32_t index)
{
    HardwareImage result;
    if (!impl_) return result;
    auto* parent = impl_.get();
    result.impl_.reset(new Impl{.resourceManager = parent->resourceManager}, releaseImage);
    auto* view = result.impl_.get();
    view->device = parent->device;
    view->imageFormat = parent->imageFormat;
    view->linearFiltering = parent->linearFiltering;
    view->pixelSize = parent->pixelSize;
    view->imageLayout = parent->imageLayout;
    view->aspectMask = parent->aspectMask;
    view->clearValue = parent->clearValue;
    view->imageUsage = parent->imageUsage;
    view->imageHandle = parent->imageHandle;
    view->imageAlloc = parent->imageAlloc;
    view->imageAllocInfo = parent->imageAllocInfo;
    view->allocationOwner = parent->allocationOwner;
    view->baseMipLevel = parent->baseMipLevel;
    view->baseArrayLayer = parent->baseArrayLayer;
    if (parent->arrayLayers == 1)
    {
        if (index >= parent->mipLevels) throw std::out_of_range("Image mip view exceeds its parent");
        view->baseMipLevel += index;
        view->imageView = parent->resourceManager->createImageView(*parent, 0, index);
        view->imageSize = ktm::uvec2(std::max(1u, parent->imageSize.x >> index), std::max(1u, parent->imageSize.y >> index));
        view->arrayLayers = 1;
        view->mipLevels = 1;
    }
    else if (parent->arrayLayers > 1)
    {
        if (index >= parent->arrayLayers) throw std::out_of_range("Image layer view exceeds its parent");
        view->baseArrayLayer += index;
        view->imageView = parent->resourceManager->createImageView(*parent, index, static_cast<uint32_t>(-1));
        view->imageSize = parent->imageSize;
        view->arrayLayers = 1;
        view->mipLevels = parent->mipLevels;
    }
    return result;
}

HardwareImage::operator bool() const
{
    return impl_ && impl_->imageHandle != VK_NULL_HANDLE;
}

HardwareDescriptor HardwareImage::storeReadDescriptor() const { return {*this, EmbeddedShader::Ast::AccessPermissions::ReadOnly}; }
HardwareDescriptor HardwareImage::storeWriteDescriptor() const { return {*this, EmbeddedShader::Ast::AccessPermissions::WriteOnly}; }
HardwareDescriptor HardwareImage::storeReadWriteDescriptor() const { return {*this, EmbeddedShader::Ast::AccessPermissions::ReadAndWrite}; }
HardwareDescriptor HardwareImage::storeSampledDescriptor() const { return {*this, EmbeddedShader::Ast::AccessPermissions::ReadOnly, true}; }

void HardwareImage::setClearColor(float r, float g, float b, float a)
{
    if (impl_) impl_->clearValue.color = {{r, g, b, a}};
}

HardwareExecutor::Command HardwareImage::copyFrom(const void *inputData, uint32_t imageLayer, uint32_t imageMip) const
{
    if (!inputData || !*this) return HardwareExecutor::Command(nullptr);
    if (imageMip >= impl_->mipLevels) return HardwareExecutor::Command(nullptr);
    HardwareBuffer stagingBuffer(uploadByteCount(*impl_, imageMip), HardwareBuffer::Usage::StorageBuffer, inputData);
    return stagingBuffer.copyTo(*this, 0, imageLayer, imageMip);
}
