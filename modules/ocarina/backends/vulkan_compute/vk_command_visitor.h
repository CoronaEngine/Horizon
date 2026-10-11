//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDACommandVisitor: one visit() per Command type.
//

#pragma once

#include "rhi/command.h"
#include "util.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;
class VkComputeStream;

class VkComputeCommandVisitor final : public CommandVisitor {
private:
    VulkanComputeDevice *device_{};
    VkComputeStream *stream_{};
    void record(const std::function<void(VkCommandBuffer)> &fn) noexcept;
    void retire(VkBufferAllocation allocation) noexcept;
    void complete(std::function<void()> fn) noexcept;
    void finish(const Command *cmd) noexcept;
    VkBufferAllocation make_staging(size_t size, const void *source, bool ordered = true) noexcept;
    void upload_image(VkImage image, const void *source, size_t size,
                      uint32_t width, uint32_t height, uint32_t depth) noexcept;

public:
    explicit VkComputeCommandVisitor(VulkanComputeDevice *device, VkComputeStream *stream = nullptr) noexcept
        : device_(device), stream_(stream) {}

    void visit(const BufferUploadCommand *cmd)       noexcept override;
    void visit(const BufferDownloadCommand *cmd)     noexcept override;
    void visit(const BufferByteSetCommand *cmd)      noexcept override;
    void visit(const BufferCopyCommand *cmd)         noexcept override;
    void visit(const BufferReallocateCommand *cmd)   noexcept override;

    void visit(const Texture3DUploadCommand *cmd)    noexcept override;
    void visit(const Texture3DDownloadCommand *cmd)  noexcept override;
    void visit(const Texture3DCopyCommand *cmd)      noexcept override;
    void visit(const BufferToTexture3DCommand *cmd)  noexcept override;

    void visit(const Texture2DUploadCommand *cmd)    noexcept override;
    void visit(const Texture2DDownloadCommand *cmd)  noexcept override;
    void visit(const Texture2DCopyCommand *cmd)      noexcept override;
    void visit(const BufferToTexture2DCommand *cmd)  noexcept override;

    void visit(const BLASBuildCommand *cmd)          noexcept override;
    void visit(const TLASBuildCommand *cmd)          noexcept override;
    void visit(const TLASUpdateCommand *cmd)         noexcept override;
    void visit(const SynchronizeCommand *cmd)        noexcept override;
    void visit(const ShaderDispatchCommand *cmd)     noexcept override;
    void visit(const HostFunctionCommand *cmd)       noexcept override;
};

}// namespace ocarina
