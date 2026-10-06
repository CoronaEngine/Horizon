//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_command_visitor.cpp.
//

#include "vk_command_visitor.h"
#include "vk_compute_device.h"
#include "vk_shader.h"
#include "vk_mesh.h"
#include "vk_accel.h"
#include "vk_texture.h"
#include "util.h"

namespace ocarina {

// ── Buffer upload/download helpers ────────────────────────────────────────────

static VkBufferAllocation make_staging(VulkanComputeDevice *dev, size_t size,
                                       const void *src) {
    auto buf = dev->allocate_buffer(size,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "staging");
    void *mapped = nullptr;
    vmaMapMemory(dev->vma_allocator(), buf.alloc, &mapped);
    if (src) memcpy(mapped, src, size);
    vmaUnmapMemory(dev->vma_allocator(), buf.alloc);
    return buf;
}

static VkBufferAllocation make_readback(VulkanComputeDevice *dev, size_t size) {
    return dev->allocate_buffer(size,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "readback");
}

static void full_barrier(VkCommandBuffer cb) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &mb, 0, nullptr, 0, nullptr);
}

// ── Buffer commands ───────────────────────────────────────────────────────────

void VkComputeCommandVisitor::visit(const BufferUploadCommand *cmd) noexcept {
    if (!cmd->device_handle() && !cmd->host_ptr()) return;
    size_t size = cmd->size_in_bytes();
    auto staging = make_staging(device_, size, cmd->host_ptr<const void *>());
    auto dst_buf = device_->get_vk_buffer(cmd->device_handle());

    VkBufferCopy region{0, cmd->device_offset(), size};
    auto do_copy = [&](VkCommandBuffer cb) {
        vkCmdCopyBuffer(cb, staging.buffer, dst_buf, 1, &region);
        full_barrier(cb);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) {
        do_copy(cmd_buf_);
    } else {
        device_->immediate_submit(do_copy);
    }
    device_->free_buffer(staging);
}

void VkComputeCommandVisitor::visit(const BufferDownloadCommand *cmd) noexcept {
    size_t size = cmd->size_in_bytes();
    auto rb = make_readback(device_, size);
    auto src_buf = device_->get_vk_buffer(cmd->device_handle());
    VkBufferCopy region{cmd->device_offset(), 0, size};
    auto do_copy = [&](VkCommandBuffer cb) {
        full_barrier(cb);
        vkCmdCopyBuffer(cb, src_buf, rb.buffer, 1, &region);
        full_barrier(cb);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else device_->immediate_submit(do_copy);
    // Map and copy to host
    void *mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), rb.alloc, &mapped);
    memcpy(cmd->host_ptr<void *>(), mapped, size);
    vmaUnmapMemory(device_->vma_allocator(), rb.alloc);
    device_->free_buffer(rb);
}

void VkComputeCommandVisitor::visit(const BufferByteSetCommand *cmd) noexcept {
    auto buf = device_->get_vk_buffer(cmd->device_handle());
    auto do_fill = [&](VkCommandBuffer cb) {
        uint32_t fill = static_cast<uint32_t>(cmd->value());
        fill = fill | (fill << 8) | (fill << 16) | (fill << 24);
        vkCmdFillBuffer(cb, buf, 0, cmd->size_in_bytes(), fill);
        full_barrier(cb);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_fill(cmd_buf_);
    else device_->immediate_submit(do_fill);
}

void VkComputeCommandVisitor::visit(const BufferCopyCommand *cmd) noexcept {
    auto src = device_->get_vk_buffer(cmd->src());
    auto dst = device_->get_vk_buffer(cmd->dst());
    VkBufferCopy region{cmd->src_offset(), cmd->dst_offset(), cmd->size()};
    auto do_copy = [&](VkCommandBuffer cb) {
        vkCmdCopyBuffer(cb, src, dst, 1, &region);
        full_barrier(cb);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else device_->immediate_submit(do_copy);
}

void VkComputeCommandVisitor::visit(const BufferReallocateCommand *cmd) noexcept {
    // Delegate to the RHIResource's own realloc path
    auto *res = cmd->rhi_resource();
    if (!res) return;
    handle_ty new_handle = device_->create_buffer(cmd->new_size(), "realloc");
    device_->destroy_buffer(res->handle());
    res->set_handle(new_handle);
}
// PLACEHOLDER_CMD_1

static void image_barrier(VkCommandBuffer cb, VkImage img,
                           VkImageLayout old_l, VkImageLayout new_l,
                           VkAccessFlags src_a, VkAccessFlags dst_a,
                           VkPipelineStageFlags src_s, VkPipelineStageFlags dst_s) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout        = old_l; b.newLayout = new_l;
    b.image            = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    b.srcAccessMask    = src_a; b.dstAccessMask = dst_a;
    vkCmdPipelineBarrier(cb, src_s, dst_s, 0, 0, nullptr, 0, nullptr, 1, &b);
}

static void upload_image(VulkanComputeDevice *dev, VkCommandBuffer cmd_buf_,
                          VkImage img, const void *src, size_t size,
                          uint32_t w, uint32_t h, uint32_t d) {
    auto staging = dev->allocate_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "img_stage");
    void *mapped = nullptr;
    vmaMapMemory(dev->vma_allocator(), staging.alloc, &mapped);
    memcpy(mapped, src, size);
    vmaUnmapMemory(dev->vma_allocator(), staging.alloc);
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, d};
        vkCmdCopyBufferToImage(cb, staging.buffer, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else dev->immediate_submit(do_copy);
    dev->free_buffer(staging);
}

