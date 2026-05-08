#include "vulkan_swapchain.h"
#include "vulkan_device.h"
#include "util.h"
#include "vulkan_texture.h"
#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif
namespace ocarina {

VulkanSwapchain::VulkanSwapchain() {

}

VulkanSwapchain::~VulkanSwapchain() {
}

void VulkanSwapchain::create_surface(VkInstance instance, uint64_t window_handle) {
#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surfaceCreateInfo.hwnd = HWND(window_handle);

    VkResult err = vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface_);
    VK_CHECK_RESULT(err);
#endif
}

void VulkanSwapchain::create_swapchain(const SwapChainCreation &creation, VulkanDevice *vulkan_device) {
    vulkan_device_ = vulkan_device;
    VkDevice device = vulkan_device->logicalDevice();
    VkPhysicalDevice physicalDevice = vulkan_device->physicalDevice();
    // Store the current swap chain handle so we can use it later on to ease up recreation
    VkSwapchainKHR oldSwapchain = swapchain_;

    // Get physical device surface properties and formats
    VkSurfaceCapabilitiesKHR surfCaps;
    VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface_, &surfCaps));

    // Get available present modes
    uint32_t presentModeCount;
    VK_CHECK_RESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface_, &presentModeCount, NULL));
    assert(presentModeCount > 0);

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_CHECK_RESULT(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface_, &presentModeCount, presentModes.data()));

    VkExtent2D swapchainExtent = {};
    // If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain
    if (surfCaps.currentExtent.width == (uint32_t)-1) {
        // If the surface size is undefined, the size is set to
        // the size of the images requested.
        swapchainExtent.width = creation.width;
        swapchainExtent.height = creation.height;
    } else {
        // If the surface size is defined, the swap chain size must match
        swapchainExtent = surfCaps.currentExtent;
    }

    // Select a present mode for the swapchain

    // The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
    // This mode waits for the vertical blank ("v-sync")
    VkPresentModeKHR swapchainPresentMode = get_preferred_presentmode(physicalDevice, surface_, creation.vsync);

    // Determine the number of images
    uint32_t desiredNumberOfSwapchainImages = std::max(creation.bufferCount, surfCaps.minImageCount + 1);
    desiredNumberOfSwapchainImages = surfCaps.maxImageCount > 0 ? std::min(desiredNumberOfSwapchainImages, surfCaps.maxImageCount) : desiredNumberOfSwapchainImages;

    if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount)) {
        desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
    }

    // Find the transformation of the surface
    VkSurfaceTransformFlagsKHR preTransform;
    if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        // We prefer a non-rotated transform
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
        preTransform = surfCaps.currentTransform;
    }

    // Find a supported composite alpha format (not all devices support alpha opaque)
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // Simply select the first composite alpha format available
    std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (auto &compositeAlphaFlag : compositeAlphaFlags) {
        if (surfCaps.supportedCompositeAlpha & compositeAlphaFlag) {
            compositeAlpha = compositeAlphaFlag;
            break;
        };
    }

    VkSwapchainCreateInfoKHR swapchainCI = {};
    swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCI.surface = surface_;
    swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
    swapchainCI.imageFormat = get_preferred_colorformat(creation.colorSpace);//get_vulkan_format(creation.format, creation.colorSpace == ColorSpace::SRGB);
    swapchainCI.imageColorSpace = colorspace_vulkan(creation.colorSpace);
    swapchainCI.imageExtent = {swapchainExtent.width, swapchainExtent.height};
    swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
    swapchainCI.imageArrayLayers = 1;
    swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCI.queueFamilyIndexCount = 0;
    swapchainCI.presentMode = swapchainPresentMode;
    // Setting oldSwapChain to the saved handle of the previous swapchain aids in resource reuse and makes sure that we can still present already acquired images
    swapchainCI.oldSwapchain = oldSwapchain;
    // Setting clipped to VK_TRUE allows the implementation to discard rendering outside of the surface area
    swapchainCI.clipped = VK_TRUE;
    swapchainCI.compositeAlpha = compositeAlpha;

    // Enable transfer source on swap chain images if supported
    if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
        swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    // Enable transfer destination on swap chain images if supported
    if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
        swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VkResult err = vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain_);
    VK_CHECK_RESULT(err);

    // If an existing swap chain is re-created, destroy the old swap chain
    // This also cleans up all the presentable images
    if (oldSwapchain != VK_NULL_HANDLE) {
        release_backbuffers();
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
    }

    resolution_ = uint2(creation.width, creation.height);
    setup_backbuffers(swapchainCI);
    setup_depth_stencil();
}

void VulkanSwapchain::release()
{
    VkDevice device = vulkan_device_->logicalDevice();
    release_backbuffers();
    release_depth_stencil();
    vkDestroySwapchainKHR(device, swapchain_, nullptr);
    vkDestroySurfaceKHR(vulkan_device_->get_instance(), surface_, nullptr);
}

VkResult VulkanSwapchain::queue_present(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore) {
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = NULL;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    // Check if a wait semaphore has been specified to wait for before presenting the image
    if (waitSemaphore != VK_NULL_HANDLE) {
        presentInfo.pWaitSemaphores = &waitSemaphore;
        presentInfo.waitSemaphoreCount = 1;
    }
    return vkQueuePresentKHR(queue, &presentInfo);
}

VkResult VulkanSwapchain::aquire_next_image(VkSemaphore present_complete_semaphore, uint32_t* image_index)
{
    return vkAcquireNextImageKHR(vulkan_device_->logicalDevice(), swapchain_, UINT64_MAX, present_complete_semaphore, (VkFence)nullptr, image_index);
}

