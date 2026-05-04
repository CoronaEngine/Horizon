//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "rhi/renderpass.h"
#include <vulkan/vulkan.h>

namespace ocarina {

    class VulkanDevice;

class VulkanRenderPass : public RHIRenderPass {
public:
    VulkanRenderPass(VulkanDevice *device, const RenderPassCreation &render_pass_creation);
    ~VulkanRenderPass() override;

    void begin_render_pass() override;

    void end_render_pass() override;
    OC_MAKE_MEMBER_GETTER(render_pass, );

    void draw_items() override;

private:
    void setup_render_pass();
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VulkanDevice *device_ = nullptr;
    VkRenderPassBeginInfo render_pass_begin_info_ = {};
    VkClearValue clear_values[kMaxRenderTargets + 1];
    VkFramebuffer vulkan_frame_buffer_ = VK_NULL_HANDLE;
};
}// namespace ocarina