//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDAStream.
//

#include "vk_stream.h"
#include "vk_compute_device.h"
#include "vk_command_visitor.h"
#include "util.h"

namespace ocarina {

VkComputeStream::VkComputeStream(VulkanComputeDevice *device) noexcept
    : device_(device) {
    VkCommandPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_ci.queueFamilyIndex = device_->queue_family();
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    OC_VK_CHECK(vkCreateCommandPool(device_->logical_device(), &pool_ci, nullptr, &cmd_pool_));

    VkCommandBufferAllocateInfo alloc_ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_ai.commandPool        = cmd_pool_;
    alloc_ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_ai.commandBufferCount = 1;
    OC_VK_CHECK(vkAllocateCommandBuffers(device_->logical_device(), &alloc_ai, &cmd_buf_));

    VkFenceCreateInfo fence_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    OC_VK_CHECK(vkCreateFence(device_->logical_device(), &fence_ci, nullptr, &fence_));
}

VkComputeStream::~VkComputeStream() noexcept {
    VkDevice dev = device_->logical_device();
    if (fence_ != VK_NULL_HANDLE) {
        vkWaitForFences(dev, 1, &fence_, VK_TRUE, UINT64_MAX);
        vkDestroyFence(dev, fence_, nullptr);
    }
    if (cmd_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(dev, cmd_pool_, nullptr);
    }
}

void VkComputeStream::begin_recording() noexcept {
    if (recording_) return;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    OC_VK_CHECK(vkBeginCommandBuffer(cmd_buf_, &bi));
    recording_ = true;
}

void VkComputeStream::end_and_submit() noexcept {
    if (!recording_) return;
    OC_VK_CHECK(vkEndCommandBuffer(cmd_buf_));

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd_buf_;
    OC_VK_CHECK(vkQueueSubmit(device_->compute_queue(), 1, &si, fence_));
    OC_VK_CHECK(vkWaitForFences(device_->logical_device(), 1, &fence_, VK_TRUE, UINT64_MAX));
    OC_VK_CHECK(vkResetFences(device_->logical_device(), 1, &fence_));
    OC_VK_CHECK(vkResetCommandBuffer(cmd_buf_, 0));
    recording_ = false;
}

void VkComputeStream::add_command(Command *cmd) noexcept {
    begin_recording();
    VkComputeCommandVisitor visitor(device_, cmd_buf_);
    cmd->accept(visitor);
}

void VkComputeStream::barrier() noexcept {
    begin_recording();
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT  | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd_buf_,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
}

void VkComputeStream::commit(const Commit &cmt) noexcept {
    end_and_submit();
    if (cmt.callback) {
        cmt.callback(nullptr);
    }
}

}// namespace ocarina
