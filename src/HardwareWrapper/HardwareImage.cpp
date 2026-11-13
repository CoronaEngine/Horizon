#include "Horizon_old.h"
#include "HardwareCommands_old.h"
#include "HardwareWrapperVulkan/HardwareContext.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"

struct ImageFormatInfo
{
    VkFormat vkFormat;
    float pixelSize;
    bool isCompressed;
};

ImageFormatInfo convertImageFormat(ImageFormat format)
{
    switch (format)
    {
    // Uncompressed formats
    case ImageFormat::RGBA8_UNORM: return {VK_FORMAT_R8G8B8A8_UNORM, 4, false};
    case ImageFormat::R32_FLOAT: return {VK_FORMAT_R32_SFLOAT, 4, false};
    case ImageFormat::BC7_UNORM: return {VK_FORMAT_BC7_UNORM_BLOCK, 1, true};
    case ImageFormat::BC7_SRGB: return {VK_FORMAT_BC7_SRGB_BLOCK, 1, true};
    case ImageFormat::RGBA8_UINT:
        return {VK_FORMAT_R8G8B8A8_UINT, 4, false};
    case ImageFormat::RGBA8_SINT:
        return {VK_FORMAT_R8G8B8A8_SINT, 4, false};
    case ImageFormat::RGBA8_SRGB:
        return {VK_FORMAT_R8G8B8A8_SRGB, 4, false};
    case ImageFormat::RGBA16_UINT:
        return {VK_FORMAT_R16G16B16A16_UINT, 8, false};
    case ImageFormat::RGBA16_SINT:
        return {VK_FORMAT_R16G16B16A16_SINT, 8, false};
    case ImageFormat::RGBA16_FLOAT:
        return {VK_FORMAT_R16G16B16A16_SFLOAT, 8, false};
    case ImageFormat::RGBA32_UINT:
        return {VK_FORMAT_R32G32B32A32_UINT, 16, false};
    case ImageFormat::RGBA32_SINT:
        return {VK_FORMAT_R32G32B32A32_SINT, 16, false};
    case ImageFormat::RGBA32_FLOAT:
        return {VK_FORMAT_R32G32B32A32_SFLOAT, 16, false};
    case ImageFormat::RG32_FLOAT:
        return {VK_FORMAT_R32G32_SFLOAT, 8, false};
    case ImageFormat::D16_UNORM:
        return {VK_FORMAT_D16_UNORM, 2, false};
    case ImageFormat::D32_FLOAT:
        return {VK_FORMAT_D32_SFLOAT, 4, false};

    // BC compressed formats - Desktop
    case ImageFormat::BC1_RGB_UNORM:
        return {VK_FORMAT_BC1_RGB_UNORM_BLOCK, 0.5f, true};
    case ImageFormat::BC1_RGB_SRGB:
        return {VK_FORMAT_BC1_RGB_SRGB_BLOCK, 0.5f, true};
    case ImageFormat::BC2_RGBA_UNORM:
        return {VK_FORMAT_BC2_UNORM_BLOCK, 1.0f, true};
    case ImageFormat::BC2_RGBA_SRGB:
        return {VK_FORMAT_BC2_SRGB_BLOCK, 1.0f, true};
    case ImageFormat::BC3_RGBA_UNORM:
        return {VK_FORMAT_BC3_UNORM_BLOCK, 1.0f, true};
    case ImageFormat::BC3_RGBA_SRGB:
        return {VK_FORMAT_BC3_SRGB_BLOCK, 1.0f, true};
    case ImageFormat::BC4_R_UNORM:
        return {VK_FORMAT_BC4_UNORM_BLOCK, 0.5f, true};
    case ImageFormat::BC4_R_SNORM:
        return {VK_FORMAT_BC4_SNORM_BLOCK, 0.5f, true};
    case ImageFormat::BC5_RG_UNORM:
        return {VK_FORMAT_BC5_UNORM_BLOCK, 1.0f, true};
    case ImageFormat::BC5_RG_SNORM:
        return {VK_FORMAT_BC5_SNORM_BLOCK, 1.0f, true};

    // ASTC compressed formats - Mobile
    case ImageFormat::ASTC_4x4_UNORM:
        return {VK_FORMAT_ASTC_4x4_UNORM_BLOCK, 1.0f, true};
    case ImageFormat::ASTC_4x4_SRGB:
        return {VK_FORMAT_ASTC_4x4_SRGB_BLOCK, 1.0f, true};

    default:
        return {VK_FORMAT_R8G8B8A8_UNORM, 4, false};
    }
}

