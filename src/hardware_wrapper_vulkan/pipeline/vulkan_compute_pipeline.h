#pragma once

#include "hardware_wrapper_vulkan/hardware/command.h"
#include "horizon.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <vector>

namespace Corona::Horizon
{
    class VulkanComputePipeline
    {
    public:
        struct BoundBuffer
        {
            uint32_t binding { 0 };
            HardwareBuffer buffer {};
            AccessKind access { AccessKind::ReadWrite };
        };

        struct BoundImage
        {
            uint32_t binding { 0 };
            HardwareImage image {};
            AccessKind access { AccessKind::ReadWrite };
        };

        struct Snapshot
        {
            ComputePipelineDesc desc;
            DispatchDesc dispatch;
            std::vector<BoundBuffer> buffers;
            std::vector<BoundImage> images;
        };

        struct PreparedDispatch
        {
            VkPipelineLayout layout { VK_NULL_HANDLE };
            VkPipeline pipeline { VK_NULL_HANDLE };
            VkDescriptorSet descriptor_set { VK_NULL_HANDLE };
            std::shared_ptr<void> descriptor_set_lifetime {};
        };

        explicit VulkanComputePipeline(ComputePipelineDesc desc,
                                       std::source_location source_location = std::source_location::current());
        ~VulkanComputePipeline();

        [[nodiscard]] ComputePipelineDesc desc() const;
        [[nodiscard]] std::source_location source_location() const noexcept { return source_location_; }

        void set_dispatch(uint16_t groups_x, uint16_t groups_y, uint16_t groups_z);
        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type);
        void bind_storage_buffer(uint32_t binding, const HardwareBuffer& buffer);
        void bind_storage_image(uint32_t binding, const HardwareImage& image);

        [[nodiscard]] Snapshot snapshot() const;
        [[nodiscard]] CommandBatch command_batch(const ResourceHandle& pipeline) const;
        [[nodiscard]] PreparedDispatch prepare_dispatch(VkDevice device, const DispatchDesc& dispatch);

        struct BindingLayout
        {
            uint32_t binding { 0 };
            DispatchBindingKind kind { DispatchBindingKind::StorageBuffer };

            [[nodiscard]] friend bool operator==(const BindingLayout&, const BindingLayout&) noexcept = default;
        };

    private:
        struct PipelineState
        {
            VkDevice device { VK_NULL_HANDLE };
            std::vector<BindingLayout> bindings;
            VkDescriptorSetLayout descriptor_set_layout { VK_NULL_HANDLE };
            VkPipelineLayout layout { VK_NULL_HANDLE };
            VkPipeline pipeline { VK_NULL_HANDLE };
        };

        [[nodiscard]] uint32_t push_constant_size() const noexcept;
        [[nodiscard]] PipelineState create_pipeline_state_unlocked(VkDevice device, std::vector<BindingLayout> bindings) const;
        [[nodiscard]] PipelineState& pipeline_state_unlocked(VkDevice device, const DispatchDesc& dispatch);
        void destroy_pipeline_cache_unlocked() noexcept;

        ComputePipelineDesc desc_;
        std::source_location source_location_;

        mutable std::mutex mutex_;
        DispatchDesc dispatch_ {};
        std::vector<std::byte> push_constant_data_;
        std::vector<BoundBuffer> bound_buffers_;
        std::vector<BoundImage> bound_images_;
        mutable std::vector<PipelineState> pipeline_cache_;
    };
}
