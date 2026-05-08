#include "ResourceCommand.h"

// CopyBufferCommand implementations
CopyBufferCommand::CopyBufferCommand(ResourceManager::BufferHardwareWrap &src, ResourceManager::BufferHardwareWrap &dst)
    : srcBuffer(src), dstBuffer(dst)
{
    executorType = ExecutorType::Transfer;
}

CommandRecordVulkan::ExecutorType CopyBufferCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyBufferCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    hardwareExecutor.hardwareContext->resourceManager.copyBuffer(hardwareExecutor.currentRecordQueue->commandBuffer, srcBuffer, dstBuffer);
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
                                                   uint32_t mip)
    : srcBuffer(srcBuf), dstImage(dstImg), mipLevel(mip)
{
    executorType = ExecutorType::Transfer;
}

CommandRecordVulkan::ExecutorType CopyBufferToImageCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyBufferToImageCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    // 使用带 mipLevel 参数的重载版本
    hardwareExecutor.hardwareContext->resourceManager.copyBufferToImage(
        hardwareExecutor.currentRecordQueue->commandBuffer,
        srcBuffer,
        dstImage,
        mipLevel,
        dstImage.arrayLayers);

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
        dstImageBarrier.subresourceRange.baseMipLevel = mipLevel;
        dstImageBarrier.subresourceRange.levelCount = 1;
        dstImageBarrier.subresourceRange.baseArrayLayer = 0;
        dstImageBarrier.subresourceRange.layerCount = dstImage.arrayLayers; // 转换所有图层
        dstImageBarrier.pNext = nullptr;

        dstImage.imageLayout = dstImageBarrier.newLayout;

        requiredBarriers.imageBarriers.push_back(dstImageBarrier);
    }
    return requiredBarriers;
}

// CopyImageToBufferCommand implementations
CopyImageToBufferCommand::CopyImageToBufferCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::BufferHardwareWrap &dstBuf)
    : srcImage(srcImg), dstBuffer(dstBuf)
{
    executorType = ExecutorType::Transfer;
}

CommandRecordVulkan::ExecutorType CopyImageToBufferCommand::getExecutorType()
{
    return CommandRecordVulkan::ExecutorType::Transfer;
}

void CopyImageToBufferCommand::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    hardwareExecutor.hardwareContext->resourceManager.copyImageToBuffer(hardwareExecutor.currentRecordQueue->commandBuffer, srcImage, dstBuffer);

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
        srcImageBarrier.subresourceRange.levelCount = 1;
        srcImageBarrier.subresourceRange.baseArrayLayer = 0;
        srcImageBarrier.subresourceRange.layerCount = 1;
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
