#pragma once

#include "hardware_wrapper_vulkan/hardware/command.h"
#include "horizon.h"

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <vector>

namespace Corona::Horizon
{
    class VulkanComputePipeline
    {
    public:
        struct BoundBuffer
        {
            uint32_t set { 0 };
            uint32_t binding { 0 };
            HardwareBuffer buffer {};
            AccessKind access { AccessKind::ReadWrite };
        };

        struct BoundImage
        {
            uint32_t set { 0 };
            uint32_t binding { 0 };
            HardwareImage image {};
            AccessKind access { AccessKind::ReadWrite };
        };

        struct Snapshot
        {
            DispatchDesc dispatch;
            std::vector<BoundBuffer> buffers;
            std::vector<BoundImage> images;
        };

        struct PreparedDispatch
        {
            // 非 UBO 资源（storage buffer/image）强制走 bindless（持久 set 0-2 + push
            // constant 索引）。UBO 与之同形：管线侧按 (binding, buffer, range) 签名
            // 分配并写入一次持久 descriptor set，execution 侧只 bind。按 set 升序排列。
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

        VulkanComputePipeline(ComputePipelineDesc desc,
                              ComputePipelineShaders shaders,
                              std::source_location source_location = std::source_location::current());
        ~VulkanComputePipeline();

        [[nodiscard]] ComputePipelineDesc desc() const;
        // shader 侧独立取用：rebuild 时需要原样搬运，不必连状态一起深拷贝。
        [[nodiscard]] ComputePipelineShaders shaders() const;
        // 只取 pipelineObject（shared_ptr 浅拷贝），避免整份深拷贝。
        [[nodiscard]] std::shared_ptr<EmbeddedShader::ComputePipelineObject> pipeline_object() const;
        // 反射里的 workgroup local size，缺失时回落到 desc_.thread_group_size。
        [[nodiscard]] ktm::uvec3 resolved_thread_group_size() const;
        [[nodiscard]] std::source_location source_location() const noexcept { return source_location_; }

        void bind_auto_resources();
        void bind_auto_image(EmbeddedShader::AutoBindEntry entry, const HardwareImage& image);
        void set_dispatch(uint16_t groups_x, uint16_t groups_y, uint16_t groups_z);
        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);

        [[nodiscard]] Snapshot snapshot() const;
        [[nodiscard]] CommandBatch command_batch(ComputePipelineBase& pipeline) const;
        [[nodiscard]] PreparedDispatch prepare_dispatch(VkDevice device, const DispatchDesc& dispatch);

        struct BindingLayout
        {
            uint32_t set { 0 };
            uint32_t binding { 0 };
            DispatchBindingKind kind { DispatchBindingKind::StorageBuffer };

            [[nodiscard]] friend bool operator==(const BindingLayout&, const BindingLayout&) noexcept = default;
        };

    private:
        struct PipelineState
        {
            struct DescriptorBindingLayout
            {
                uint32_t set { 0 };
                uint32_t binding { 0 };
                VkDescriptorType descriptor_type { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
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

            VkDevice device { VK_NULL_HANDLE };
            std::vector<BindingLayout> bindings;
            std::vector<DescriptorSetLayout> descriptor_set_layouts;
            std::vector<VkDescriptorSetLayout> empty_descriptor_set_layouts;
            bool uses_bindless { false };
            VkPipelineLayout layout { VK_NULL_HANDLE };
            VkPipeline pipeline { VK_NULL_HANDLE };
        };

        [[nodiscard]] uint32_t push_constant_size() const noexcept;
        // 按签名取回（必要时分配并写入）该 set 的持久 UBO descriptor set。
        [[nodiscard]] static VkDescriptorSet uniform_descriptor_set_unlocked(
            VkDevice device,
            PipelineState::DescriptorSetLayout& set_layout,
            const std::vector<PipelineState::UniformDescriptorSet::Signature>& signature,
            const std::string& debug_name);
        [[nodiscard]] PipelineState create_pipeline_state_unlocked(VkDevice device, std::vector<BindingLayout> bindings) const;
        [[nodiscard]] PipelineState& pipeline_state_unlocked(VkDevice device, const DispatchDesc& dispatch);
        void destroy_pipeline_cache_unlocked() noexcept;
        // 把 uniform_buffers_ 的 CPU 影子数据落到本帧槽的持久 buffer，并让
        // gpu_buffer 指向该槽。槽未变且影子未脏时是空操作。
        void sync_ubo_slot_unlocked() const;

        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries_;
        ComputePipelineShaders shaders_;
        ComputePipelineDesc desc_;
        std::source_location source_location_;

        mutable std::mutex mutex_;
        DispatchDesc dispatch_ {};
        std::vector<std::byte> push_constant_data_;
        // CPU 影子数据 + 本帧槽的 gpu_buffer 句柄。setter 只写影子并置脏，
        // 真正的 flush 推迟到 snapshot()（const，故 mutable）。
        mutable std::vector<UniformBufferBindingData> uniform_buffers_;
        // 与 uniform_buffers_ 同序的持久 GPU buffer 环，每条 binding 一环、
        // 环长 = frame_ring_size()。第 i 帧写槽 i%N，不覆写仍在飞的帧读的槽。
        mutable std::vector<std::vector<HardwareBuffer>> ubo_rings_;
        mutable int64_t ubo_slot_ { -1 };
        mutable bool ubo_dirty_ { true };
        std::vector<BoundBuffer> bound_buffers_;
        std::vector<BoundImage> bound_images_;
        mutable std::vector<PipelineState> pipeline_cache_;
    };
}
