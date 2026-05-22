#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "format.h"
#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"

class DeviceManager;

namespace Corona::Horizon::Vulkan
{
    class ResourceManager
    {
    public:
        struct ExternalMemoryHandle
        {
#if defined(_WIN32) || defined(_WIN64)
            void* handle = nullptr;
#else
            int fd = -1;
#endif
        };

        struct BufferHardwareWrap
        {
            uint32_t element_count = 0;
            uint32_t elementCount = 0;
            uint32_t element_size = 0;
            uint32_t elementSize = 0;
            uint64_t ref_count = 1;
            uint64_t refCount = 1;

            VkBuffer buffer = VK_NULL_HANDLE;
            VkBuffer bufferHandle = VK_NULL_HANDLE;
            VkBufferUsageFlags usage = 0;
            VkBufferUsageFlags bufferUsage = 0;

            VmaAllocation allocation = VK_NULL_HANDLE;
            VmaAllocation bufferAlloc = VK_NULL_HANDLE;
            VmaAllocationInfo allocation_info{};
            VmaAllocationInfo bufferAllocInfo{};

            bool host_imported_manual_bind = false;
            bool hostImportedManualBind = false;

            int32_t bindless_index = -1;
            int32_t bindlessIndex = -1;

            DeviceManager* device = nullptr;
            ResourceManager* resource_manager = nullptr;
            ResourceManager* resourceManager = nullptr;
        };

        struct ImageHardwareWrap
        {
            uint64_t ref_count = 1;
            uint64_t refCount = 1;

            VkImage image = VK_NULL_HANDLE;
            VkImage imageHandle = VK_NULL_HANDLE;
            VkImageView image_view = VK_NULL_HANDLE;
            VkImageView imageView = VK_NULL_HANDLE;
            std::unordered_map<uint64_t, VkImageView> sub_views;
            std::unordered_map<uint64_t, VkImageView> allSubViews;

            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkFormat imageFormat = VK_FORMAT_UNDEFINED;
            VkImageUsageFlags usage = 0;
            VkImageUsageFlags imageUsage = 0;
            VkImageAspectFlags aspect_mask = 0;
            VkImageAspectFlags aspectMask = 0;

            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t depth = 1;
            uint32_t array_layers = 1;
            uint32_t arrayLayers = 1;
            uint32_t mip_levels = 1;
            uint32_t mipLevels = 1;

            VkClearValue clear_value{};
            VkClearValue clearValue{};

            VmaAllocation allocation = VK_NULL_HANDLE;
            VmaAllocation imageAlloc = VK_NULL_HANDLE;
            VmaAllocationInfo allocation_info{};
            VmaAllocationInfo imageAllocInfo{};

            int32_t bindless_index = -1;
            int32_t bindlessIndex = -1;

            DeviceManager* device = nullptr;
            ResourceManager* resource_manager = nullptr;
            ResourceManager* resourceManager = nullptr;
        };

        struct BindlessDescriptorSet
        {
            VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
            VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
            VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
            VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
            VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        };

        ResourceManager() = default;
        ~ResourceManager();

        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;

        void init(DeviceManager& device_manager);
        void cleanup() noexcept;

        void init_resource_manager(DeviceManager& device_manager) { init(device_manager); }
        void initResourceManager(DeviceManager& device_manager) { init(device_manager); }
        void clean_up_resource_manager() noexcept { cleanup(); }
        void cleanUpResourceManager() noexcept { cleanup(); }

        [[nodiscard]] BufferHardwareWrap create_buffer(uint32_t element_count,
                                                       uint32_t element_size,
                                                       VkBufferUsageFlags usage,
                                                       bool host_visible_mapped = true,
                                                       bool dedicated = false);
        [[nodiscard]] BufferHardwareWrap createBuffer(uint32_t element_count,
                                                      uint32_t element_size,
                                                      VkBufferUsageFlags usage,
                                                      bool host_visible_mapped = true,
                                                      bool dedicated = false)
        {
            return create_buffer(element_count, element_size, usage, host_visible_mapped, dedicated);
        }

