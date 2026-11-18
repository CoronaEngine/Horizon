#pragma once

#include "rhi/resources/shader.h"
#include "vk_shader_parameters.h"
#include "vk_shader_program.h"

namespace ocarina {

class VkShaderBase : public Shader<>::Impl {
private:
    const ShaderExecutionMode mode_;
    const string description_;
    // Immediate launches run under the device submission lock. Streams use batch arenas.
    VkBufferAllocation params_buf_{};
    VkDescriptorPool params_uniform_pool_{};
    VkDescriptorSet params_uniform_set_{};

protected:
    VulkanComputeDevice *device_;
    const VkShaderParameterLayout parameter_layout_;
    VkPipelineLayout layout_{};
    VkPipeline pipeline_{};

    VkShaderBase(VulkanComputeDevice *device, const Function &function, ShaderExecutionMode mode,
                 const string &description);
    [[nodiscard]] VkPipelineShaderStageCreateInfo create_stage(const VkShaderModuleCode &module) const noexcept;
    [[nodiscard]] VkShaderStageFlagBits parameter_stage() const noexcept;
    virtual void record_dispatch(VkCommandBuffer cb, uint3 dim, uint8_t *push_constants) const noexcept = 0;

public:
    ~VkShaderBase() noexcept override;
    [[nodiscard]] ShaderExecutionMode execution_mode() const noexcept override { return mode_; }
    [[nodiscard]] VkPipelineStageFlagBits access_stage() const noexcept;
    [[nodiscard]] VkPipelineBindPoint bind_point() const noexcept;
    [[nodiscard]] bool parameters_inline() const noexcept { return parameter_layout_.is_inline(); }
    [[nodiscard]] bool parameters_uniform() const noexcept { return parameter_layout_.uniform_bytes != 0u; }
    [[nodiscard]] const string &description() const noexcept { return description_; }

    void launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept override;
    void dispatch(VkCommandBuffer cb, uint3 dim, span<const std::byte> data, VkDeviceAddress params = 0,
                  bool bind_pipeline = true, VkDescriptorSet uniform_set = VK_NULL_HANDLE,
                  uint32_t uniform_offset = 0u) noexcept;
    void dispatch_barrier(VkCommandBuffer cb) const noexcept;
};

class VkComputeShader final : public VkShaderBase {
private:
    const Function &function_;
    const uint3 workgroup_size_;
    uint3 max_group_count_;
    void record_dispatch(VkCommandBuffer cb, uint3 dim, uint8_t *push_constants) const noexcept override;

public:
    VkComputeShader(VulkanComputeDevice *device, const VkShaderProgram &program, const Function &function,
                    const string &description, uint3 workgroup_size);
};

struct VkShaderFactory {
    [[nodiscard]] static VkShaderBase *create(VulkanComputeDevice *device, const VkShaderProgram &program,
                                              const Function &function, const string &description, uint3 workgroup_size);
};

}// namespace ocarina
