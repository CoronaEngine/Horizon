//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDATexture/CUDATexture2D/CUDATexture3D.
//

#include "vk_texture.h"
#include "vk_compute_device.h"

namespace ocarina {

// ── Format mapping ────────────────────────────────────────────────────────────

VkFormat pixel_storage_to_vk_format(PixelStorage ps) noexcept {
    switch (ps) {
        case PixelStorage::BYTE1:   return VK_FORMAT_R8_UNORM;
        case PixelStorage::BYTE2:   return VK_FORMAT_R8G8_UNORM;
        case PixelStorage::BYTE4:   return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelStorage::SHORT1:  return VK_FORMAT_R16_UNORM;
        case PixelStorage::SHORT2:  return VK_FORMAT_R16G16_UNORM;
        case PixelStorage::SHORT4:  return VK_FORMAT_R16G16B16A16_UNORM;
        case PixelStorage::INT1:    return VK_FORMAT_R32_UINT;
        case PixelStorage::INT2:    return VK_FORMAT_R32G32_UINT;
        case PixelStorage::INT4:    return VK_FORMAT_R32G32B32A32_UINT;
        case PixelStorage::HALF1:   return VK_FORMAT_R16_SFLOAT;
        case PixelStorage::HALF2:   return VK_FORMAT_R16G16_SFLOAT;
        case PixelStorage::HALF4:   return VK_FORMAT_R16G16B16A16_SFLOAT;
        case PixelStorage::FLOAT1:  return VK_FORMAT_R32_SFLOAT;
        case PixelStorage::FLOAT2:  return VK_FORMAT_R32G32_SFLOAT;
        case PixelStorage::FLOAT4:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        default:                    return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

uint32_t pixel_storage_bytes(PixelStorage ps) noexcept {
    switch (ps) {
        case PixelStorage::BYTE1:  return 1;
        case PixelStorage::BYTE2:  return 2;
        case PixelStorage::BYTE4:  return 4;
        case PixelStorage::SHORT1: return 2;
        case PixelStorage::SHORT2: return 4;
        case PixelStorage::SHORT4: return 8;
        case PixelStorage::INT1:   return 4;
        case PixelStorage::INT2:   return 8;
        case PixelStorage::INT4:   return 16;
        case PixelStorage::HALF1:  return 2;
        case PixelStorage::HALF2:  return 4;
        case PixelStorage::HALF4:  return 8;
        case PixelStorage::FLOAT1: return 4;
        case PixelStorage::FLOAT2: return 8;
        case PixelStorage::FLOAT4: return 16;
        default:                   return 4;
    }
}

// ── VkTexture base ────────────────────────────────────────────────────────────

VkTexture::VkTexture(VulkanComputeDevice *device, uint3 res,
                     PixelStorage ps, uint level_num)
    : device_(device), res_(res), level_num_(level_num) {
    descriptor_.pixel_storage = ps;
}

void VkTexture::register_descriptors(bool is_3d) noexcept {
    // Use static per-device counter. In a real implementation this would be
    // managed by VulkanComputeDevice's descriptor allocator; we use a simple
    // atomic bump here for clarity.
    static std::atomic<uint32_t> s_next_sample_slot{1u};  // 0 reserved as null
    static std::atomic<uint32_t> s_next_storage_slot{1u};

    sample_slot_  = s_next_sample_slot.fetch_add(1u);
    storage_slot_ = s_next_storage_slot.fetch_add(1u);
    descriptor_.texture = sample_slot_;
    descriptor_.surface = storage_slot_;

    VkDescriptorImageInfo sample_info{};
    sample_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sample_info.imageView   = image_.view;
    sample_info.sampler     = device_->sampler(0);

    VkDescriptorImageInfo storage_info{};
    storage_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storage_info.imageView   = image_.storage_view != VK_NULL_HANDLE
                                   ? image_.storage_view : image_.view;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = device_->global_desc_set();
    writes[0].dstBinding      = 0;
    writes[0].dstArrayElement = sample_slot_;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo      = &sample_info;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = device_->global_desc_set();
    writes[1].dstBinding      = 1;
    writes[1].dstArrayElement = storage_slot_;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo      = &storage_info;

    vkUpdateDescriptorSets(device_->logical_device(), 2, writes, 0, nullptr);
}

VkTexture::~VkTexture() {
    device_->free_image(image_);
}

// ── VkTexture2D ───────────────────────────────────────────────────────────────

VkTexture2D::VkTexture2D(VulkanComputeDevice *device, uint3 res,
                         PixelStorage ps, uint level_num)
    : VkTexture(device, res, ps, level_num) {
    VkFormat fmt = pixel_storage_to_vk_format(ps);
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_ = device_->allocate_image({res.x, res.y, 1u}, fmt, level_num, usage, "tex2d");
    register_descriptors(false);
}

VkTexture2D::VkTexture2D(VulkanComputeDevice *device, uint external_handle)
    : VkTexture(device) {
    OC_NOT_IMPLEMENT_ERROR(VkTexture2D_external);
}

// ── VkTexture3D ───────────────────────────────────────────────────────────────

VkTexture3D::VkTexture3D(VulkanComputeDevice *device, uint3 res,
                         PixelStorage ps, uint level_num)
    : VkTexture(device, res, ps, level_num) {
    VkFormat fmt = pixel_storage_to_vk_format(ps);
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_ = device_->allocate_image(res, fmt, level_num, usage, "tex3d");
    register_descriptors(true);
}

}// namespace ocarina
