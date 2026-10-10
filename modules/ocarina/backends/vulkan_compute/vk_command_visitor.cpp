//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_command_visitor.cpp.
//

#include "vk_command_visitor.h"
#include "vk_compute_device.h"
#include "vk_shader.h"
#include "vk_stream.h"
#include "vk_mesh.h"
#include "vk_accel.h"
#include "vk_texture.h"
#include "util.h"

namespace ocarina {

void VkComputeCommandVisitor::record(const std::function<void(VkCommandBuffer)> &fn) noexcept {
    if (stream_) stream_->record(fn);
    else device_->immediate_submit(std::function<void(VkCommandBuffer)>{fn});
}
void VkComputeCommandVisitor::retire(VkBufferAllocation allocation) noexcept {
    if (stream_) stream_->retain(allocation);
    else device_->free_buffer(allocation);
}
void VkComputeCommandVisitor::complete(std::function<void()> fn) noexcept {
    if (stream_) stream_->after_completion(std::move(fn));
    else fn();
}
void VkComputeCommandVisitor::finish(const Command *cmd) noexcept {
    if (stream_ && !cmd->async()) stream_->synchronize();
}


// ── Buffer upload/download helpers ────────────────────────────────────────────

VkBufferAllocation VkComputeCommandVisitor::make_staging(size_t size, const void *src, bool ordered) noexcept {
    auto *dev = device_;
    auto buf = dev->allocate_buffer(size,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "staging");
    auto copy = [dev, buf, src, size] {
        void *mapped = nullptr;
        OC_VK_CHECK(vmaMapMemory(dev->vma_allocator(), buf.alloc, &mapped));
        if (src) memcpy(mapped, src, size);
        vmaUnmapMemory(dev->vma_allocator(), buf.alloc);
    };
    if (stream_ && ordered) stream_->prepare_upload(std::move(copy));
    else copy();
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
    if (!cmd->device_handle() || !cmd->host_ptr() || !cmd->size_in_bytes()) return;
    size_t size = cmd->size_in_bytes();
    auto staging = make_staging(size, cmd->host_ptr<const void *>());
    VkDeviceSize offset = 0;
    auto dst_buf = device_->get_vk_buffer(cmd->device_handle(), &offset);

    VkBufferCopy region{0, offset + cmd->device_offset(), size};
    auto do_copy = [&](VkCommandBuffer cb) {
        vkCmdCopyBuffer(cb, staging.buffer, dst_buf, 1, &region);
        full_barrier(cb);
    };
    record(do_copy);
    retire(staging);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const BufferDownloadCommand *cmd) noexcept {
    if (!cmd->size_in_bytes()) return;
    size_t size = cmd->size_in_bytes();
    auto rb = make_readback(device_, size);
    VkDeviceSize offset = 0;
    auto src_buf = device_->get_vk_buffer(cmd->device_handle(), &offset);
    VkBufferCopy region{offset + cmd->device_offset(), 0, size};
    auto do_copy = [&](VkCommandBuffer cb) {
        full_barrier(cb);
        vkCmdCopyBuffer(cb, src_buf, rb.buffer, 1, &region);
        full_barrier(cb);
    };
    record(do_copy);
    complete([device = device_, rb, host = cmd->host_ptr<void *>(), size] {
        void *mapped = nullptr;
        OC_VK_CHECK(vmaMapMemory(device->vma_allocator(), rb.alloc, &mapped));
        memcpy(host, mapped, size);
        vmaUnmapMemory(device->vma_allocator(), rb.alloc);
    });
    retire(rb);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const BufferByteSetCommand *cmd) noexcept {
    if (!cmd->size_in_bytes()) return;
    VkDeviceSize offset = 0;
    auto buf = device_->get_vk_buffer(cmd->device_handle(), &offset);
    if (cmd->size_in_bytes() % 4 != 0 || offset % 4 != 0) {
        vector<uchar> bytes(cmd->size_in_bytes(), cmd->value());
        auto staging = make_staging(bytes.size(), bytes.data(), false);
        record([&](VkCommandBuffer cb) {
            VkBufferCopy region{0, offset, bytes.size()};
            vkCmdCopyBuffer(cb, staging.buffer, buf, 1, &region);
            full_barrier(cb);
        });
        retire(staging);
        finish(cmd);
        return;
    }
    auto do_fill = [&](VkCommandBuffer cb) {
        uint32_t fill = static_cast<uint32_t>(cmd->value());
        fill = fill | (fill << 8) | (fill << 16) | (fill << 24);
        vkCmdFillBuffer(cb, buf, offset, cmd->size_in_bytes(), fill);
        full_barrier(cb);
    };
    record(do_fill);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const BufferCopyCommand *cmd) noexcept {
    if (!cmd->size()) return;
    VkDeviceSize src_offset = 0, dst_offset = 0;
    auto src = device_->get_vk_buffer(cmd->src(), &src_offset);
    auto dst = device_->get_vk_buffer(cmd->dst(), &dst_offset);
    VkBufferCopy region{src_offset + cmd->src_offset(), dst_offset + cmd->dst_offset(), cmd->size()};
    auto do_copy = [&](VkCommandBuffer cb) {
        vkCmdCopyBuffer(cb, src, dst, 1, &region);
        full_barrier(cb);
    };
    record(do_copy);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const BufferReallocateCommand *cmd) noexcept {
    if (stream_) stream_->synchronize();
    // Delegate to the RHIResource's own realloc path
    auto *res = cmd->rhi_resource();
    if (!res) return;
    device_->destroy_buffer(res->handle());
    *static_cast<handle_ty *>(res->handle_ptr()) = cmd->new_size() == 0 ? 0 :
        device_->create_buffer(cmd->new_size(), "realloc");
}

static void image_barrier(VkCommandBuffer cb, VkImage img,
                           VkImageLayout old_l, VkImageLayout new_l,
                           VkAccessFlags src_a, VkAccessFlags dst_a,
                           VkPipelineStageFlags src_s, VkPipelineStageFlags dst_s) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout        = old_l; b.newLayout = new_l;
    b.image            = img;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    b.srcAccessMask    = src_a; b.dstAccessMask = dst_a;
    vkCmdPipelineBarrier(cb, src_s, dst_s, 0, 0, nullptr, 0, nullptr, 1, &b);
}

