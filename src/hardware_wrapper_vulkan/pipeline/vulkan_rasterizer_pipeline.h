#pragma once

#include "hardware_wrapper_vulkan/hardware/command.h"
#include "horizon_refac.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <vector>

namespace Corona::Horizon
{
    class VulkanRasterizerPipeline
    {
    public:
        struct BoundBuffer
        {
            uint64_t byte_offset { 0 };
            uint32_t type_size { 0 };
            int32_t bind_type { -1 };
            HardwareBuffer buffer {};
        };

        struct BoundImage
        {
            uint64_t byte_offset { 0 };
            uint32_t type_size { 0 };
            int32_t bind_type { -1 };
            uint32_t location { 0 };
            HardwareImage image {};
        };

        struct RecordedDraw
        {
            std::weak_ptr<IResourceRef> pipeline;
            HardwareBuffer index_buffer {};
            HardwareBuffer vertex_buffer {};
            DrawIndexedParams params {};
            std::vector<std::byte> push_constant_data;
        };

        struct Snapshot
        {
            RasterizerPipelineDesc desc;
            uint32_t width { 0 };
            uint32_t height { 0 };
            std::vector<BoundBuffer> buffers;
            std::vector<BoundImage> images;
            std::vector<RecordedDraw> draws;
        };

        explicit VulkanRasterizerPipeline(RasterizerPipelineDesc desc,
                                          std::source_location source_location = std::source_location::current());
        ~VulkanRasterizerPipeline();

        [[nodiscard]] RasterizerPipelineDesc desc() const;
        [[nodiscard]] std::source_location source_location() const noexcept { return source_location_; }

        void set_extent(uint16_t width, uint16_t height);
        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t location = 0);
        void add_auto_bind_entry(EmbeddedShader::AutoBindEntry entry);
        [[nodiscard]] std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries() const;
        void record(const ResourceHandle& pipeline, const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
        void record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
        void clear_records();

        struct GraphicsPipeline
        {
            VkPipelineLayout layout { VK_NULL_HANDLE };
            VkPipeline pipeline { VK_NULL_HANDLE };
        };

        [[nodiscard]] GraphicsPipeline graphics_pipeline(VkDevice device,
                                                         VkFormat color_format,
                                                         VkFormat depth_format,
                                                         uint32_t vertex_stride);

        [[nodiscard]] Snapshot snapshot() const;
        [[nodiscard]] CommandBatch command_batch() const;

    private:
        struct PipelineKey
        {
            VkDevice device { VK_NULL_HANDLE };
            VkFormat color_format { VK_FORMAT_UNDEFINED };
            VkFormat depth_format { VK_FORMAT_UNDEFINED };
            uint32_t vertex_stride { 0 };

            [[nodiscard]] friend bool operator==(const PipelineKey&, const PipelineKey&) noexcept = default;
        };

        struct PipelineState
        {
            PipelineKey key {};
            VkPipelineLayout layout { VK_NULL_HANDLE };
            VkPipeline pipeline { VK_NULL_HANDLE };
        };

        [[nodiscard]] uint32_t push_constant_size() const noexcept;
        [[nodiscard]] PipelineState create_graphics_pipeline_unlocked(const PipelineKey& key) const;
        void destroy_pipeline_cache_unlocked() noexcept;

        RasterizerPipelineDesc desc_;
        std::source_location source_location_;

        mutable std::mutex mutex_;
        uint32_t width_ { 0 };
        uint32_t height_ { 0 };
        std::vector<std::byte> push_constant_data_;
        std::vector<BoundBuffer> bound_buffers_;
        std::vector<BoundImage> bound_images_;
        std::vector<RecordedDraw> draws_;
        mutable std::vector<PipelineState> pipeline_cache_;
    };
}
