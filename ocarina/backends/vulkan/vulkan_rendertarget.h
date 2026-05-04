//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "rhi/rendertarget.h"
#include <vulkan/vulkan.h>

namespace ocarina {

    class VulkanDevice;

class VulkanRenderTarget : public RenderTarget {
private:
    VkImageView image_view_;
    VkImage image_;
    VkDevice device_;

public:
    VulkanRenderTarget(VulkanDevice *device, const RenderTargetCreation& creation);
    ~VulkanRenderTarget() override;
    
    OC_MAKE_MEMBER_GETTER(image_view, );
    OC_MAKE_MEMBER_GETTER(image, );


};
}// namespace ocarina