#pragma once

#include "HardwareExecutorVulkan.h"

struct CopyBufferCommand : public CommandRecordVulkan
{
    ResourceManager::BufferHardwareWrap &srcBuffer;
    ResourceManager::BufferHardwareWrap &dstBuffer;

    CopyBufferCommand(ResourceManager::BufferHardwareWrap &src, ResourceManager::BufferHardwareWrap &dst);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    CommandRecordVulkan::RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor) override;
};

struct CopyImageCommand : public CommandRecordVulkan
{
    ResourceManager::ImageHardwareWrap &srcImage;
    ResourceManager::ImageHardwareWrap &dstImage;

    uint32_t srcLayer;
    uint32_t dstLayer;
    uint32_t srcMip;
    uint32_t dstMip;

    CopyImageCommand(ResourceManager::ImageHardwareWrap &srcImg,
                     ResourceManager::ImageHardwareWrap &dstImg,
                     uint32_t srcLayer = 0,
                     uint32_t dstLayer = 0,
                     uint32_t srcMip = 0,
                     uint32_t dstMip = 0);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    CommandRecordVulkan::RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor) override;
};

// struct CopyBufferToImageCommand : public CommandRecordVulkan {
//     ResourceManager::BufferHardwareWrap& srcBuffer;
//     ResourceManager::ImageHardwareWrap& dstImage;
//     uint32_t mipLevel;
//
//     CopyBufferToImageCommand(ResourceManager::BufferHardwareWrap& srcBuf,
//                              ResourceManager::ImageHardwareWrap& dstImg,
//                              uint32_t mip = 0)
//         : srcBuffer(srcBuf), dstImage(dstImg), mipLevel(mip) {
//         executorType = ExecutorType::Transfer;
//     }
//
//     ExecutorType getExecutorType() override {
//         return CommandRecordVulkan::ExecutorType::Transfer;
//     }
//
//     void commitCommand(HardwareExecutorVulkan& hardwareExecutor) override {
//         // 使用带 mipLevel 参数的重载版本
//         hardwareExecutor.hardwareContext->resourceManager.copyBufferToImage(
//             hardwareExecutor.currentRecordQueue->commandBuffer,
//             srcBuffer,
//             dstImage,
//             mipLevel);
//     }
//
//     CommandRecordVulkan::RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan& hardwareExecutor) override {
//         CommandRecordVulkan::RequiredBarriers requiredBarriers;
//         {
//             VkBufferMemoryBarrier2 srcBufferBarrier{};
//             srcBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
//             srcBufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
//             srcBufferBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
//             srcBufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
//             srcBufferBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
//             srcBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//             srcBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//             srcBufferBarrier.buffer = srcBuffer.bufferHandle;
//             srcBufferBarrier.offset = 0;
//             srcBufferBarrier.size = VK_WHOLE_SIZE;
//             srcBufferBarrier.pNext = nullptr;
//
//             requiredBarriers.bufferBarriers.push_back(srcBufferBarrier);
//         }
//         {
//             VkImageMemoryBarrier2 dstImageBarrier{};
//             dstImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
//             dstImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
//             dstImageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
//             dstImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
//             dstImageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
//             dstImageBarrier.oldLayout = dstImage.imageLayout;
//             dstImageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//             dstImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//             dstImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//             dstImageBarrier.image = dstImage.imageHandle;
//             dstImageBarrier.subresourceRange.aspectMask = dstImage.aspectMask;
//             dstImageBarrier.subresourceRange.baseMipLevel = mipLevel;  // 使用指定的 mipLevel
//             dstImageBarrier.subresourceRange.levelCount = 1;
//             dstImageBarrier.subresourceRange.baseArrayLayer = 0;
//             dstImageBarrier.subresourceRange.layerCount = 1;
//             dstImageBarrier.pNext = nullptr;
//
//             dstImage.imageLayout = dstImageBarrier.newLayout;
//
//             requiredBarriers.imageBarriers.push_back(dstImageBarrier);
//         }
//         return requiredBarriers;
//     }
// };

struct CopyBufferToImageCommand : public CommandRecordVulkan
{
    ResourceManager::BufferHardwareWrap &srcBuffer;
    ResourceManager::ImageHardwareWrap &dstImage;
    uint32_t mipLevel;

    CopyBufferToImageCommand(ResourceManager::BufferHardwareWrap &srcBuf,
                             ResourceManager::ImageHardwareWrap &dstImg,
                             uint32_t mip = 0);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    CommandRecordVulkan::RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor) override;
};

struct CopyImageToBufferCommand : public CommandRecordVulkan
{
    ResourceManager::ImageHardwareWrap &srcImage;
    ResourceManager::BufferHardwareWrap &dstBuffer;

    CopyImageToBufferCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::BufferHardwareWrap &dstBuf);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    CommandRecordVulkan::RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor) override;
};

struct BlitImageCommand : public CommandRecordVulkan
{
    ResourceManager::ImageHardwareWrap &srcImage;
    ResourceManager::ImageHardwareWrap &dstImage;

    BlitImageCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::ImageHardwareWrap &dstImg);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    CommandRecordVulkan::RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor) override;
};

struct TransitionImageLayoutCommand : public CommandRecordVulkan
{
    ResourceManager::ImageHardwareWrap &image;
    VkImageLayout imageLayout;
    VkPipelineStageFlags2 dstStageMask;
    VkAccessFlags2 dstAccessMask;

    TransitionImageLayoutCommand(ResourceManager::ImageHardwareWrap &image, VkImageLayout imageLayout, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    CommandRecordVulkan::RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor) override;
};
