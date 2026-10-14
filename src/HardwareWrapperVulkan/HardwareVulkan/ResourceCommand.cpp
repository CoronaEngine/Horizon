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
    executorType = ExecutorType::Transfer;
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

CommandRecordVulkan::RequiredBarriers CopyBufferCommand::getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor)
{
    CommandRecordVulkan::RequiredBarriers requiredBarriers;

    {
        VkBufferMemoryBarrier2 srcBufferBarrier{};
        srcBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        srcBufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        srcBufferBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        srcBufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        srcBufferBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        srcBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBufferBarrier.buffer = srcBuffer.bufferHandle;
        srcBufferBarrier.offset = 0;
        srcBufferBarrier.size = VK_WHOLE_SIZE;
        srcBufferBarrier.pNext = nullptr;

        requiredBarriers.bufferBarriers.push_back(srcBufferBarrier);
    }

    {
        VkBufferMemoryBarrier2 dstBufferBarrier{};
        dstBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        dstBufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        dstBufferBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        dstBufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        dstBufferBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        dstBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBufferBarrier.buffer = dstBuffer.bufferHandle;
        dstBufferBarrier.offset = 0;
        dstBufferBarrier.size = VK_WHOLE_SIZE;
        dstBufferBarrier.pNext = nullptr;

        requiredBarriers.bufferBarriers.push_back(dstBufferBarrier);
    }

    return requiredBarriers;
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
    executorType = ExecutorType::Transfer;
}

CommandRecordVulkan::ExecutorType CopyImageCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyImageCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    if (srcImage.imageFormat != dstImage.imageFormat ||
        srcLayer >= std::max(1u, srcImage.arrayLayers) ||
        dstLayer >= std::max(1u, dstImage.arrayLayers) ||
        srcMip >= std::max(1u, srcImage.mipLevels) ||
        dstMip >= std::max(1u, dstImage.mipLevels))
    {
        return;
    }

    hardwareExecutor.hardwareContext->resourceManager.copyImage(hardwareExecutor.currentRecordQueue->commandBuffer,
                                                                srcImage,
                                                                dstImage,
                                                                srcLayer,
                                                                dstLayer,
                                                                srcMip,
                                                                dstMip);

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

CommandRecordVulkan::RequiredBarriers CopyImageCommand::getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor)
{
    CommandRecordVulkan::RequiredBarriers requiredBarriers;

    if (srcImage.imageFormat != dstImage.imageFormat ||
        srcLayer >= std::max(1u, srcImage.arrayLayers) ||
        dstLayer >= std::max(1u, dstImage.arrayLayers) ||
        srcMip >= std::max(1u, srcImage.mipLevels) ||
        dstMip >= std::max(1u, dstImage.mipLevels))
    {
        return requiredBarriers;
    }

    {
        VkImageMemoryBarrier2 srcImageBarrier{};
        srcImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        srcImageBarrier.pNext = nullptr;
        srcImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        srcImageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        srcImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        srcImageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        srcImageBarrier.oldLayout = srcImage.imageLayout;
        srcImageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcImageBarrier.image = srcImage.imageHandle;
        srcImageBarrier.subresourceRange.aspectMask = srcImage.aspectMask;
        srcImageBarrier.subresourceRange.baseMipLevel = 0;
        srcImageBarrier.subresourceRange.levelCount = std::max(1u, srcImage.mipLevels);
        srcImageBarrier.subresourceRange.baseArrayLayer = 0;
        srcImageBarrier.subresourceRange.layerCount = std::max(1u, srcImage.arrayLayers);

        srcImage.imageLayout = srcImageBarrier.newLayout;

        requiredBarriers.imageBarriers.push_back(srcImageBarrier);
    }

    {
        VkImageMemoryBarrier2 dstImageBarrier{};
        dstImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        dstImageBarrier.pNext = nullptr;
        dstImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        dstImageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        dstImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        dstImageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        dstImageBarrier.oldLayout = dstImage.imageLayout;
        dstImageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dstImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstImageBarrier.image = dstImage.imageHandle;
        dstImageBarrier.subresourceRange.aspectMask = dstImage.aspectMask;
        dstImageBarrier.subresourceRange.baseMipLevel = 0;
        dstImageBarrier.subresourceRange.levelCount = std::max(1u, dstImage.mipLevels);
        dstImageBarrier.subresourceRange.baseArrayLayer = 0;
        dstImageBarrier.subresourceRange.layerCount = std::max(1u, dstImage.arrayLayers);

        dstImage.imageLayout = dstImageBarrier.newLayout;

        requiredBarriers.imageBarriers.push_back(dstImageBarrier);
    }

    return requiredBarriers;
}