void VkComputeCommandVisitor::visit(const Texture3DUploadCommand *cmd) noexcept {
    auto *tex = reinterpret_cast<VkTexture *>(cmd->device_handle());
    size_t sz = cmd->size_in_bytes() * cmd->depth();
    upload_image(device_, cmd_buf_, tex->vk_image(), cmd->host_ptr<const void *>(), sz,
                 (uint32_t)cmd->width(), (uint32_t)cmd->height(), (uint32_t)cmd->depth());
}
// PLACEHOLDER_CMD_2

void VkComputeCommandVisitor::visit(const Texture3DDownloadCommand *cmd) noexcept {
    auto *tex = reinterpret_cast<VkTexture *>(cmd->device_handle());
    size_t sz = cmd->size_in_bytes() * cmd->depth();
    auto rb = device_->allocate_buffer(sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "img_rb");
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, tex->vk_image(), VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)cmd->width(), (uint32_t)cmd->height(), (uint32_t)cmd->depth()};
        vkCmdCopyImageToBuffer(cb, tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               rb.buffer, 1, &region);
        full_barrier(cb);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else device_->immediate_submit(do_copy);
    void *mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), rb.alloc, &mapped);
    memcpy(cmd->host_ptr<void *>(), mapped, sz);
    vmaUnmapMemory(device_->vma_allocator(), rb.alloc);
    device_->free_buffer(rb);
}

void VkComputeCommandVisitor::visit(const Texture3DCopyCommand *cmd) noexcept {
    auto *src_tex = reinterpret_cast<VkTexture *>(cmd->src());
    auto *dst_tex = reinterpret_cast<VkTexture *>(cmd->dst());
    uint3 res = cmd->resolution();
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, src_tex->vk_image(), VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        image_barrier(cb, dst_tex->vk_image(), VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageCopy ic{};
        ic.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->src_level(), 0, 1};
        ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->dst_level(), 0, 1};
        ic.extent = {res.x, res.y, res.z};
        vkCmdCopyImage(cb, src_tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst_tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
        image_barrier(cb, dst_tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else device_->immediate_submit(do_copy);
}
// PLACEHOLDER_CMD_3

void VkComputeCommandVisitor::visit(const BufferToTexture3DCommand *cmd) noexcept {
    auto *tex = reinterpret_cast<VkTexture *>(cmd->dst());
    auto src_buf = device_->get_vk_buffer(cmd->src());
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, tex->vk_image(), VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.bufferOffset = cmd->buffer_offset();
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->level(), 0, 1};
        region.imageExtent = {(uint32_t)cmd->width(), (uint32_t)cmd->height(), (uint32_t)cmd->depth()};
        vkCmdCopyBufferToImage(cb, src_buf, tex->vk_image(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(cb, tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else device_->immediate_submit(do_copy);
}

void VkComputeCommandVisitor::visit(const Texture2DUploadCommand *cmd) noexcept {
    auto *tex = reinterpret_cast<VkTexture *>(cmd->device_handle());
    size_t sz = cmd->size_in_bytes();
    upload_image(device_, cmd_buf_, tex->vk_image(), cmd->host_ptr<const void *>(), sz,
                 (uint32_t)cmd->width(), (uint32_t)cmd->height(), 1u);
}

void VkComputeCommandVisitor::visit(const Texture2DDownloadCommand *cmd) noexcept {
    auto *tex = reinterpret_cast<VkTexture *>(cmd->device_handle());
    size_t sz = cmd->size_in_bytes();
    auto rb = device_->allocate_buffer(sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "img_rb2d");
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, tex->vk_image(), VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)cmd->width(), (uint32_t)cmd->height(), 1u};
        vkCmdCopyImageToBuffer(cb, tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               rb.buffer, 1, &region);
        full_barrier(cb);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else device_->immediate_submit(do_copy);
    void *mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), rb.alloc, &mapped);
    memcpy(cmd->host_ptr<void *>(), mapped, sz);
    vmaUnmapMemory(device_->vma_allocator(), rb.alloc);
    device_->free_buffer(rb);
}
// PLACEHOLDER_CMD_4

