//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDABindlessArray: descriptor-index-based bindless arrays.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/managed.h"
#include "rhi/resources/bindless_array.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;

// Device-side slot element for buffer bindless
struct VkBindlessBufferSlot {
    uint64_t device_address{0};
    uint32_t offset{0};
    uint64_t size{0};
};

// Device-side slot for texture bindless: just a descriptor array index (uint32)
using VkBindlessTexSlot = uint32_t;

class VkBindlessArray final : public BindlessArray::Impl {
private:
    VulkanComputeDevice *device_{};
    BindlessArrayDesc    slot_soa_{};

    // Host-side slot tables uploaded as SSBOs
    Managed<VkBindlessBufferSlot> buffer_slots_;
    Managed<VkBindlessTexSlot>    tex3d_slots_;
    Managed<VkBindlessTexSlot>    tex2d_slots_;

    static constexpr size_t c_max_slots = c_max_slot_num;

public:
    explicit VkBindlessArray(VulkanComputeDevice *device);

    [[nodiscard]] const void *handle_ptr()   const noexcept override { return &slot_soa_; }
    [[nodiscard]] size_t max_member_size()   const noexcept override { return sizeof(uint64_t); }
    [[nodiscard]] size_t data_size()         const noexcept override { return sizeof(BindlessArrayDesc); }
    [[nodiscard]] size_t data_alignment()    const noexcept override { return alignof(BindlessArrayDesc); }

    [[nodiscard]] CommandBatch update_slotSOA(bool async) noexcept override;

    // Buffer slots
    [[nodiscard]] size_t emplace_buffer(handle_ty handle, uint offset,
                                        size_t size) noexcept override;
    void   remove_buffer(handle_ty index) noexcept override;
    void   set_buffer(handle_ty index, handle_ty handle,
                      uint offset, size_t size) noexcept override;
    [[nodiscard]] size_t buffer_num()          const noexcept override;
    [[nodiscard]] size_t buffer_slot_size()    const noexcept override;
    [[nodiscard]] BufferUploadCommand *upload_buffer_handles(bool async) const noexcept override;

    // Tex3D slots
    [[nodiscard]] size_t emplace_texture3d(handle_ty handle) noexcept override;
    [[nodiscard]] size_t emplace_texture3d(TextureDesc desc) noexcept override;
    void remove_texture3d(handle_ty index) noexcept override;
    void set_texture3d(handle_ty index, handle_ty handle) noexcept override;
    void set_texture3d(handle_ty index, TextureDesc desc) noexcept override;
    [[nodiscard]] size_t texture3d_num()       const noexcept override;
    [[nodiscard]] size_t tex3d_slot_size()     const noexcept override;
    [[nodiscard]] BufferUploadCommand *upload_texture3d_handles(bool async) const noexcept override;

    // Tex2D slots
    [[nodiscard]] size_t emplace_texture2d(handle_ty handle) noexcept override;
    [[nodiscard]] size_t emplace_texture2d(TextureDesc desc) noexcept override;
    void remove_texture2d(handle_ty index) noexcept override;
    void set_texture2d(handle_ty index, handle_ty handle) noexcept override;
    void set_texture2d(handle_ty index, TextureDesc desc) noexcept override;
    [[nodiscard]] size_t texture2d_num()       const noexcept override;
    [[nodiscard]] size_t tex2d_slot_size()     const noexcept override;
    [[nodiscard]] BufferUploadCommand *upload_texture2d_handles(bool async) const noexcept override;

    [[nodiscard]] ByteBufferDesc buffer_view(uint index) const noexcept override;
};

}// namespace ocarina
