#pragma once
#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "rhi/graphics_descriptions.h"
#include <vulkan/vulkan.h>
namespace ocarina {
class VulkanTexture;
class VulkanDevice;

struct SwapChainBuffer
{
    VkImage image_;
    VkImageView imageView_;
};

class VulkanSwapchain : public concepts::Noncopyable {
public:
    VulkanSwapchain();
    ~VulkanSwapchain();
    void create_surface(VkInstance instance, uint64_t window_handle);
    void create_swapchain(const SwapChainCreation &creation, VulkanDevice* vulkan_device);
    void release();
    OC_MAKE_MEMBER_GETTER(swapchain, )

    VkResult queue_present(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore);
    OC_MAKE_MEMBER_GETTER(color_format, )
    OC_MAKE_MEMBER_GETTER(depth_format, )

    uint32_t backbuffer_size() const
    {
        return backbuffers_.size();
    }

    OC_MAKE_MEMBER_GETTER(resolution, )

    SwapChainBuffer get_swapchain_buffer(int index)
    {
        return backbuffers_[index];
    }

    VkResult aquire_next_image(VkSemaphore present_complete_semaphore, uint32_t *image_index);

    struct DepthStencil{
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } depth_stencil;

    DepthStencil get_depth_stencil() const
    {
        return depth_stencil;
    }
private:
    void setup_backbuffers(const VkSwapchainCreateInfoKHR &swapChainCreateInfo);
    void setup_depth_stencil();
    void release_backbuffers();
    void release_depth_stencil();
    VkPresentModeKHR get_preferred_presentmode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool vsync);
    VkFormat get_preferred_colorformat(ColorSpace colorSpace);

private:
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    //VkDevice device_ = VK_NULL_HANDLE;
    VulkanDevice *vulkan_device_;
    //uint32_t imageCount_ = 0;
    std::vector<SwapChainBuffer> backbuffers_;
    VkFormat color_format_{VK_FORMAT_R8G8B8A8_UNORM};
    uint2 resolution_;
    VkFormat depth_format_{VK_FORMAT_D24_UNORM_S8_UINT};

    
};
}// namespace ocarina