void VkComputeCommandVisitor::visit(const Texture2DCopyCommand *cmd) noexcept {
    auto *src_tex = reinterpret_cast<VkTexture *>(cmd->src());
    auto *dst_tex = reinterpret_cast<VkTexture *>(cmd->dst());
    uint3 res = cmd->resolution();
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, src_tex->vk_image(), VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        image_barrier(cb, dst_tex->vk_image(), VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageCopy ic{};
        ic.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->src_level(), 0, 1};
        ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->dst_level(), 0, 1};
        ic.extent = {res.x, res.y, 1u};
        vkCmdCopyImage(cb, src_tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst_tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
        image_barrier(cb, dst_tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else device_->immediate_submit(do_copy);
}

void VkComputeCommandVisitor::visit(const BufferToTexture2DCommand *cmd) noexcept {
    auto *tex = reinterpret_cast<VkTexture *>(cmd->dst());
    auto src_buf = device_->get_vk_buffer(cmd->src());
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, tex->vk_image(), VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.bufferOffset = cmd->buffer_offset();
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->level(), 0, 1};
        region.imageExtent = {(uint32_t)cmd->width(), (uint32_t)cmd->height(), 1u};
        vkCmdCopyBufferToImage(cb, src_buf, tex->vk_image(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(cb, tex->vk_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    };
    if (cmd_buf_ != VK_NULL_HANDLE) do_copy(cmd_buf_);
    else device_->immediate_submit(do_copy);
}
// PLACEHOLDER_CMD_5

void VkComputeCommandVisitor::visit(const BLASBuildCommand *cmd) noexcept {
    auto *mesh = cmd->mesh<VkMesh>();
    mesh->build_bvh(cmd);
}

void VkComputeCommandVisitor::visit(const TLASBuildCommand *cmd) noexcept {
    auto *accel = cmd->accel<VkAccel>();
    accel->build_tlas(this);
}

void VkComputeCommandVisitor::visit(const TLASUpdateCommand *cmd) noexcept {
    auto *accel = cmd->accel<VkAccel>();
    accel->update_tlas(this);
}

void VkComputeCommandVisitor::visit(const SynchronizeCommand *) noexcept {
    if (cmd_buf_ != VK_NULL_HANDLE) {
        full_barrier(cmd_buf_);
    } else {
        device_->immediate_submit([](VkCommandBuffer cb) {
            VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(cb,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0, 1, &mb, 0, nullptr, 0, nullptr);
        });
    }
}

void VkComputeCommandVisitor::visit(const ShaderDispatchCommand *cmd) noexcept {
    auto *shader = cmd->entry<VkShaderBase *>();
    if (!shader) return;
    shader->dispatch(cmd_buf_, const_cast<ShaderDispatchCommand *>(cmd));
}

void VkComputeCommandVisitor::visit(const HostFunctionCommand *cmd) noexcept {
    // Flush any pending GPU work so host function sees up-to-date results
    if (cmd_buf_ != VK_NULL_HANDLE) {
        OC_VK_CHECK(vkEndCommandBuffer(cmd_buf_));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1; si.pCommandBuffers = &cmd_buf_;
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        OC_VK_CHECK(vkCreateFence(device_->logical_device(), &fi, nullptr, &fence));
        OC_VK_CHECK(vkQueueSubmit(device_->compute_queue(), 1, &si, fence));
        OC_VK_CHECK(vkWaitForFences(device_->logical_device(), 1, &fence, VK_TRUE, UINT64_MAX));
        vkDestroyFence(device_->logical_device(), fence, nullptr);
        // Re-begin for subsequent commands
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        OC_VK_CHECK(vkBeginCommandBuffer(cmd_buf_, &bi));
    }
    cmd->function()();
}

}// namespace ocarina
