#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vk_mem_alloc.h>
#include <volk.h>

#include <mutex>
#include <string>
#include <vector>

#include "hardware_wrapper_vulkan/resource_pool.h"

namespace Corona::Horizon
{
    class DeviceManager;

    class ResourceManager
    {
    public:
        ResourceManager();
        ~ResourceManager();

        ResourceManager(const ResourceManager&) = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;
        ResourceManager(ResourceManager&&) = delete;
        ResourceManager& operator=(ResourceManager&&) = delete;

        void initialize(DeviceManager& device_manager);
        void shutdown() noexcept;

        [[nodiscard]] bool initialized() const noexcept;

        [[nodiscard]] BufferWrap create_buffer(const HardwareBufferDesc& desc);
        [[nodiscard]] BufferWrap import_buffer(const ExternalMemoryHandle& handle, const HardwareBufferDesc& desc);
        [[nodiscard]] ExternalMemoryHandle export_buffer(BufferWrap& buffer);
        [[nodiscard]] uint32_t store_descriptor(BufferWrap& buffer);
        void flush_buffer(const BufferWrap& buffer, uint64_t byte_offset, uint64_t byte_size);
        void invalidate_buffer(const BufferWrap& buffer, uint64_t byte_offset, uint64_t byte_size);
        void destroy_buffer(BufferWrap& buffer) noexcept;

        [[nodiscard]] ImageWrap create_image(const HardwareImageDesc& desc);
        [[nodiscard]] ImageWrap import_image(const ExternalMemoryHandle& handle, const HardwareImageDesc& desc, uint64_t allocation_size = 0);
        [[nodiscard]] ImageWrap wrap_swapchain_image(VkImage image, VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, std::string debug_name = {});
        [[nodiscard]] ExternalMemoryHandle export_image(ImageWrap& image);
        [[nodiscard]] uint32_t store_descriptor(ImageWrap& image);
        [[nodiscard]] ImageSubresourceLayout image_subresource_layout(const ImageWrap& image, uint32_t layer, uint32_t mip) const;
        void flush_image(const ImageWrap& image, uint64_t byte_offset, uint64_t byte_size);
        void invalidate_image(const ImageWrap& image, uint64_t byte_offset, uint64_t byte_size);
        void destroy_image(ImageWrap& image) noexcept;

    private:
        struct DescriptorArray
        {
            VkDescriptorPool pool { VK_NULL_HANDLE };
            VkDescriptorSetLayout layout { VK_NULL_HANDLE };
            VkDescriptorSet set { VK_NULL_HANDLE };
            uint32_t capacity { 0 };
        };

        [[nodiscard]] VmaAllocationCreateInfo allocation_info(const HardwareBufferDesc& desc) const noexcept;
        [[nodiscard]] VmaAllocationCreateInfo allocation_info(const HardwareImageDesc& desc) const noexcept;
        [[nodiscard]] VkBufferCreateInfo buffer_info(const HardwareBufferDesc& desc, std::vector<uint32_t>& queue_family_indices) const;
        [[nodiscard]] VkImageCreateInfo image_info(const HardwareImageDesc& desc) const;
        [[nodiscard]] VkImageView create_image_view(const ImageWrap& image, ImageSubresourceRange range) const;
        void create_allocator();
        void create_storage_buffer_descriptors();
        void create_sampled_image_descriptors();
        void destroy_descriptors_unlocked() noexcept;
        void shutdown_unlocked() noexcept;

        mutable std::mutex mutex_;
        DeviceManager* device_manager_ { nullptr };
        VmaAllocator allocator_ { VK_NULL_HANDLE };
        DescriptorArray storage_buffer_descriptors_ {};
        DescriptorArray sampled_image_descriptors_ {};
        uint32_t next_storage_buffer_descriptor_ { 0 };
        uint32_t next_sampled_image_descriptor_ { 0 };
    };
}
