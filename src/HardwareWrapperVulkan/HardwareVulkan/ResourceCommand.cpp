#include "ResourceAccess.h"
#include "ResourceCommand.h"

namespace
{
void validateBufferImageRegion(const ResourceManager::BufferHardwareWrap& buffer,
                              const ResourceManager::ImageHardwareWrap& image,
                              uint32_t mip, uint32_t layer, uint32_t layers, VkDeviceSize offset)
{
    if (mip >= image.mipLevels || mip >= 32 || layer >= image.arrayLayers ||
        layers == 0 || layers > image.arrayLayers - layer)
        throw std::out_of_range("Image copy subresource is out of range");
    const uint64_t width = std::max(1u, image.imageSize.x >> mip);
    const uint64_t height = std::max(1u, image.imageSize.y >> mip);
    // All compressed formats exposed by the legacy API use 4x4 blocks.
    const bool compressed = image.pixelSize < 2.0f;
    const uint64_t blockBytes = static_cast<uint64_t>(image.pixelSize * (compressed ? 16 : 1));
    const uint64_t blockCount = compressed ? ((width + 3) / 4) * ((height + 3) / 4) : width * height;
    const uint64_t capacity = uint64_t(buffer.elementCount) * buffer.elementSize;
    if (blockBytes == 0 || offset % blockBytes != 0 || offset % 4 != 0)
        throw std::invalid_argument("Image copy buffer offset is not aligned");
    if (offset > capacity || blockCount > (capacity - offset) / blockBytes / layers)
        throw std::out_of_range("Image copy exceeds the buffer size");
}
}

// CopyBufferCommand implementations
CopyBufferCommand::CopyBufferCommand(ResourceManager::BufferHardwareWrap &src, ResourceManager::BufferHardwareWrap &dst,
                                   VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize size)
    : srcBuffer(src), dstBuffer(dst), srcOffset(srcOffset), dstOffset(dstOffset), size(size)
{
    const auto srcSize = uint64_t(src.elementCount) * src.elementSize;
    const auto dstSize = uint64_t(dst.elementCount) * dst.elementSize;
    if (srcOffset > srcSize || dstOffset > dstSize)
        throw std::out_of_range("Buffer copy offset exceeds the buffer size");
    if (this->size == 0) this->size = srcSize - srcOffset;
    if (this->size == 0 || this->size > srcSize - srcOffset || this->size > dstSize - dstOffset)
        throw std::out_of_range("Buffer copy range exceeds the buffer size");
    if (src.bufferHandle == dst.bufferHandle &&
        srcOffset < dstOffset + this->size && dstOffset < srcOffset + this->size)
        throw std::invalid_argument("Overlapping buffer copy ranges are not supported");
}

CommandRecordVulkan::ExecutorType CopyBufferCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyBufferCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    VkBufferCopy region{srcOffset, dstOffset, size};
    vkCmdCopyBuffer(hardwareExecutor.currentRecordQueue->commandBuffer,
                    srcBuffer.bufferHandle, dstBuffer.bufferHandle, 1, &region);
}

