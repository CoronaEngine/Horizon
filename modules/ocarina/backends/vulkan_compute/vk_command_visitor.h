//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDACommandVisitor: one visit() per Command type.
//

#pragma once

#include "rhi/command.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;

class VkComputeCommandVisitor final : public CommandVisitor {
private:
    VulkanComputeDevice *device_{};
    VkCommandBuffer      cmd_buf_{VK_NULL_HANDLE};

public:
    explicit VkComputeCommandVisitor(VulkanComputeDevice *device,
                                     VkCommandBuffer cmd_buf = VK_NULL_HANDLE) noexcept
        : device_(device), cmd_buf_(cmd_buf) {}

    void set_cmd_buf(VkCommandBuffer cb) noexcept { cmd_buf_ = cb; }

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
