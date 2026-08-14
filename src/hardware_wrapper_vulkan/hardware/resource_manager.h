#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vk_mem_alloc.h>
#include <volk.h>

#include <array>
#include <cstdint>
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
        void destroy_buffer(BufferWrap& buffer) noexcept;

        [[nodiscard]] ImageWrap create_image(const HardwareImageDesc& desc);
        [[nodiscard]] ImageWrap import_image(const ExternalMemoryHandle& handle, const HardwareImageDesc& desc, uint64_t allocation_size = 0);
        [[nodiscard]] ImageWrap wrap_swapchain_image(VkImage image, VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, std::string debug_name = {});
        [[nodiscard]] ExternalMemoryHandle export_image(ImageWrap& image);
        [[nodiscard]] uint32_t store_descriptor(ImageWrap& image);
        [[nodiscard]] uint32_t store_sampled_descriptor(ImageWrap& image);
        [[nodiscard]] uint32_t store_storage_descriptor(ImageWrap& image);
        [[nodiscard]] ImageSubresourceLayout image_subresource_layout(const ImageWrap& image, uint32_t layer, uint32_t mip) const;
        void flush_image(const ImageWrap& image, uint64_t byte_offset, uint64_t byte_size);
        void destroy_image(ImageWrap& image) noexcept;

        static constexpr uint32_t bindless_texture_set = 0;
        static constexpr uint32_t bindless_storage_buffer_set = 1;
        static constexpr uint32_t bindless_storage_image_set = 2;
        static constexpr uint32_t bindless_descriptor_set_count = 3;
        [[nodiscard]] std::array<VkDescriptorSetLayout, bindless_descriptor_set_count> bindless_descriptor_set_layouts();
        [[nodiscard]] std::array<VkDescriptorSet, bindless_descriptor_set_count> bindless_descriptor_sets();
        [[nodiscard]] VkSampler default_sampler();

    private:
        struct DescriptorArray
        {
            VkDescriptorPool pool { VK_NULL_HANDLE };
            VkDescriptorSetLayout layout { VK_NULL_HANDLE };
            VkDescriptorSet set { VK_NULL_HANDLE };
            uint32_t capacity { 0 };
            // 单调分配游标 + 回收的空闲槽位。destroy 时归还槽位供后续复用，
            // 避免 bindless 数组随资源 create/destroy 单调耗尽（P0：slot 泄漏修复）。
            uint32_t next { 0 };
            std::vector<uint32_t> free_list;

            static constexpr uint32_t invalid_slot = ~0u;

            // 分配一个槽位：优先复用回收槽位，否则递增游标；容量耗尽返回 invalid_slot。
            [[nodiscard]] uint32_t allocate() noexcept
            {
                if (!free_list.empty())
                {
                    const uint32_t index = free_list.back();
                    free_list.pop_back();
                    return index;
                }
                if (next >= capacity)
                {
                    return invalid_slot;
                }
                return next++;
            }

            // 归还槽位供后续复用。调用方须确保 index 有效且该槽位不再被 shader 索引。
            void release(uint32_t index) noexcept
            {
                free_list.push_back(index);
            }

            // 重建数组时重置分配状态（游标归零、清空回收表）。
            void reset_allocation() noexcept
            {
                next = 0;
                free_list.clear();
            }
        };

        [[nodiscard]] VmaAllocationCreateInfo allocation_info(const HardwareBufferDesc& desc) const noexcept;
        [[nodiscard]] VmaAllocationCreateInfo allocation_info(const HardwareImageDesc& desc) const noexcept;
        [[nodiscard]] VkBufferCreateInfo buffer_info(const HardwareBufferDesc& desc, std::vector<uint32_t>& queue_family_indices) const;
        [[nodiscard]] VkImageCreateInfo image_info(const HardwareImageDesc& desc) const;
        [[nodiscard]] VkImageView create_image_view(const ImageWrap& image, ImageSubresourceRange range) const;
        void create_allocator();
        void create_default_sampler();
        void create_bindless_descriptor_array(DescriptorArray& descriptors,
                                              VkDescriptorType descriptor_type,
                                              uint32_t descriptor_limit,
                                              bool update_after_bind,
                                              const char* label);
        void create_combined_texture_descriptors();
        void create_storage_buffer_descriptors();
        void create_storage_image_descriptors();
        void ensure_bindless_descriptors_unlocked();
        void destroy_descriptors_unlocked() noexcept;
        void shutdown_unlocked() noexcept;

        mutable std::mutex mutex_;
        DeviceManager* device_manager_ { nullptr };
        VmaAllocator allocator_ { VK_NULL_HANDLE };
        VkSampler default_sampler_ { VK_NULL_HANDLE };
        DescriptorArray combined_texture_descriptors_ {};
        DescriptorArray storage_buffer_descriptors_ {};
        DescriptorArray storage_image_descriptors_ {};
    };
}
