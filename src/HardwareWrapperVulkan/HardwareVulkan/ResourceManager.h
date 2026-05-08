#pragma once

#include <ktm/ktm.h>

#include "DeviceManager.h"
#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "corona/kernel/utils/storage.h"

class HardwareExecutor;

struct ResourceManager
{
    struct ExternalMemoryHandle
    {
#if _WIN32 || _WIN64
        HANDLE handle = nullptr;
#else
        int fd = -1;
#endif
    };

    struct BufferHardwareWrap
    {
        uint32_t elementCount{0};
        uint32_t elementSize{0};
        uint64_t refCount{1};

        VkBuffer bufferHandle{VK_NULL_HANDLE};
        VkBufferUsageFlags bufferUsage{VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM};

        VmaAllocation bufferAlloc{VK_NULL_HANDLE};
        VmaAllocationInfo bufferAllocInfo{};
        bool hostImportedManualBind{false};

        int32_t bindlessIndex{-1};

        DeviceManager *device{nullptr};
        ResourceManager *resourceManager{nullptr};
    };

    struct ImageHardwareWrap
    {
        VkImageLayout imageLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        float pixelSize{0};
        ktm::uvec2 imageSize{0, 0};
        uint64_t refCount{1};

        VkFormat imageFormat{VK_FORMAT_MAX_ENUM};
        VkImageUsageFlags imageUsage{VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_NONE};

        uint32_t arrayLayers{1};
        uint32_t mipLevels{1};

        VkClearValue clearValue{};

        VkImage imageHandle{VK_NULL_HANDLE};

        VkImageView imageView{VK_NULL_HANDLE};
        std::unordered_map<uint64_t, VkImageView> allSubViews{};

        VmaAllocation imageAlloc{VK_NULL_HANDLE};
        VmaAllocationInfo imageAllocInfo{};

        int32_t bindlessIndex{-1};

        DeviceManager *device{nullptr};
        ResourceManager *resourceManager{nullptr};
    };

    struct BindlessDescriptorSet
    {
        VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    };

    ResourceManager();
    ~ResourceManager();

    void initResourceManager(DeviceManager &device);
    void cleanUpResourceManager();

    // Image operations
    [[nodiscard]] ImageHardwareWrap createImage(ktm::uvec2 imageSize,
                                                VkFormat imageFormat,
                                                float pixelSize,
                                                VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                                uint32_t arrayLayers = 1,
                                                uint32_t mipLevels = 1,
                                                VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);
    [[nodiscard]] VkImageView createImageView(ImageHardwareWrap &image, uint32_t layer = -1, uint32_t mipLevel = -1);
    void destroyImage(ImageHardwareWrap &image);

    // Buffer operations
    [[nodiscard]] BufferHardwareWrap createBuffer(uint32_t elementCount,
                                                  uint32_t elementSize,
                                                  VkBufferUsageFlags usage,
                                                  bool hostVisibleMapped = true,
                                                  bool useDedicated = false);
    void destroyBuffer(BufferHardwareWrap &buffer);

    // External memory operations
    [[nodiscard]] ExternalMemoryHandle exportBufferMemory(BufferHardwareWrap &sourceBuffer);
    [[nodiscard]] BufferHardwareWrap importBufferMemory(const ExternalMemoryHandle &memHandle,
                                                        uint32_t elementCount,
                                                        uint32_t elementSize,
                                                        uint32_t allocSize,
                                                        VkBufferUsageFlags usage);
    [[nodiscard]] BufferHardwareWrap importHostBuffer(void *hostPtr, uint64_t size);

    // Descriptor operations
    [[nodiscard]] int32_t storeDescriptor(Corona::Kernel::Utils::Storage<ResourceManager::ImageHardwareWrap>::WriteHandle &image);
    [[nodiscard]] int32_t storeDescriptor(Corona::Kernel::Utils::Storage<ResourceManager::BufferHardwareWrap>::WriteHandle &buffer);
    [[nodiscard]] bool storeDescriptorAt(Corona::Kernel::Utils::Storage<ResourceManager::ImageHardwareWrap>::WriteHandle &image, uint32_t descriptorIndex);
    [[nodiscard]] bool storeDescriptorAt(Corona::Kernel::Utils::Storage<ResourceManager::BufferHardwareWrap>::WriteHandle &buffer, uint32_t descriptorIndex);

