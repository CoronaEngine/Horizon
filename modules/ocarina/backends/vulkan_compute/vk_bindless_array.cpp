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

    buffer_slots_.host_buffer().resize(c_max_slots);
    tex3d_slots_.host_buffer().resize(c_max_slots);
    tex2d_slots_.host_buffer().resize(c_max_slots);

    // Initialize all slots to null sentinel
    std::fill(tex3d_slots_.begin(), tex3d_slots_.end(), 0u);
    std::fill(tex2d_slots_.begin(), tex2d_slots_.end(), 0u);

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
    OC_ERROR_IF(buffer_count_ >= c_max_slots, "Vulkan bindless buffer slots exhausted");
    size_t slot = buffer_count_++;
    buffer_slots_[slot] = {handle, offset, static_cast<uint64_t>(size)};
    return slot;
}

void VkBindlessArray::remove_buffer(handle_ty index) noexcept {
    if (index >= buffer_count_) return;
    for (size_t i = index; i + 1 < buffer_count_; ++i) buffer_slots_[i] = buffer_slots_[i + 1];
    buffer_slots_[--buffer_count_] = {};
}

void VkBindlessArray::set_buffer(handle_ty index, handle_ty handle,
                                  uint offset, size_t size) noexcept {
    OC_ASSERT(index < buffer_count_);
    buffer_slots_[index] = {handle, offset, (uint64_t)size};
}

size_t VkBindlessArray::buffer_num() const noexcept { return buffer_count_; }

size_t VkBindlessArray::buffer_slot_size() const noexcept {
    return sizeof(VkBindlessBufferSlot) * c_max_slots;
}

BufferUploadCommand *VkBindlessArray::upload_buffer_handles(bool async) const noexcept {
    return buffer_slots_.upload(async);
}

// ── Tex3D slots ───────────────────────────────────────────────────────────────

size_t VkBindlessArray::emplace_texture3d(handle_ty handle) noexcept {
    OC_ERROR_IF(tex3d_count_ >= c_max_slots, "Vulkan bindless texture slots exhausted");
    size_t slot = tex3d_count_++;
    tex3d_slots_[slot] = static_cast<uint32_t>(handle);
    return slot;
}

size_t VkBindlessArray::emplace_texture3d(TextureDesc desc) noexcept {
    return emplace_texture3d(static_cast<handle_ty>(desc.texture));
}

void VkBindlessArray::remove_texture3d(handle_ty index) noexcept {
    if (index >= tex3d_count_) return;
    for (size_t i = index; i + 1 < tex3d_count_; ++i) tex3d_slots_[i] = tex3d_slots_[i + 1];
    tex3d_slots_[--tex3d_count_] = 0u;
}

void VkBindlessArray::set_texture3d(handle_ty index, handle_ty handle) noexcept {
    OC_ASSERT(index < tex3d_count_);
    tex3d_slots_[index] = static_cast<uint32_t>(handle);
}

void VkBindlessArray::set_texture3d(handle_ty index, TextureDesc desc) noexcept {
    set_texture3d(index, desc.texture);
}

size_t VkBindlessArray::texture3d_num() const noexcept { return tex3d_count_; }

size_t VkBindlessArray::tex3d_slot_size() const noexcept {
    return sizeof(VkBindlessTexSlot) * c_max_slots;
}

BufferUploadCommand *VkBindlessArray::upload_texture3d_handles(bool async) const noexcept {
    return tex3d_slots_.upload(async);
}

// ── Tex2D slots ───────────────────────────────────────────────────────────────

size_t VkBindlessArray::emplace_texture2d(handle_ty handle) noexcept {
    OC_ERROR_IF(tex2d_count_ >= c_max_slots, "Vulkan bindless texture slots exhausted");
    size_t slot = tex2d_count_++;
    tex2d_slots_[slot] = static_cast<uint32_t>(handle);
    return slot;
}

size_t VkBindlessArray::emplace_texture2d(TextureDesc desc) noexcept {
    return emplace_texture2d(static_cast<handle_ty>(desc.texture));
}

void VkBindlessArray::remove_texture2d(handle_ty index) noexcept {
    if (index >= tex2d_count_) return;
    for (size_t i = index; i + 1 < tex2d_count_; ++i) tex2d_slots_[i] = tex2d_slots_[i + 1];
    tex2d_slots_[--tex2d_count_] = 0u;
}

void VkBindlessArray::set_texture2d(handle_ty index, handle_ty handle) noexcept {
    OC_ASSERT(index < tex2d_count_);
    tex2d_slots_[index] = static_cast<uint32_t>(handle);
}

void VkBindlessArray::set_texture2d(handle_ty index, TextureDesc desc) noexcept {
    set_texture2d(index, desc.texture);
}

size_t VkBindlessArray::texture2d_num() const noexcept { return tex2d_count_; }

size_t VkBindlessArray::tex2d_slot_size() const noexcept {
    return sizeof(VkBindlessTexSlot) * c_max_slots;
}

BufferUploadCommand *VkBindlessArray::upload_texture2d_handles(bool async) const noexcept {
    return tex2d_slots_.upload(async);
}

// ── buffer_view ───────────────────────────────────────────────────────────────

ByteBufferDesc VkBindlessArray::buffer_view(uint index) const noexcept {
    const auto &slot = buffer_slots_[index];
    ByteBufferDesc desc{};
    desc.handle = reinterpret_cast<std::byte *>(slot.device_address);
    desc.offset = slot.offset;
    desc.size   = slot.size;
    return desc;
}

}// namespace ocarina