void CopyBufferCommand::resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList& result)
{
    result.buffer(srcBuffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    result.buffer(dstBuffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

// CopyImageCommand implementations
CopyImageCommand::CopyImageCommand(ResourceManager::ImageHardwareWrap &srcImg,
                                   ResourceManager::ImageHardwareWrap &dstImg,
                                   uint32_t srcLayerValue,
                                   uint32_t dstLayerValue,
                                   uint32_t srcMipValue,
                                   uint32_t dstMipValue)
    : srcImage(srcImg),
      dstImage(dstImg),
      srcLayer(srcLayerValue),
      dstLayer(dstLayerValue),
      srcMip(srcMipValue),
      dstMip(dstMipValue)
{
}

CommandRecordVulkan::ExecutorType CopyImageCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

bool CopyImageCommand::valid() const
{
    return srcImage.imageFormat == dstImage.imageFormat &&
           srcLayer < srcImage.arrayLayers && dstLayer < dstImage.arrayLayers &&
           srcMip < srcImage.mipLevels && dstMip < dstImage.mipLevels;
}

void CopyImageCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    if (!valid()) return;

    VkImageCopy region{};
    region.srcSubresource = {srcImage.aspectMask, srcImage.baseMipLevel + srcMip, srcImage.baseArrayLayer + srcLayer, 1};
    region.dstSubresource = {dstImage.aspectMask, dstImage.baseMipLevel + dstMip, dstImage.baseArrayLayer + dstLayer, 1};
    region.extent = {
        std::max(1u, std::min(srcImage.imageSize.x >> srcMip, dstImage.imageSize.x >> dstMip)),
        std::max(1u, std::min(srcImage.imageSize.y >> srcMip, dstImage.imageSize.y >> dstMip)), 1};
    vkCmdCopyImage(hardwareExecutor.currentRecordQueue->commandBuffer,
                   srcImage.imageHandle, srcImage.imageLayout, dstImage.imageHandle, dstImage.imageLayout, 1, &region);

    if ((srcImage.imageUsage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0 &&
        srcImage.imageLayout != VK_IMAGE_LAYOUT_GENERAL)
    {
        hardwareExecutor.hardwareContext->resourceManager.transitionImageLayout(
            hardwareExecutor.currentRecordQueue->commandBuffer,
            srcImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }

    if ((dstImage.imageUsage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0 &&
        dstImage.imageLayout != VK_IMAGE_LAYOUT_GENERAL)
    {
        hardwareExecutor.hardwareContext->resourceManager.transitionImageLayout(
            hardwareExecutor.currentRecordQueue->commandBuffer,
            dstImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
}

void CopyImageCommand::resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList& result)
{
    if (!valid()) return;

    result.image(srcImage, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, {srcImage.aspectMask, srcImage.baseMipLevel + srcMip, 1, srcImage.baseArrayLayer + srcLayer, 1});
    result.image(dstImage, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {dstImage.aspectMask, dstImage.baseMipLevel + dstMip, 1, dstImage.baseArrayLayer + dstLayer, 1});
}

// CopyBufferToImageCommand implementations
CopyBufferToImageCommand::CopyBufferToImageCommand(ResourceManager::BufferHardwareWrap &srcBuf,
                                                   ResourceManager::ImageHardwareWrap &dstImg,
                                                   uint32_t mip, uint32_t layer, VkDeviceSize offset, uint32_t layers)
    : srcBuffer(srcBuf), dstImage(dstImg), mipLevel(mip), imageLayer(layer),
      layerCount(layers ? layers : dstImg.arrayLayers), bufferOffset(offset)
{
    validateBufferImageRegion(srcBuffer, dstImage, mipLevel, imageLayer, layerCount, bufferOffset);
}

CommandRecordVulkan::ExecutorType CopyBufferToImageCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyBufferToImageCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.imageSubresource = {dstImage.aspectMask, dstImage.baseMipLevel + mipLevel, dstImage.baseArrayLayer + imageLayer, layerCount};
    region.imageExtent = {std::max(1u, dstImage.imageSize.x >> mipLevel),
                          std::max(1u, dstImage.imageSize.y >> mipLevel), 1};
    vkCmdCopyBufferToImage(hardwareExecutor.currentRecordQueue->commandBuffer,
                           srcBuffer.bufferHandle, dstImage.imageHandle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if ((dstImage.imageUsage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0 &&
        dstImage.imageLayout != VK_IMAGE_LAYOUT_GENERAL)
    {
        hardwareExecutor.hardwareContext->resourceManager.transitionImageLayout(
            hardwareExecutor.currentRecordQueue->commandBuffer,
            dstImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
}

void CopyBufferToImageCommand::resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList& result)
{
    result.buffer(srcBuffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    result.image(dstImage, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {dstImage.aspectMask, dstImage.baseMipLevel + mipLevel, 1, dstImage.baseArrayLayer + imageLayer, layerCount});
}

// CopyImageToBufferCommand implementations
CopyImageToBufferCommand::CopyImageToBufferCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::BufferHardwareWrap &dstBuf,
                                                 uint32_t mip, uint32_t layer, VkDeviceSize offset, uint32_t layers)
    : srcImage(srcImg), dstBuffer(dstBuf), mipLevel(mip), imageLayer(layer),
      layerCount(layers ? layers : srcImg.arrayLayers), bufferOffset(offset)
{
    validateBufferImageRegion(dstBuffer, srcImage, mipLevel, imageLayer, layerCount, bufferOffset);
}

CommandRecordVulkan::ExecutorType CopyImageToBufferCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyImageToBufferCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.imageSubresource = {srcImage.aspectMask, srcImage.baseMipLevel + mipLevel, srcImage.baseArrayLayer + imageLayer, layerCount};
    region.imageExtent = {std::max(1u, srcImage.imageSize.x >> mipLevel),
                          std::max(1u, srcImage.imageSize.y >> mipLevel), 1};
    vkCmdCopyImageToBuffer(hardwareExecutor.currentRecordQueue->commandBuffer,
                           srcImage.imageHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dstBuffer.bufferHandle, 1, &region);

    if ((srcImage.imageUsage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0 &&
        srcImage.imageLayout != VK_IMAGE_LAYOUT_GENERAL)
    {
        hardwareExecutor.hardwareContext->resourceManager.transitionImageLayout(
            hardwareExecutor.currentRecordQueue->commandBuffer,
            srcImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
}

void CopyImageToBufferCommand::resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList& result)
{
    result.image(srcImage, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, {srcImage.aspectMask, srcImage.baseMipLevel + mipLevel, 1, srcImage.baseArrayLayer + imageLayer, layerCount});
    result.buffer(dstBuffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

// BlitImageCommand implementations
BlitImageCommand::BlitImageCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::ImageHardwareWrap &dstImg)
    : srcImage(srcImg), dstImage(dstImg)
{
}

CommandRecordVulkan::ExecutorType BlitImageCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Graphics;
}

void BlitImageCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    hardwareExecutor.hardwareContext->resourceManager.blitImage(hardwareExecutor.currentRecordQueue->commandBuffer, srcImage, dstImage);

    if ((srcImage.imageUsage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0 &&
        srcImage.imageLayout != VK_IMAGE_LAYOUT_GENERAL)
    {
        hardwareExecutor.hardwareContext->resourceManager.transitionImageLayout(
            hardwareExecutor.currentRecordQueue->commandBuffer,
            srcImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }

    if ((dstImage.imageUsage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0 &&
        dstImage.imageLayout != VK_IMAGE_LAYOUT_GENERAL)
    {
        hardwareExecutor.hardwareContext->resourceManager.transitionImageLayout(
            hardwareExecutor.currentRecordQueue->commandBuffer,
            dstImage,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
}

void BlitImageCommand::resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList& result)
{
    result.image(srcImage, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, {srcImage.aspectMask, srcImage.baseMipLevel, 1, srcImage.baseArrayLayer, 1});
    result.image(dstImage, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {dstImage.aspectMask, dstImage.baseMipLevel, 1, dstImage.baseArrayLayer, 1});
}

// TransitionImageLayoutCommand implementations
TransitionImageLayoutCommand::TransitionImageLayoutCommand(ResourceManager::ImageHardwareWrap &image, VkImageLayout imageLayout, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask)
    : image(image), imageLayout(imageLayout), dstStageMask(dstStageMask), dstAccessMask(dstAccessMask)
{
}

CommandRecordVulkan::ExecutorType TransitionImageLayoutCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void TransitionImageLayoutCommand::commitCommand(HardwareExecutorVulkan&)
{
    // The executor emits the layout transition from resolveResourceUses().
}

void TransitionImageLayoutCommand::resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList& result)
{
    result.image(image, dstStageMask, dstAccessMask, imageLayout, imageViewRange(image));
}