void VulkanSwapchain::setup_backbuffers(const VkSwapchainCreateInfoKHR &swapChainCreateInfo) {
    VkDevice device = vulkan_device_->logicalDevice();
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, nullptr);

    std::vector<VkImage> images;
    images.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, &images.front());

    for (auto image : images) {
        // We pass a VK_NULL_HANDLE for the device since vkImages are owned by the swapchain
        uint3 res = {swapChainCreateInfo.imageExtent.width, swapChainCreateInfo.imageExtent.height, 1};

        //create buffer imageview
        VkImageView imageView = VK_NULL_HANDLE;
        VkImageViewCreateInfo colorAttachmentView = {};
        colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        colorAttachmentView.pNext = NULL;
        colorAttachmentView.format = swapChainCreateInfo.imageFormat;
        colorAttachmentView.components = {
            VK_COMPONENT_SWIZZLE_R,
            VK_COMPONENT_SWIZZLE_G,
            VK_COMPONENT_SWIZZLE_B,
            VK_COMPONENT_SWIZZLE_A};
        colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorAttachmentView.subresourceRange.baseMipLevel = 0;
        colorAttachmentView.subresourceRange.levelCount = 1;
        colorAttachmentView.subresourceRange.baseArrayLayer = 0;
        colorAttachmentView.subresourceRange.layerCount = 1;
        colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorAttachmentView.flags = 0;
        colorAttachmentView.image = image;

        VkResult err = vkCreateImageView(device, &colorAttachmentView, nullptr, &imageView);
        VK_CHECK_RESULT(err);
        backbuffers_.push_back({image, imageView});
    }
}

void VulkanSwapchain::setup_depth_stencil()
{
    VkDevice device = vulkan_device_->logicalDevice();

    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = depth_format_;
    imageCI.extent = {(uint32_t)resolution_.x, (uint32_t)resolution_.y, 1};
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &depth_stencil.image));
    VkMemoryRequirements mem_reqs{};
    vkGetImageMemoryRequirements(device, depth_stencil.image, &mem_reqs);

    VkMemoryAllocateInfo memAllloc{};
    memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllloc.allocationSize = mem_reqs.size;
    memAllloc.memoryTypeIndex = vulkan_device_->get_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device, &memAllloc, nullptr, &depth_stencil.mem));
    VK_CHECK_RESULT(vkBindImageMemory(device, depth_stencil.image, depth_stencil.mem, 0));

    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.image = depth_stencil.image;
    imageViewCI.format = depth_format_;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT
    if (depth_format_ >= VK_FORMAT_D16_UNORM_S8_UINT) {
        imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &depth_stencil.view));
}

void VulkanSwapchain::release_backbuffers()
{
    for (size_t i = 0; i < backbuffers_.size(); i++) {
        vkDestroyImageView(vulkan_device_->logicalDevice(), backbuffers_[i].imageView_, nullptr);
    }

    backbuffers_.clear();
}

void VulkanSwapchain::release_depth_stencil()
{
    VkDevice device = vulkan_device_->logicalDevice();
    if (depth_stencil.view != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan_device_->logicalDevice(), depth_stencil.view, nullptr);
        depth_stencil.view = VK_NULL_HANDLE;
    }
    if (depth_stencil.image != VK_NULL_HANDLE) {
        vkDestroyImage(vulkan_device_->logicalDevice(), depth_stencil.image, nullptr);
        depth_stencil.image = VK_NULL_HANDLE;
    }
    if (depth_stencil.mem != VK_NULL_HANDLE) {
        vkFreeMemory(vulkan_device_->logicalDevice(), depth_stencil.mem, nullptr);
        depth_stencil.mem = VK_NULL_HANDLE;
    }
}

VkPresentModeKHR VulkanSwapchain::get_preferred_presentmode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool vsync)
{
    // Default mode, only one guaranteed to be supported
    VkPresentModeKHR selectedMode = VK_PRESENT_MODE_FIFO_KHR;

    // Get available present modes
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

    // In some weird conditions, some Nvidia driver just doesnt touch presentModeCount, leaving us with an unitialized value.
    // We need to skip following code otherwise Vector<VkPresentModeKHR> presentModes(presentModeCount) will fail.
    // This is not going to prevent the application from running.
    if (presentModeCount == 0) {
        return selectedMode;
    }

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, &presentModes.front());

    if (!vsync) {
        for (auto presentMode : presentModes) {
            // Favor immediate mode for no vsync (will tear but gives lowest latency)
            if (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                selectedMode = presentMode;
                break;
            } else if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                // If no immediate mode, settle for mailbox
                selectedMode = presentMode;
            }
        }
    } else {
        for (auto presentMode : presentModes) {
            if (presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
                // Favor adaptive vsync if available
                selectedMode = presentMode;
                break;
            }
        }
    }

    return selectedMode;
}

VkFormat VulkanSwapchain::get_preferred_colorformat(ColorSpace colorSpace)
{
    VkPhysicalDevice physicalDevice = vulkan_device_->physicalDevice();
    uint32_t formatCount;
    VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface_, &formatCount, nullptr));
    assert(formatCount > 0);

    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    VK_CHECK_RESULT(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface_, &formatCount, surfaceFormats.data()));
    VkColorSpaceKHR vulkanColorSpace = colorspace_vulkan(colorSpace);
    for (uint32_t i = 0; i < formatCount; ++i)
    {
        if (vulkanColorSpace == surfaceFormats[i].colorSpace)
        {
            color_format_ = surfaceFormats[i].format;
            return color_format_;
        }
    }

    return VK_FORMAT_UNDEFINED;
}

}// namespace ocarina