        [[nodiscard]] BufferHardwareWrap import_buffer_memory(const ExternalMemoryHandle& handle,
                                                              uint32_t element_count,
                                                              uint32_t element_size,
                                                              uint32_t allocation_size,
                                                              VkBufferUsageFlags usage);
        [[nodiscard]] BufferHardwareWrap importBufferMemory(const ExternalMemoryHandle& handle,
                                                            uint32_t element_count,
                                                            uint32_t element_size,
                                                            uint32_t allocation_size,
                                                            VkBufferUsageFlags usage)
        {
            return import_buffer_memory(handle, element_count, element_size, allocation_size, usage);
        }

        [[nodiscard]] ExternalMemoryHandle export_buffer_memory(BufferHardwareWrap& buffer);
        [[nodiscard]] ExternalMemoryHandle exportBufferMemory(BufferHardwareWrap& buffer)
        {
            return export_buffer_memory(buffer);
        }

        void destroy_buffer(BufferHardwareWrap& buffer) noexcept;
        void destroyBuffer(BufferHardwareWrap& buffer) noexcept { destroy_buffer(buffer); }

        [[nodiscard]] ImageHardwareWrap create_image(uint32_t width,
                                                     uint32_t height,
                                                     VkFormat format,
                                                     VkImageUsageFlags usage,
                                                     uint32_t layers = 1,
                                                     uint32_t mip_levels = 1);
        [[nodiscard]] VkImageView create_image_view(ImageHardwareWrap& image,
                                                    uint32_t layer = std::numeric_limits<uint32_t>::max(),
                                                    uint32_t mip_level = std::numeric_limits<uint32_t>::max());
        [[nodiscard]] VkImageView createImageView(ImageHardwareWrap& image,
                                                  uint32_t layer = std::numeric_limits<uint32_t>::max(),
                                                  uint32_t mip_level = std::numeric_limits<uint32_t>::max())
        {
            return create_image_view(image, layer, mip_level);
        }
        void destroy_image(ImageHardwareWrap& image) noexcept;
        void destroyImage(ImageHardwareWrap& image) noexcept { destroy_image(image); }

        [[nodiscard]] int32_t store_descriptor(BufferHardwareWrap& buffer);
        [[nodiscard]] int32_t storeDescriptor(BufferHardwareWrap& buffer) { return store_descriptor(buffer); }
        [[nodiscard]] int32_t store_descriptor(ImageHardwareWrap& image);
        [[nodiscard]] int32_t storeDescriptor(ImageHardwareWrap& image) { return store_descriptor(image); }

        [[nodiscard]] bool store_descriptor_at(BufferHardwareWrap& buffer, uint32_t descriptor_index);
        [[nodiscard]] bool storeDescriptorAt(BufferHardwareWrap& buffer, uint32_t descriptor_index)
        {
            return store_descriptor_at(buffer, descriptor_index);
        }

        [[nodiscard]] bool store_descriptor_at(ImageHardwareWrap& image, uint32_t descriptor_index);
        [[nodiscard]] bool storeDescriptorAt(ImageHardwareWrap& image, uint32_t descriptor_index)
        {
            return store_descriptor_at(image, descriptor_index);
        }

        [[nodiscard]] bool initialized() const noexcept { return device_ != nullptr && allocator_ != VK_NULL_HANDLE; }
        [[nodiscard]] DeviceManager* device() const noexcept { return device_; }

        BindlessDescriptorSet bindless_descriptors[3]{};
        BindlessDescriptorSet bindlessDescriptors[3]{};

    private:
        [[nodiscard]] VkDevice logical_device() const noexcept;
        [[nodiscard]] VkPhysicalDevice physical_device() const noexcept;

        void create_allocator();
        void destroy_allocator() noexcept;

        DeviceManager* device_ = nullptr;
        VmaAllocator allocator_ = VK_NULL_HANDLE;
        uint32_t next_descriptor_index_ = 0;
    };
}

namespace Corona::Horizon
{
    using VulkanResourceManager = Vulkan::ResourceManager;
}
