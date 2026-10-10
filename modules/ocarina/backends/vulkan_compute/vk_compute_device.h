//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDADevice: Vulkan compute-only device with no swapchain.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "rhi/resources/resource.h"
#include "core/concurrency/thread_safety.h"
#include "util.h"
#include "slang_compiler.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VkComputeCommandVisitor;
class VkComputeStream;

class VulkanComputeDevice : public Device::Impl {
public:
    // ── Type-on-device size/alignment (mirrors CUDADevice statics) ──────────
    static constexpr size_t size(Type::Tag tag) {
        using Tag = Type::Tag;
        switch (tag) {
            case Tag::BUFFER:         return sizeof(BufferDesc<>);
            case Tag::BYTE_BUFFER:    return sizeof(BufferDesc<>);
            case Tag::ACCEL:          return sizeof(handle_ty);   // VkDeviceAddress
            case Tag::TEXTURE3D:      return sizeof(TextureDesc);
            case Tag::TEXTURE2D:      return sizeof(TextureDesc);
            case Tag::BINDLESS_ARRAY: return sizeof(BindlessArrayDesc);
            default:                  return 0;
        }
    }
    static constexpr size_t size(const Type *type) {
        auto ret = size(type->tag());
        return ret == 0 ? type->size() : ret;
    }
    static constexpr size_t alignment(Type::Tag tag) {
        using Tag = Type::Tag;
        switch (tag) {
            case Tag::BUFFER:         return alignof(BufferDesc<>);
            case Tag::BYTE_BUFFER:    return alignof(BufferDesc<>);
            case Tag::ACCEL:          return alignof(handle_ty);
            case Tag::TEXTURE3D:      return alignof(TextureDesc);
            case Tag::TEXTURE2D:      return alignof(TextureDesc);
            case Tag::BINDLESS_ARRAY: return alignof(BindlessArrayDesc);
            default:                  return 0;
        }
    }
    static constexpr size_t alignment(const Type *type) {
        auto ret = alignment(type->tag());
        return ret == 0 ? type->alignment() : ret;
    }
    static size_t max_member_size(const Type *type) {
        auto ret = type->max_member_size();
        return ret == 0 ? sizeof(handle_ty) : ret;
    }

private:
    // ── Vulkan core handles ──────────────────────────────────────────────────
    VkInstance        instance_{VK_NULL_HANDLE};
    VkPhysicalDevice  phys_device_{VK_NULL_HANDLE};
    VkDevice          device_{VK_NULL_HANDLE};
    uint32_t          compute_queue_family_{0u};
    VkQueue           compute_queue_{VK_NULL_HANDLE};
    VmaAllocator      allocator_{nullptr};

    // ── RT extension support ─────────────────────────────────────────────────
    bool has_rt_{false};
    VkDeviceSize scratch_alignment_{1};
    std::recursive_mutex submission_mutex_;
    std::mutex streams_mutex_;
    vector<VkComputeStream *> streams_;

    // ── Descriptor pool for global bindless sets ─────────────────────────────
    VkDescriptorPool      desc_pool_{VK_NULL_HANDLE};
    VkDescriptorSetLayout global_layout_{VK_NULL_HANDLE};
    VkDescriptorSet       global_set_{VK_NULL_HANDLE};
    uint32_t next_texture_slot_{1u};
    vector<uint32_t> free_texture_slots_;

    // ── Sampler cache ────────────────────────────────────────────────────────
    static constexpr uint32_t c_sampler_count = 16u;
    std::array<VkSampler, c_sampler_count> samplers_{};

    // ── Buffer address map (handle_ty → VkBuffer for barrier bookkeeping) ────
    thread_safety<std::mutex>                          buffer_map_guard_;
    std::map<handle_ty, VkBufferAllocation>  buffer_map_;
    std::unordered_map<handle_ty, VkImageAllocation>   image_map_;

    std::unique_ptr<VkComputeCommandVisitor> cmd_visitor_;
    std::unique_ptr<SlangShaderCompiler>     compiler_;

    // ── Init helpers ─────────────────────────────────────────────────────────
    void init_instance() noexcept;
    void init_physical_device() noexcept;
    void init_logical_device() noexcept;
    void init_vma() noexcept;
    void init_descriptor_pool() noexcept;
    void init_samplers() noexcept;

public:
    static constexpr uint32_t max_texture_slots = 4096u;
    uint32_t allocate_texture_slot() noexcept;
    void release_texture_slot(uint32_t slot) noexcept;
    explicit VulkanComputeDevice(RHIContext *context);
    ~VulkanComputeDevice() noexcept;

    // ── Accessors used by sub-objects ────────────────────────────────────────
    [[nodiscard]] VkDevice         logical_device()   const noexcept { return device_; }
    [[nodiscard]] VkPhysicalDevice physical_device()  const noexcept { return phys_device_; }
    [[nodiscard]] VmaAllocator     vma_allocator()    const noexcept { return allocator_; }
    [[nodiscard]] VkQueue          compute_queue()    const noexcept { return compute_queue_; }
    [[nodiscard]] uint32_t         queue_family()     const noexcept { return compute_queue_family_; }
    [[nodiscard]] bool             has_rt()           const noexcept { return has_rt_; }
    [[nodiscard]] VkDeviceSize scratch_alignment() const noexcept { return scratch_alignment_; }
    [[nodiscard]] uint3 workgroup_size(const Function &function) const noexcept;
    [[nodiscard]] VkDescriptorSetLayout global_desc_layout() const noexcept { return global_layout_; }
    [[nodiscard]] VkDescriptorSet       global_desc_set()    const noexcept { return global_set_; }
    [[nodiscard]] VkSampler             sampler(uint32_t idx) const noexcept { return samplers_[idx % c_sampler_count]; }
    [[nodiscard]] SlangShaderCompiler  *compiler()           const noexcept { return compiler_.get(); }

