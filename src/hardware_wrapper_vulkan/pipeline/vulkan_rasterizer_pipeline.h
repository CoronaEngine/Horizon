#pragma once

#include "hardware_wrapper_vulkan/hardware/command.h"
#include "horizon.h"

#include <volk.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
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
            uint32_t set { 0 };
            uint32_t binding { 0 };
            HardwareBuffer buffer {};
        };

        struct BoundImage
        {
            uint64_t byte_offset { 0 };
            uint32_t type_size { 0 };
            int32_t bind_type { -1 };
            uint32_t location { 0 };
            uint32_t set { 0 };
            uint32_t binding { 0 };
            HardwareImage image {};
        };

        struct RecordedDraw
        {
            RasterizerPipelineBase* pipeline;
            HardwareBuffer index_buffer {};
            HardwareBuffer vertex_buffer {};
            DrawIndexedParams params {};
            std::vector<std::byte> push_constant_data;
            std::vector<UniformBufferBindingData> uniform_buffers;
        };

        explicit VulkanRasterizerPipeline(RasterizerPipelineDesc desc,
                                          std::source_location source_location = std::source_location::current());
        ~VulkanRasterizerPipeline();

        [[nodiscard]] RasterizerPipelineDesc desc() const;
        [[nodiscard]] std::source_location source_location() const noexcept { return source_location_; }

        void set_extent(uint16_t width, uint16_t height);
        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t location = 0, uint32_t set = 0, uint32_t binding = 0);
        void set_depth_target(const HardwareImage& image);
        void add_auto_bind_entry(EmbeddedShader::AutoBindEntry entry);
        void bind_auto_resources();
        [[nodiscard]] std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries() const;
        void record(RasterizerPipelineBase* pipeline, const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
        void record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
        void clear_records();

        struct PreparedDraw
        {
            // UBO 与 bindless set 0-2 同形：管线侧按 (binding, buffer, range) 签名
            // 分配并写入一次持久 descriptor set，execution 侧只 bind，不再每 draw
            // push descriptor。按 set 升序排列。
            struct UniformSet
            {
                uint32_t set { 0 };
                VkDescriptorSet descriptor_set { VK_NULL_HANDLE };
            };

            VkPipelineLayout layout { VK_NULL_HANDLE };
            VkPipeline pipeline { VK_NULL_HANDLE };
            bool uses_bindless { false };
            std::vector<UniformSet> uniform_sets;
        };

        [[nodiscard]] PreparedDraw prepare_draw(VkDevice device,
                                                const std::array<VkFormat, 4>& color_formats,
                                                VkFormat depth_format,
                                                uint32_t vertex_stride,
                                                const DrawIndexedDesc& draw);

        [[nodiscard]] CommandBatch command_batch() const;
        // 直接录进 recorder：与 command_batch() 等价，但省掉中间的 CommandBatch /
        // StreamCommand(std::function) 以及批次 payload 的一次拷贝。
        void record_into(CommandRecorder& recorder) const;

    private:
        struct PipelineKey
        {
            VkDevice device { VK_NULL_HANDLE };
            std::array<VkFormat, 4> color_formats { VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED };
            VkFormat depth_format { VK_FORMAT_UNDEFINED };
            uint32_t vertex_stride { 0 };

            [[nodiscard]] friend bool operator==(const PipelineKey&, const PipelineKey&) noexcept = default;
        };

        struct PipelineState
        {
            struct DescriptorBindingLayout
            {
                uint32_t set { 0 };
                uint32_t binding { 0 };
                VkDescriptorType descriptor_type { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
            };

            // 一组已写入的 UBO descriptor：签名相同即可复用，签名不同另分配一份。
            // 已分配的 set 从不重写——in-flight 命令缓冲可能仍在引用它。
            struct UniformDescriptorSet
            {
                struct Signature
                {
                    uint32_t binding { 0 };
                    VkBuffer buffer { VK_NULL_HANDLE };
                    VkDeviceSize range { 0 };

                    [[nodiscard]] friend bool operator==(const Signature&, const Signature&) noexcept = default;
                };

                std::vector<Signature> signature;
                VkDescriptorPool pool { VK_NULL_HANDLE };
                VkDescriptorSet descriptor_set { VK_NULL_HANDLE };
            };

            struct DescriptorSetLayout
            {
                uint32_t set { 0 };
                std::vector<DescriptorBindingLayout> bindings;
                VkDescriptorSetLayout layout { VK_NULL_HANDLE };
                std::vector<UniformDescriptorSet> uniform_sets;
            };

            PipelineKey key {};
            std::vector<DescriptorSetLayout> descriptor_set_layouts;
            std::vector<VkDescriptorSetLayout> empty_descriptor_set_layouts;
            bool uses_bindless { false };
            VkPipelineLayout layout { VK_NULL_HANDLE };
            VkPipeline pipeline { VK_NULL_HANDLE };
        };

        // 一帧提交所需的全部内容，在一次加锁内取齐。
        struct DrawPlan
        {
            bool has_rendering_scope { false };
            RenderingDesc rendering {};
            DrawIndexedBatchDesc batch {};
        };

        [[nodiscard]] DrawPlan build_draw_plan() const;

        [[nodiscard]] uint32_t push_constant_size() const noexcept;
        // 按签名取回（必要时分配并写入）该 set 的持久 UBO descriptor set。
        [[nodiscard]] static VkDescriptorSet uniform_descriptor_set_unlocked(
            VkDevice device,
            PipelineState::DescriptorSetLayout& set_layout,
            const std::vector<PipelineState::UniformDescriptorSet::Signature>& signature,
            const std::string& debug_name);
        [[nodiscard]] PipelineState create_graphics_pipeline_unlocked(const PipelineKey& key) const;
        void destroy_pipeline_cache_unlocked() noexcept;

        RasterizerPipelineDesc desc_;
        std::source_location source_location_;

        mutable std::mutex mutex_;
        uint32_t width_ { 0 };
        uint32_t height_ { 0 };
        std::vector<std::byte> push_constant_data_;
        std::vector<UniformBufferBindingData> uniform_buffers_;
        // 与 uniform_buffers_ 同序的持久 GPU buffer，初始化时创建，批次内写入原地替换
        std::vector<HardwareBuffer> ubo_buffers_;
        std::vector<BoundBuffer> bound_buffers_;
        std::vector<BoundImage> bound_images_;
        HardwareImage depth_target_;
        std::vector<RecordedDraw> draws_;
        mutable std::vector<PipelineState> pipeline_cache_;
    };
}