    // Copy operations
    ResourceManager &copyBuffer(VkCommandBuffer &commandBuffer, BufferHardwareWrap &srcBuffer, BufferHardwareWrap &dstBuffer);
    //ResourceManager &copyImage(VkCommandBuffer &commandBuffer, ImageHardwareWrap &source, ImageHardwareWrap &destination);
    ResourceManager &copyImage(VkCommandBuffer &commandBuffer, ImageHardwareWrap &source, ImageHardwareWrap &destination, uint32_t srcLayer = 0, uint32_t dstLayer = 0, uint32_t srcMip = 0, uint32_t dstMip = 0);

    ResourceManager &copyBufferToImage(VkCommandBuffer &commandBuffer, BufferHardwareWrap &buffer, ImageHardwareWrap &image, uint32_t mipLevel = 0, uint32_t layerCount = 1);
    ResourceManager &copyImageToBuffer(VkCommandBuffer &commandBuffer, ImageHardwareWrap &image, BufferHardwareWrap &buffer);
    ResourceManager &blitImage(VkCommandBuffer &commandBuffer, ImageHardwareWrap &srcImage, ImageHardwareWrap &dstImage);

    void copyBufferToHost(BufferHardwareWrap &buffer, void *cpuData, uint64_t size);

    // Layout transition
    void transitionImageLayout(VkCommandBuffer &commandBuffer,
                               ImageHardwareWrap &image,
                               VkImageLayout imageLayout,
                               VkPipelineStageFlags2 dstStageMask,
                               VkAccessFlags2 dstAccessMask);

    // Shader module
    [[nodiscard]] VkShaderModule createShaderModule(const std::vector<unsigned int> &code);


    BindlessDescriptorSet bindlessDescriptors[3];
    
  private:
    //enum class DescriptorBindingType : uint8_t
    //{
    //   Texture = 0,
    //   StorageBuffer = 1,
    //   StorageImage = 2,
    //   Uniform = 3
    //};

    //template <typename THandle>
    //struct BindingEntry
    //{
    //    THandle handle = static_cast<THandle>(VK_NULL_HANDLE);
    //    int bindingIndex = -1;
    //};

    void createVmaAllocator();
    void createTextureSampler();
    void createBindlessDescriptorSet();
    void createExternalBufferMemoryPool();
    
    void createDedicatedBuffer(const VkBufferCreateInfo &bufferInfo, const VmaAllocationCreateInfo &allocInfo, BufferHardwareWrap &resultBuffer);
    void createPooledBuffer(const VkBufferCreateInfo &bufferInfo, const VmaAllocationCreateInfo &allocInfo, BufferHardwareWrap &resultBuffer);
    void createNonExportableBuffer(const VkBufferCreateInfo &bufferInfo, const VmaAllocationCreateInfo &allocInfo, BufferHardwareWrap &resultBuffer);

    // uint32_t getMipLevelsCount(uint32_t texWidth, uint32_t texHeight) const;

    VmaAllocator vmaAllocator{VK_NULL_HANDLE};
    VmaPool exportBufferPool{VK_NULL_HANDLE};
    VkSampler textureSampler{VK_NULL_HANDLE};

    const uint32_t textureBinding{0};
    const uint32_t storageBufferBinding{1};
    const uint32_t storageImageBinding{2};

    uint64_t deviceMemorySize{0};
    uint64_t hostSharedMemorySize{0};
    uint64_t multiInstanceMemorySize{0};

    DeviceManager *device{nullptr};

    // 缓存的物理设备属性，避免重复查询
    VkPhysicalDeviceProperties cachedDeviceProperties{};
    VkPhysicalDeviceDescriptorIndexingProperties cachedIndexingProperties{};
};