    // Buffer allocation helpers
    [[nodiscard]] VkBufferAllocation allocate_buffer(size_t size, VkBufferUsageFlags usage,
                                                     bool host_visible = false,
                                                     const string &debug_name = "", VkDeviceSize alignment = 1) noexcept;
    void free_buffer(VkBufferAllocation &alloc) noexcept;
    [[nodiscard]] VkImageAllocation allocate_image(uint3 extent, VkFormat fmt,
                                                   uint32_t levels,
                                                   VkImageUsageFlags usage,
                                                   const string &debug_name = "", bool is_3d = false) noexcept;
    void free_image(VkImageAllocation &alloc) noexcept;

    // Submit one-shot command
    void immediate_submit(std::function<void(VkCommandBuffer)> &&fn) noexcept;
    void submit(VkCommandBuffer cb, VkFence fence, VkSemaphore timeline, uint64_t wait_value) noexcept;
    void synchronize_streams() noexcept;
    void register_stream(VkComputeStream *stream) noexcept;
    void unregister_stream(VkComputeStream *stream) noexcept;
    void run_commit_callback(const std::function<void(void *)> &callback) noexcept;

    // Buffer/image lookup by device address / pointer handle
    [[nodiscard]] VkBuffer get_vk_buffer(handle_ty addr, VkDeviceSize *offset = nullptr) noexcept;
    [[nodiscard]] VkImageAllocation &get_vk_image(handle_ty handle) noexcept;

    // ── Device::Impl interface ───────────────────────────────────────────────
    [[nodiscard]] handle_ty create_buffer(size_t size, const string &desc,
                                          bool exported = false) noexcept override;
    void destroy_buffer(handle_ty handle) noexcept override;

    [[nodiscard]] handle_ty create_texture3d(uint3 res, PixelStorage ps,
                                             uint level_num,
                                             const string &desc) noexcept override;
    [[nodiscard]] handle_ty create_texture2d(uint2 res, PixelStorage ps,
                                             uint level_num,
                                             const string &desc) noexcept override;
    [[nodiscard]] handle_ty create_texture2d_from_external(uint handle,
                                                           const string &desc) noexcept override;
    [[nodiscard]] handle_ty create_texture3d(Image *, const TextureViewCreation &) noexcept override { OC_NOT_IMPLEMENT_ERROR(create_texture3d_img); return 0; }
    [[nodiscard]] handle_ty create_texture2d(Image *, const TextureViewCreation &) noexcept override { OC_NOT_IMPLEMENT_ERROR(create_texture2d_img); return 0; }
    void destroy_texture3d(handle_ty handle) noexcept override;
    void destroy_texture2d(handle_ty handle) noexcept override;

    [[nodiscard]] handle_ty create_shader(const Function &function) noexcept override;
    [[nodiscard]] handle_ty create_shader_from_file(const string &, ShaderType,
                                                    const std::set<string> &) noexcept override { return InvalidUI64; }
    void destroy_shader(handle_ty handle) noexcept override;

    [[nodiscard]] handle_ty create_accel(AccelUsageTag usage_tag) noexcept override;
    void destroy_accel(handle_ty handle) noexcept override;

    [[nodiscard]] handle_ty create_stream() noexcept override;
    void destroy_stream(handle_ty handle) noexcept override;

    [[nodiscard]] handle_ty create_mesh(const MeshParams &params) noexcept override;
    void destroy_mesh(handle_ty handle) noexcept override;

    [[nodiscard]] handle_ty create_bindless_array() noexcept override;
    void destroy_bindless_array(handle_ty handle) noexcept override;

    [[nodiscard]] handle_ty create_texture_from_external(uint) noexcept override;
    [[nodiscard]] handle_ty create_buffer_from_external(uint) noexcept override;

    void init_rtx() noexcept override;
    [[nodiscard]] CommandVisitor *command_visitor() noexcept override;
    void submit_frame() noexcept override {}

    // ── Rasterization stubs (unused in compute-only backend) ─────────────────
    [[nodiscard]] VertexBuffer *create_vertex_buffer() noexcept override { return nullptr; }
    [[nodiscard]] IndexBuffer  *create_index_buffer(const void *, uint32_t, bool) noexcept override { return nullptr; }
    [[nodiscard]] RHIRenderPass *create_render_pass(const RenderPassCreation &) noexcept override { return nullptr; }
    void destroy_render_pass(RHIRenderPass *) noexcept override {}
    std::array<DescriptorSetLayout *, max_descriptor_sets_per_shader>
    create_descriptor_set_layout(void **, uint32_t) noexcept override { return {}; }
    void bind_pipeline(handle_ty) noexcept override {}
    [[nodiscard]] RHIPipeline *get_pipeline(const PipelineState &, RHIRenderPass *) noexcept override { return nullptr; }
    [[nodiscard]] DescriptorSet *get_global_descriptor_set(const string &) noexcept override { return nullptr; }
    void bind_descriptor_sets(DescriptorSet **, uint32_t, RHIPipeline *) noexcept override {}
    void begin_frame() noexcept override {}
    void end_frame() noexcept override {}

    void memory_allocate(handle_ty *handle, size_t size, bool exported) override;
    void memory_free(handle_ty *handle) override;
    [[nodiscard]] DevicePrecisionCaps precision_caps() const noexcept override;
    [[nodiscard]] uint64_t get_aligned_memory_size(handle_ty handle) const override;

#if _WIN32 || _WIN64
    [[nodiscard]] handle_ty import_handle(handle_ty handle, size_t size) override;
    [[nodiscard]] uint64_t  export_handle(handle_ty handle_) override;
#endif
};

}// namespace ocarina