static void transfer_barrier(VkCommandBuffer cb) {
    // Images stay in GENERAL. Order both the written destination and the read
    // source against later transfers, including overwrites of a source buffer.
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VkComputeCommandVisitor::upload_image(VkImage img, const void *src, size_t size,
                                            uint32_t w, uint32_t h, uint32_t d) noexcept {
    auto staging = make_staging(size, src);
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, img, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                      0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, d};
        vkCmdCopyBufferToImage(cb, staging.buffer, img, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        transfer_barrier(cb);
    };
    record(do_copy);
    retire(staging);
}

void VkComputeCommandVisitor::visit(const Texture3DUploadCommand *cmd) noexcept {
    auto tex = device_->get_vk_image(cmd->device_handle()).image;
    size_t sz = cmd->size_in_bytes() * cmd->depth();
    upload_image(tex, cmd->host_ptr<const void *>(), sz,
                 (uint32_t)cmd->width(), (uint32_t)cmd->height(), (uint32_t)cmd->depth());
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const Texture3DDownloadCommand *cmd) noexcept {
    auto tex = device_->get_vk_image(cmd->device_handle()).image;
    size_t sz = cmd->size_in_bytes() * cmd->depth();
    auto rb = device_->allocate_buffer(sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "img_rb");
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, tex, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)cmd->width(), (uint32_t)cmd->height(), (uint32_t)cmd->depth()};
        vkCmdCopyImageToBuffer(cb, tex, VK_IMAGE_LAYOUT_GENERAL,
                               rb.buffer, 1, &region);
        full_barrier(cb);
    };
    record(do_copy);
    complete([device = device_, rb, host = cmd->host_ptr<void *>(), sz] {
        void *mapped = nullptr;
        OC_VK_CHECK(vmaMapMemory(device->vma_allocator(), rb.alloc, &mapped));
        memcpy(host, mapped, sz);
        vmaUnmapMemory(device->vma_allocator(), rb.alloc);
    });
    retire(rb);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const Texture3DCopyCommand *cmd) noexcept {
    auto src_tex = device_->get_vk_image(cmd->src()).image;
    auto dst_tex = device_->get_vk_image(cmd->dst()).image;
    uint3 res = cmd->resolution();
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, src_tex, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        image_barrier(cb, dst_tex, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageCopy ic{};
        ic.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->src_level(), 0, 1};
        ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->dst_level(), 0, 1};
        ic.extent = {res.x, res.y, res.z};
        vkCmdCopyImage(cb, src_tex, VK_IMAGE_LAYOUT_GENERAL,
                       dst_tex, VK_IMAGE_LAYOUT_GENERAL, 1, &ic);
        transfer_barrier(cb);
    };
    record(do_copy);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const BufferToTexture3DCommand *cmd) noexcept {
    auto tex = device_->get_vk_image(cmd->dst()).image;
    VkDeviceSize offset = 0;
    auto src_buf = device_->get_vk_buffer(cmd->src(), &offset);
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, tex, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.bufferOffset = offset + cmd->buffer_offset();
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->level(), 0, 1};
        region.imageExtent = {(uint32_t)cmd->width(), (uint32_t)cmd->height(), (uint32_t)cmd->depth()};
        vkCmdCopyBufferToImage(cb, src_buf, tex,
                               VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        transfer_barrier(cb);
    };
    record(do_copy);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const Texture2DUploadCommand *cmd) noexcept {
    auto tex = device_->get_vk_image(cmd->device_handle()).image;
    size_t sz = cmd->size_in_bytes();
    upload_image(tex, cmd->host_ptr<const void *>(), sz,
                 (uint32_t)cmd->width(), (uint32_t)cmd->height(), 1u);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const Texture2DDownloadCommand *cmd) noexcept {
    auto tex = device_->get_vk_image(cmd->device_handle()).image;
    size_t sz = cmd->size_in_bytes();
    auto rb = device_->allocate_buffer(sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "img_rb2d");
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, tex, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)cmd->width(), (uint32_t)cmd->height(), 1u};
        vkCmdCopyImageToBuffer(cb, tex, VK_IMAGE_LAYOUT_GENERAL,
                               rb.buffer, 1, &region);
        full_barrier(cb);
    };
    record(do_copy);
    complete([device = device_, rb, host = cmd->host_ptr<void *>(), sz] {
        void *mapped = nullptr;
        OC_VK_CHECK(vmaMapMemory(device->vma_allocator(), rb.alloc, &mapped));
        memcpy(host, mapped, sz);
        vmaUnmapMemory(device->vma_allocator(), rb.alloc);
    });
    retire(rb);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const Texture2DCopyCommand *cmd) noexcept {
    auto src_tex = device_->get_vk_image(cmd->src()).image;
    auto dst_tex = device_->get_vk_image(cmd->dst()).image;
    uint3 res = cmd->resolution();
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, src_tex, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        image_barrier(cb, dst_tex, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageCopy ic{};
        ic.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->src_level(), 0, 1};
        ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->dst_level(), 0, 1};
        ic.extent = {res.x, res.y, 1u};
        vkCmdCopyImage(cb, src_tex, VK_IMAGE_LAYOUT_GENERAL,
                       dst_tex, VK_IMAGE_LAYOUT_GENERAL, 1, &ic);
        transfer_barrier(cb);
    };
    record(do_copy);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const BufferToTexture2DCommand *cmd) noexcept {
    auto tex = device_->get_vk_image(cmd->dst()).image;
    VkDeviceSize offset = 0;
    auto src_buf = device_->get_vk_buffer(cmd->src(), &offset);
    auto do_copy = [&](VkCommandBuffer cb) {
        image_barrier(cb, tex, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.bufferOffset = offset + cmd->buffer_offset();
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, cmd->level(), 0, 1};
        region.imageExtent = {(uint32_t)cmd->width(), (uint32_t)cmd->height(), 1u};
        vkCmdCopyBufferToImage(cb, src_buf, tex,
                               VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        transfer_barrier(cb);
    };
    record(do_copy);
    finish(cmd);
}

void VkComputeCommandVisitor::visit(const BLASBuildCommand *cmd) noexcept {
    if (stream_) stream_->synchronize();
    auto *mesh = cmd->mesh<VkMesh>();
    mesh->build_bvh(cmd);
}

void VkComputeCommandVisitor::visit(const TLASBuildCommand *cmd) noexcept {
    if (stream_) stream_->synchronize();
    auto *accel = cmd->accel<VkAccel>();
    accel->build_tlas(this);
}

void VkComputeCommandVisitor::visit(const TLASUpdateCommand *cmd) noexcept {
    if (stream_) stream_->synchronize();
    auto *accel = cmd->accel<VkAccel>();
    accel->update_tlas(this);
}

void VkComputeCommandVisitor::visit(const SynchronizeCommand *) noexcept {
    if (stream_) stream_->synchronize();
}

void VkComputeCommandVisitor::visit(const ShaderDispatchCommand *cmd) noexcept {
    auto *shader = cmd->entry<VkShaderBase *>();
    if (!shader) return;
    auto data = const_cast<ShaderDispatchCommand *>(cmd)->argument_data();
    auto address = stream_ && !shader->parameters_inline() ?
        stream_->stage_parameters(data.data(), data.size_bytes()) : 0;
    record([&](VkCommandBuffer cb) {
        const bool bind_pipeline = !stream_ || stream_->update_shader_binding(shader);
        shader->dispatch(cb, const_cast<ShaderDispatchCommand *>(cmd), address, bind_pipeline);
        // All DSL kernels (including ray queries) execute in the compute stage.
        // Preserve RAW/WAR/WAW order with later compute and transfer commands
        // without serializing unrelated pipeline stages after every dispatch.
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);
    });
}

void VkComputeCommandVisitor::visit(const HostFunctionCommand *cmd) noexcept {
    if (stream_) stream_->host_function(cmd->function(), cmd->async());
    else cmd->function()();
}

}// namespace ocarina
