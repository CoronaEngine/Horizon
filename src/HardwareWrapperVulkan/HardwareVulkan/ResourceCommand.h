#pragma once

#include "HardwareExecutorVulkan.h"

struct CopyBufferCommand : public CommandRecordVulkan
{
    ResourceManager::BufferHardwareWrap &srcBuffer;
    ResourceManager::BufferHardwareWrap &dstBuffer;

    VkDeviceSize srcOffset{0};
    VkDeviceSize dstOffset{0};
    VkDeviceSize size{0};

    CopyBufferCommand(ResourceManager::BufferHardwareWrap &src, ResourceManager::BufferHardwareWrap &dst,
                      VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0, VkDeviceSize size = 0);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) override;
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
    void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) override;

private:
    bool valid() const;
};

struct CopyBufferToImageCommand : public CommandRecordVulkan
{
    ResourceManager::BufferHardwareWrap &srcBuffer;
    ResourceManager::ImageHardwareWrap &dstImage;
    uint32_t mipLevel;
    uint32_t imageLayer;
    uint32_t layerCount;
    VkDeviceSize bufferOffset;

    CopyBufferToImageCommand(ResourceManager::BufferHardwareWrap &srcBuf,
                             ResourceManager::ImageHardwareWrap &dstImg,
                             uint32_t mip = 0, uint32_t layer = 0,
                             VkDeviceSize offset = 0, uint32_t layers = 0);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) override;
};

struct CopyImageToBufferCommand : public CommandRecordVulkan
{
    ResourceManager::ImageHardwareWrap &srcImage;
    ResourceManager::BufferHardwareWrap &dstBuffer;
    uint32_t mipLevel;
    uint32_t imageLayer;
    uint32_t layerCount;
    VkDeviceSize bufferOffset;

    CopyImageToBufferCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::BufferHardwareWrap &dstBuf,
                            uint32_t mip = 0, uint32_t layer = 0,
                            VkDeviceSize offset = 0, uint32_t layers = 0);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) override;
};

struct BlitImageCommand : public CommandRecordVulkan
{
    ResourceManager::ImageHardwareWrap &srcImage;
    ResourceManager::ImageHardwareWrap &dstImage;

    BlitImageCommand(ResourceManager::ImageHardwareWrap &srcImg, ResourceManager::ImageHardwareWrap &dstImg);

    ExecutorType getExecutorType() override;
    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) override;
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
    void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) override;
};