VkImageUsageFlags convertImageUsage(ImageUsage usage, bool isCompressed)
{
    // 所有图像默认支持传输
    VkImageUsageFlags vkUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    switch (usage)
    {
    case ImageUsage::SampledImage:
        vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        // 压缩纹理不支持作为 Render Target
        if (!isCompressed)
        {
            vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        break;

    case ImageUsage::SampledStorageImage:
        vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        [[fallthrough]];
    case ImageUsage::StorageImage:
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

    case ImageUsage::DepthImage:
        vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        break;
    default:
        break;
    }

    return vkUsage;
}

namespace {
void retainImage(uintptr_t id)
{
    if (id) ++legacyImageFromId(id)->refCount;
}

void releaseImage(uintptr_t id)
{
    if (!id) return;
    auto* resource = legacyImageFromId(id);
    if (--resource->refCount == 0)
    {
        resource->resourceManager->destroyImage(*resource);
        delete resource;
    }
}
}

HardwareImage::HardwareImage() = default;

HardwareImage::HardwareImage(const HardwareImageCreateInfo& createInfo)
{
    if (createInfo.cube && (createInfo.width != createInfo.height || createInfo.arrayLayers != 6))
        throw std::invalid_argument("A cubemap requires six square faces");
    const auto [vkFormat, pixelSize, isCompressed] = convertImageFormat(createInfo.format);
    auto resource = std::make_unique<ResourceManager::ImageHardwareWrap>();
    *resource = getHardwareContext().getMainDevice()->resourceManager.createImage(
        ktm::uvec2(createInfo.width, createInfo.height), vkFormat, pixelSize,
        convertImageUsage(createInfo.usage, isCompressed), createInfo.arrayLayers, createInfo.mipLevels, VK_IMAGE_TILING_OPTIMAL, createInfo.cube);
    resource->linearFiltering = createInfo.linearFiltering;
    imageID = reinterpret_cast<uintptr_t>(resource.release());
}

HardwareImage::HardwareImage(uint32_t width, uint32_t height, ImageFormat format, ImageUsage usage, int arrayLayers, void* imageData)
    : HardwareImage([&] {
        HardwareImageCreateInfo info(width, height, format);
        info.usage = usage;
        info.arrayLayers = arrayLayers;
        return info;
    }())
{
    if (imageData)
    {
        HardwareExecutorVulkan executor;
        auto* image = legacyImageFromId(imageID);
        HardwareBuffer staging(image->imageSize.x * image->imageSize.y * image->pixelSize,
                               BufferUsage::StorageBuffer, imageData);
        CopyBufferToImageCommand command(*legacyBufferFromId(staging.getBufferID()), *image, 0);
        executor << &command;
        executor.commit();
    }
}

HardwareImage::HardwareImage(const HardwareImage& other) : imageID(other.imageID)
{
    retainImage(imageID);
}

HardwareImage::HardwareImage(HardwareImage&& other) noexcept : imageID(std::exchange(other.imageID, 0)) {}

HardwareImage::~HardwareImage()
{
    releaseImage(imageID);
}

HardwareImage& HardwareImage::operator=(const HardwareImage& other)
{
    if (imageID == other.imageID) return *this;
    retainImage(other.imageID);
    releaseImage(imageID);
    imageID = other.imageID;
    return *this;
}

HardwareImage& HardwareImage::operator=(HardwareImage&& other) noexcept
{
    if (this == &other) return *this;
    releaseImage(imageID);
    imageID = std::exchange(other.imageID, 0);
    return *this;
}

HardwareImage HardwareImage::operator[](uint32_t index)
{
    HardwareImage result;
    if (!imageID) return result;
    auto* parent = legacyImageFromId(imageID);
    auto view = std::make_unique<ResourceManager::ImageHardwareWrap>();
    view->device = parent->device;
    view->resourceManager = parent->resourceManager;
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
    result.imageID = reinterpret_cast<uintptr_t>(view.release());
    return result;
}

HardwareImage::operator bool() const
{
    return imageID && legacyImageFromId(imageID)->imageHandle != VK_NULL_HANDLE;
}

uint32_t HardwareImage::storeSampledDescriptor() const
{
    auto* image = legacyImageFromId(imageID);
    return image->resourceManager->storeSampledDescriptor(*image);
}

uint32_t HardwareImage::storeDescriptor() const
{
    auto imageHandle = legacyImageFromId(imageID);
    return imageHandle->resourceManager->storeDescriptor(*imageHandle);
}

void HardwareImage::setClearColor(float r, float g, float b, float a)
{
    auto const self_image_id = imageID;
    if (self_image_id == 0) return;
    auto handle = legacyImageFromId(self_image_id);
    handle->clearValue.color = {{r, g, b, a}};
}

ImageCopyCommand HardwareImage::copyTo(const HardwareImage &dst,
                                       uint32_t srcLayer, uint32_t dstLayer,
                                       uint32_t srcMip, uint32_t dstMip) const
{
    return ImageCopyCommand(*this, dst, srcLayer, dstLayer, srcMip, dstMip);
}

ImageToBufferCommand HardwareImage::copyTo(const HardwareBuffer &dst,
                                           uint32_t imageLayer,
                                           uint32_t imageMip,
                                           uint64_t bufferOffset) const
{
    return ImageToBufferCommand(*this, dst, imageLayer, imageMip, bufferOffset);
}

BufferToImageCommand HardwareImage::copyFrom(const void *inputData,
                                             uint32_t imageLayer,
                                             uint32_t imageMip) const
{
    if (inputData == nullptr)
    {
        return BufferToImageCommand();
    }

    uint64_t bufferSize = 0;

    {
        auto const imageHandle = legacyImageFromId(imageID);
        if (imageMip >= imageHandle->mipLevels)
        {
            return BufferToImageCommand();
        }
        const uint32_t width = std::max(1u, imageHandle->imageSize.x >> imageMip);
        const uint32_t height = std::max(1u, imageHandle->imageSize.y >> imageMip);

        // 判断是否是压缩格式 (压缩格式的 pixelSize < 2.0f)
        const bool isCompressed = imageHandle->pixelSize < 2.0f;

        if (isCompressed)
        {
            const uint32_t blockWidth = 4;
            const uint32_t blockHeight = 4;
            // 向上取整计算 Block 数量
            uint32_t widthInBlocks = (width + blockWidth - 1) / blockWidth;
            uint32_t heightInBlocks = (height + blockHeight - 1) / blockHeight;
            // 计算每个 Block 的字节数 (例如 BC1: 0.5 * 16 = 8 bytes)
            uint32_t bytesPerBlock = static_cast<uint32_t>(imageHandle->pixelSize * 16.0f);
            bufferSize = widthInBlocks * heightInBlocks * bytesPerBlock;
        }
        else
        {
            bufferSize = width * height * static_cast<uint32_t>(imageHandle->pixelSize);
        }
    }

    HardwareBuffer stagingBuffer(bufferSize, BufferUsage::StorageBuffer, inputData);
    auto cmd = BufferToImageCommand(std::move(stagingBuffer), *this, 0, imageLayer, imageMip);
    return cmd;
}
