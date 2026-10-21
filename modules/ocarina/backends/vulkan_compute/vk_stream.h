//
// Compute command batches with asynchronous fence retirement.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/stream.h"
#include "rhi/native_interop.h"
#include "util.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;
class VkShaderBase;
class ShaderDispatchCommand;

class VkComputeStream : public Stream::Impl {
private:
    VulkanComputeDevice *device_{};
    struct State;
    std::unique_ptr<State> state_;

public:
    explicit VkComputeStream(VulkanComputeDevice *device) noexcept;
    ~VkComputeStream() noexcept override;
    void record(const std::function<void(VkCommandBuffer)> &fn) noexcept;
    void retain(VkBufferAllocation allocation) noexcept;
    void after_completion(std::function<void()> fn) noexcept;
    void host_function(std::function<void()> fn, bool async) noexcept;
    void native_callback(NativeCallbackFunction fn) noexcept;
    void prepare_upload(std::function<void()> copy) noexcept;
    [[nodiscard]] VkDeviceAddress stage_parameters(const void *data, size_t size) noexcept;
    [[nodiscard]] bool update_shader_binding(const VkShaderBase *shader) noexcept;
    void dispatch_shader(VkShaderBase *shader, ShaderDispatchCommand *command) noexcept;
    void synchronize() noexcept;
    void wait_idle() noexcept;
    [[nodiscard]] static bool is_completion_thread(const VulkanComputeDevice *device) noexcept;
    void add_command(Command *cmd) noexcept override;
    void barrier() noexcept override;
    void commit(const Commit &cmt) noexcept override;
};

}// namespace ocarina