// CopyBufferToImageCommand implementations
CopyBufferToImageCommand::CopyBufferToImageCommand(ResourceManager::BufferHardwareWrap &srcBuf,
                                                   ResourceManager::ImageHardwareWrap &dstImg,
                                                   uint32_t mip, uint32_t layer, VkDeviceSize offset, uint32_t layers)
    : srcBuffer(srcBuf), dstImage(dstImg), mipLevel(mip), imageLayer(layer),
      layerCount(layers ? layers : dstImg.arrayLayers), bufferOffset(offset)
{
    validateBufferImageRegion(srcBuffer, dstImage, mipLevel, imageLayer, layerCount, bufferOffset);
    executorType = ExecutorType::Transfer;
}

CommandRecordVulkan::ExecutorType CopyBufferToImageCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyBufferToImageCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.imageSubresource = {dstImage.aspectMask, mipLevel, imageLayer, layerCount};
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

CommandRecordVulkan::RequiredBarriers CopyBufferToImageCommand::getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor)
{
    CommandRecordVulkan::RequiredBarriers requiredBarriers;
    {
        VkBufferMemoryBarrier2 srcBufferBarrier{};
        srcBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        srcBufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        srcBufferBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        srcBufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        srcBufferBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        srcBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBufferBarrier.buffer = srcBuffer.bufferHandle;
        srcBufferBarrier.offset = 0;
        srcBufferBarrier.size = VK_WHOLE_SIZE;
        srcBufferBarrier.pNext = nullptr;

        requiredBarriers.bufferBarriers.push_back(srcBufferBarrier);
    }
    {
        VkImageMemoryBarrier2 dstImageBarrier{};
        dstImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        dstImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        dstImageBarrier.srcAccessMask = 0; // 初始转换，没有源访问
        dstImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        dstImageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        //dstImageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // 从未定义布局开始
        dstImageBarrier.oldLayout = dstImage.imageLayout;
        dstImageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dstImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstImageBarrier.image = dstImage.imageHandle;
        dstImageBarrier.subresourceRange.aspectMask = dstImage.aspectMask;
        // The legacy resource tracks one layout for the entire image.
        dstImageBarrier.subresourceRange.baseMipLevel = 0;
        dstImageBarrier.subresourceRange.levelCount = dstImage.mipLevels;
        dstImageBarrier.subresourceRange.baseArrayLayer = 0;
        dstImageBarrier.subresourceRange.layerCount = dstImage.arrayLayers; // 转换所有图层
        dstImageBarrier.pNext = nullptr;

        dstImage.imageLayout = dstImageBarrier.newLayout;

        requiredBarriers.imageBarriers.push_back(dstImageBarrier);
    }
    return requiredBarriers;
}

// CopyImageToBufferCommand implementations
CopyImageToBufferCommand::CopyImageToBufferCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::BufferHardwareWrap &dstBuf,
                                                 uint32_t mip, uint32_t layer, VkDeviceSize offset, uint32_t layers)
    : srcImage(srcImg), dstBuffer(dstBuf), mipLevel(mip), imageLayer(layer),
      layerCount(layers ? layers : srcImg.arrayLayers), bufferOffset(offset)
{
    validateBufferImageRegion(dstBuffer, srcImage, mipLevel, imageLayer, layerCount, bufferOffset);
    executorType = ExecutorType::Transfer;
}

CommandRecordVulkan::ExecutorType CopyImageToBufferCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyImageToBufferCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.imageSubresource = {srcImage.aspectMask, mipLevel, imageLayer, layerCount};
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

