//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDAStream: wraps a VkCommandBuffer + timeline semaphore.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/stream.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;

class VkComputeStream : public Stream::Impl {
private:
    VulkanComputeDevice *device_{};
    VkCommandPool   cmd_pool_{VK_NULL_HANDLE};
    VkCommandBuffer cmd_buf_{VK_NULL_HANDLE};
    VkFence         fence_{VK_NULL_HANDLE};
    bool            recording_{false};

    void begin_recording() noexcept;
    void end_and_submit() noexcept;

public:
    explicit VkComputeStream(VulkanComputeDevice *device) noexcept;
    ~VkComputeStream() noexcept override;

    [[nodiscard]] VkCommandBuffer cmd_buf() const noexcept { return cmd_buf_; }

    void add_command(Command *cmd) noexcept override;
    void barrier() noexcept override;
    void commit(const Commit &cmt) noexcept override;
};

}// namespace ocarina
