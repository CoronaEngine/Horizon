//
// Created by Zero on 06/08/2022.
//

#include "vulkan_rendertarget.h"
#include "vulkan_device.h"
#include "util.h"

namespace ocarina {

VulkanRenderTarget::VulkanRenderTarget(VulkanDevice *device, const RenderTargetCreation &creation) : RenderTarget(creation) {
    device_ = device->logicalDevice();
    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.extent.width = creation.width;
    image.extent.height = creation.height;
    image.extent.depth = 1;
    image.mipLevels = creation.mipmaps;
    image.arrayLayers = creation.array_size;
    image.samples = get_vulkan_sample_count(creation.msaa_samples);
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.format = get_vulkan_format(creation.format, creation.srgb);                                     
    image.usage = get_vulkan_image_usage_flag((uint32_t)creation.usage);
    VK_CHECK_RESULT(vkCreateImage(device_, &image, nullptr, &image_));

    VkImageAspectFlags aspect_mask = 0;
    if (image.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    else if (image.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else {
        aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkImageViewCreateInfo image_view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    image_view.format = image.format;
    image_view.image = image_;  
    image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view.subresourceRange.aspectMask = aspect_mask;
    image_view.subresourceRange.baseMipLevel = 0;
    image_view.subresourceRange.levelCount = creation.mipmaps;
    image_view.subresourceRange.baseArrayLayer = 0;
    image_view.subresourceRange.layerCount = 1;

    VK_CHECK_RESULT(vkCreateImageView(device_, &image_view, nullptr, &image_view_));
}

VulkanRenderTarget::~VulkanRenderTarget() {
    // Cleanup code for Vulkan render target
    if (image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, image_view_, nullptr);
        image_view_ = VK_NULL_HANDLE;
    }

    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
}
}// namespace ocarina