CommandRecordVulkan::RequiredBarriers CopyImageToBufferCommand::getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor)
{
    CommandRecordVulkan::RequiredBarriers requiredBarriers;
    {
        VkImageMemoryBarrier2 srcImageBarrier{};
        srcImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        srcImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        srcImageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        srcImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        srcImageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        srcImageBarrier.oldLayout = srcImage.imageLayout;
        srcImageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcImageBarrier.image = srcImage.imageHandle;
        srcImageBarrier.subresourceRange.aspectMask = srcImage.aspectMask;
        srcImageBarrier.subresourceRange.baseMipLevel = 0;
        srcImageBarrier.subresourceRange.levelCount = srcImage.mipLevels;
        srcImageBarrier.subresourceRange.baseArrayLayer = 0;
        srcImageBarrier.subresourceRange.layerCount = srcImage.arrayLayers;
        srcImageBarrier.pNext = nullptr;

        srcImage.imageLayout = srcImageBarrier.newLayout;

        requiredBarriers.imageBarriers.push_back(srcImageBarrier);
    }
    {
        VkBufferMemoryBarrier2 dstBufferBarrier{};
        dstBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        dstBufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        dstBufferBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        dstBufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        dstBufferBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        dstBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBufferBarrier.buffer = dstBuffer.bufferHandle;
        dstBufferBarrier.offset = 0;
        dstBufferBarrier.size = VK_WHOLE_SIZE;
        dstBufferBarrier.pNext = nullptr;

        requiredBarriers.bufferBarriers.push_back(dstBufferBarrier);
    }
    return requiredBarriers;
}

// BlitImageCommand implementations
BlitImageCommand::BlitImageCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::ImageHardwareWrap &dstImg)
    : srcImage(srcImg), dstImage(dstImg)
{
    executorType = ExecutorType::Graphics;
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

CommandRecordVulkan::RequiredBarriers BlitImageCommand::getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor)
{
    CommandRecordVulkan::RequiredBarriers requiredBarriers;

    {
        VkImageMemoryBarrier2 srcImageBarrier{};
        srcImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        srcImageBarrier.pNext = nullptr;
        srcImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        srcImageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        srcImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        srcImageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        srcImageBarrier.oldLayout = srcImage.imageLayout;
        srcImageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcImageBarrier.image = srcImage.imageHandle;
        srcImageBarrier.subresourceRange.aspectMask = srcImage.aspectMask;
        srcImageBarrier.subresourceRange.baseMipLevel = 0;
        srcImageBarrier.subresourceRange.levelCount = 1;
        srcImageBarrier.subresourceRange.baseArrayLayer = 0;
        srcImageBarrier.subresourceRange.layerCount = 1;

        srcImage.imageLayout = srcImageBarrier.newLayout;

        requiredBarriers.imageBarriers.push_back(srcImageBarrier);
    }

    {
        VkImageMemoryBarrier2 dstImageBarrier{};
        dstImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        dstImageBarrier.pNext = nullptr;
        dstImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        dstImageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        dstImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        dstImageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        dstImageBarrier.oldLayout = dstImage.imageLayout;
        dstImageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dstImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstImageBarrier.image = dstImage.imageHandle;
        dstImageBarrier.subresourceRange.aspectMask = dstImage.aspectMask;
        dstImageBarrier.subresourceRange.baseMipLevel = 0;
        dstImageBarrier.subresourceRange.levelCount = 1;
        dstImageBarrier.subresourceRange.baseArrayLayer = 0;
        dstImageBarrier.subresourceRange.layerCount = 1;

        dstImage.imageLayout = dstImageBarrier.newLayout;

        requiredBarriers.imageBarriers.push_back(dstImageBarrier);
    }
    return requiredBarriers;
}

// TransitionImageLayoutCommand implementations
TransitionImageLayoutCommand::TransitionImageLayoutCommand(ResourceManager::ImageHardwareWrap &image, VkImageLayout imageLayout, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask)
    : image(image), imageLayout(imageLayout), dstStageMask(dstStageMask), dstAccessMask(dstAccessMask)
{
    executorType = ExecutorType::Transfer;
}

CommandRecordVulkan::ExecutorType TransitionImageLayoutCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void TransitionImageLayoutCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    hardwareExecutor.hardwareContext->resourceManager.transitionImageLayout(hardwareExecutor.currentRecordQueue->commandBuffer, image, imageLayout, dstStageMask, dstAccessMask);
}

CommandRecordVulkan::RequiredBarriers TransitionImageLayoutCommand::getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor)
{
    return CommandRecordVulkan::RequiredBarriers{};
}
