#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vk_mem_alloc.h>
#include <volk.h>

#include <mutex>
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
        void destroy_buffer(BufferWrap& buffer) noexcept;

    private:
        struct DescriptorArray
        {
            VkDescriptorPool pool { VK_NULL_HANDLE };
            VkDescriptorSetLayout layout { VK_NULL_HANDLE };
            VkDescriptorSet set { VK_NULL_HANDLE };
            uint32_t capacity { 0 };
        };

        [[nodiscard]] VmaAllocationCreateInfo allocation_info(const HardwareBufferDesc& desc) const noexcept;
        [[nodiscard]] VkBufferCreateInfo buffer_info(const HardwareBufferDesc& desc, std::vector<uint32_t>& queue_family_indices) const;
        void create_allocator();
        void create_storage_buffer_descriptors();
        void destroy_descriptors_unlocked() noexcept;
        void shutdown_unlocked() noexcept;

        mutable std::mutex mutex_;
        DeviceManager* device_manager_ { nullptr };
        VmaAllocator allocator_ { VK_NULL_HANDLE };
        DescriptorArray storage_buffer_descriptors_ {};
        uint32_t next_storage_buffer_descriptor_ { 0 };
    };
}
