//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_bindless_array.cpp.
//

#include "vk_bindless_array.h"
#include "vk_compute_device.h"
#include "rhi/command.h"
#include "util.h"

namespace ocarina {

VkBindlessArray::VkBindlessArray(VulkanComputeDevice *device)
    : device_(device) {
    buffer_slots_ = Managed<VkBindlessBufferSlot>(
        device, c_max_slots, "bindless_buf_slots");
    tex3d_slots_ = Managed<VkBindlessTexSlot>(
        device, c_max_slots, "bindless_tex3d_slots");
    tex2d_slots_ = Managed<VkBindlessTexSlot>(
        device, c_max_slots, "bindless_tex2d_slots");

    // Initialize all slots to null sentinel
    std::fill(tex3d_slots_.begin(), tex3d_slots_.end(), 0xFFFFFFFFu);
    std::fill(tex2d_slots_.begin(), tex2d_slots_.end(), 0xFFFFFFFFu);

    slot_soa_.buffer_slot = buffer_slots_.handle();
    slot_soa_.tex3d_slot  = tex3d_slots_.handle();
    slot_soa_.tex2d_slot  = tex2d_slots_.handle();
}

CommandBatch VkBindlessArray::update_slotSOA(bool async) noexcept {
    CommandBatch batch;
    batch.push_back(upload_buffer_handles(async));
    batch.push_back(upload_texture3d_handles(async));
    batch.push_back(upload_texture2d_handles(async));
    return batch;
}

// ── Buffer slots ──────────────────────────────────────────────────────────────

size_t VkBindlessArray::emplace_buffer(handle_ty handle, uint offset,
                                        size_t size) noexcept {
    for (size_t i = 0; i < c_max_slots; ++i) {
        if (buffer_slots_[i].device_address == 0) {
            buffer_slots_[i] = {handle, offset, (uint64_t)size};
            return i;
        }
    }
    OC_ERROR("VkBindlessArray: buffer slots exhausted");
    return 0;
}

void VkBindlessArray::remove_buffer(handle_ty index) noexcept {
    buffer_slots_[index] = {};
}

void VkBindlessArray::set_buffer(handle_ty index, handle_ty handle,
                                  uint offset, size_t size) noexcept {
    buffer_slots_[index] = {handle, offset, (uint64_t)size};
}

size_t VkBindlessArray::buffer_num() const noexcept {
    size_t n = 0;
    for (size_t i = 0; i < c_max_slots; ++i)
        if (buffer_slots_[i].device_address) ++n;
    return n;
}

size_t VkBindlessArray::buffer_slot_size() const noexcept {
    return sizeof(VkBindlessBufferSlot);
}

BufferUploadCommand *VkBindlessArray::upload_buffer_handles(bool async) const noexcept {
    return BufferUploadCommand::create(
        slot_soa_.buffer_slot, 0,
        buffer_slots_.host_ptr(), buffer_slots_.size_in_bytes(), async);
}

// ── Tex3D slots ───────────────────────────────────────────────────────────────

size_t VkBindlessArray::emplace_texture3d(handle_ty handle) noexcept {
    for (size_t i = 0; i < c_max_slots; ++i) {
        if (tex3d_slots_[i] == 0xFFFFFFFFu) {
            tex3d_slots_[i] = static_cast<uint32_t>(handle);
            return i;
        }
    }
    OC_ERROR("VkBindlessArray: tex3d slots exhausted");
    return 0;
}

size_t VkBindlessArray::emplace_texture3d(TextureDesc desc) noexcept {
    return emplace_texture3d(static_cast<handle_ty>(desc.texture));
}

void VkBindlessArray::remove_texture3d(handle_ty index) noexcept {
    tex3d_slots_[index] = 0xFFFFFFFFu;
}

void VkBindlessArray::set_texture3d(handle_ty index, handle_ty handle) noexcept {
    tex3d_slots_[index] = static_cast<uint32_t>(handle);
}

void VkBindlessArray::set_texture3d(handle_ty index, TextureDesc desc) noexcept {
    tex3d_slots_[index] = static_cast<uint32_t>(desc.texture);
}

size_t VkBindlessArray::texture3d_num() const noexcept {
    size_t n = 0;
    for (size_t i = 0; i < c_max_slots; ++i)
        if (tex3d_slots_[i] != 0xFFFFFFFFu) ++n;
    return n;
}

size_t VkBindlessArray::tex3d_slot_size() const noexcept {
    return sizeof(VkBindlessTexSlot);
}

BufferUploadCommand *VkBindlessArray::upload_texture3d_handles(bool async) const noexcept {
    return BufferUploadCommand::create(
        slot_soa_.tex3d_slot, 0,
        tex3d_slots_.host_ptr(), tex3d_slots_.size_in_bytes(), async);
}

// ── Tex2D slots ───────────────────────────────────────────────────────────────

size_t VkBindlessArray::emplace_texture2d(handle_ty handle) noexcept {
    for (size_t i = 0; i < c_max_slots; ++i) {
        if (tex2d_slots_[i] == 0xFFFFFFFFu) {
            tex2d_slots_[i] = static_cast<uint32_t>(handle);
            return i;
        }
    }
    OC_ERROR("VkBindlessArray: tex2d slots exhausted");
    return 0;
}

size_t VkBindlessArray::emplace_texture2d(TextureDesc desc) noexcept {
    return emplace_texture2d(static_cast<handle_ty>(desc.texture));
}

void VkBindlessArray::remove_texture2d(handle_ty index) noexcept {
    tex2d_slots_[index] = 0xFFFFFFFFu;
}

void VkBindlessArray::set_texture2d(handle_ty index, handle_ty handle) noexcept {
    tex2d_slots_[index] = static_cast<uint32_t>(handle);
}

void VkBindlessArray::set_texture2d(handle_ty index, TextureDesc desc) noexcept {
    tex2d_slots_[index] = static_cast<uint32_t>(desc.texture);
}

size_t VkBindlessArray::texture2d_num() const noexcept {
    size_t n = 0;
    for (size_t i = 0; i < c_max_slots; ++i)
        if (tex2d_slots_[i] != 0xFFFFFFFFu) ++n;
    return n;
}

size_t VkBindlessArray::tex2d_slot_size() const noexcept {
    return sizeof(VkBindlessTexSlot);
}

BufferUploadCommand *VkBindlessArray::upload_texture2d_handles(bool async) const noexcept {
    return BufferUploadCommand::create(
        slot_soa_.tex2d_slot, 0,
        tex2d_slots_.host_ptr(), tex2d_slots_.size_in_bytes(), async);
}

// ── buffer_view ───────────────────────────────────────────────────────────────

ByteBufferDesc VkBindlessArray::buffer_view(uint index) const noexcept {
    const auto &slot = buffer_slots_[index];
    ByteBufferDesc desc{};
    desc.handle = slot.device_address;
    desc.offset = slot.offset;
    desc.size   = slot.size;
    return desc;
}

}// namespace ocarina
