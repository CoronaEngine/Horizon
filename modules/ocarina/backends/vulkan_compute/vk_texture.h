//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDATexture / CUDATexture2D / CUDATexture3D.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/texture.h"
#include "util.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;

// ── VkFormat helpers ─────────────────────────────────────────────────────────
[[nodiscard]] VkFormat pixel_storage_to_vk_format(PixelStorage ps) noexcept;
[[nodiscard]] uint32_t pixel_storage_bytes(PixelStorage ps) noexcept;

// ── Shared base ──────────────────────────────────────────────────────────────

class VkTexture : public Texture::Impl {
protected:
    mutable TextureDesc descriptor_{};
    VulkanComputeDevice *device_{};
    uint32_t            level_num_{1u};
    uint3               res_{};
    VkImageAllocation   image_{};

    // Descriptor-array slot indices written into TextureDesc.
    uint32_t sample_slot_{0u};
    uint32_t storage_slot_{0u};

    explicit VkTexture(VulkanComputeDevice *device) : device_(device) {}
    VkTexture(VulkanComputeDevice *device, uint3 res, PixelStorage ps, uint level_num);

    void register_descriptors(bool is_3d) noexcept;

public:
    [[nodiscard]] uint3 resolution() const noexcept override { return res_; }
    [[nodiscard]] const TextureDesc &descriptor() const noexcept override { return descriptor_; }
    [[nodiscard]] handle_ty tex_handle() const noexcept override { return descriptor_.texture; }
    [[nodiscard]] const void *handle_ptr() const noexcept override { return &descriptor_; }
    [[nodiscard]] handle_ty array_handle() const noexcept override { return reinterpret_cast<handle_ty>(image_.image); }
    [[nodiscard]] PixelStorage pixel_storage() const noexcept override { return descriptor_.pixel_storage; }
    [[nodiscard]] size_t data_size() const noexcept override { return sizeof(TextureDesc); }
    [[nodiscard]] size_t data_alignment() const noexcept override { return alignof(TextureDesc); }
    [[nodiscard]] size_t max_member_size() const noexcept override { return sizeof(handle_ty); }
    [[nodiscard]] bool is_external() const noexcept override { return false; }

    [[nodiscard]] VkImage vk_image() const noexcept { return image_.image; }
    [[nodiscard]] VkImageView vk_view() const noexcept { return image_.view; }
    [[nodiscard]] VkImageView vk_storage_view() const noexcept { return image_.storage_view; }

    ~VkTexture() override;
};

class VkTexture2D final : public VkTexture {
public:
    VkTexture2D(VulkanComputeDevice *device, uint3 res, PixelStorage ps, uint level_num);
    VkTexture2D(VulkanComputeDevice *device, uint external_handle); // interop
};

class VkTexture3D final : public VkTexture {
public:
    VkTexture3D(VulkanComputeDevice *device, uint3 res, PixelStorage ps, uint level_num);
};

}// namespace ocarina
