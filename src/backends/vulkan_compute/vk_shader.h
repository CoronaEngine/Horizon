//
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/shader.h"
#include "util.h"
#include "vk_shader_parameters.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;

// ── Base class ────────────────────────────────────────────────────────────────

class VkShaderBase : public Shader<>::Impl {
protected:
    VulkanComputeDevice *device_{};
    const Function      &function_;
    const VkShaderParameterLayout parameter_layout_;

public:
    VkShaderBase(VulkanComputeDevice *device, const Function &f)
        : device_(device), function_(f), parameter_layout_(f) {}
    ~VkShaderBase() override = default;

    [[nodiscard]] bool parameters_inline() const noexcept { return parameter_layout_.is_inline(); }
    [[nodiscard]] bool parameters_uniform() const noexcept { return parameter_layout_.uniform_bytes != 0u; }
    [[nodiscard]] const string &description() const noexcept { return function_.description(); }

    // Called by VkComputeCommandVisitor::visit(ShaderDispatchCommand*)
    virtual void dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd, VkDeviceAddress params = 0,
                          bool bind_pipeline = true, VkDescriptorSet uniform_set = VK_NULL_HANDLE,
                          uint32_t uniform_offset = 0u) noexcept = 0;
};

// ── Compute pipeline ──────────────────────────────────────────────────────────

class VkComputeShader final : public VkShaderBase {
private:
    VkPipeline       pipeline_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    // Immediate launches are synchronous; stream launches own their parameter storage.
    VkBufferAllocation params_buf_{};
    VkDescriptorPool params_uniform_pool_{VK_NULL_HANDLE};
    VkDescriptorSet params_uniform_set_{VK_NULL_HANDLE};
    uint3              workgroup_size_{1u, 1u, 1u};
    uint3              max_group_count_{1u, 1u, 1u};

    void create_pipeline_layout() noexcept;
    void create_pipeline(const vector<uint32_t> &spirv) noexcept;

public:
    VkComputeShader(VulkanComputeDevice *device,
                    const vector<uint32_t> &spirv,
                    const Function &f);
    ~VkComputeShader() noexcept override;

    void launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept override;
    void dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd, VkDeviceAddress params = 0,
                  bool bind_pipeline = true, VkDescriptorSet uniform_set = VK_NULL_HANDLE,
                          uint32_t uniform_offset = 0u) noexcept override;
    void compute_fit_size() noexcept override;
};

// ── Factory ──────────────────────────────────────────────────────────────────

struct VkShaderFactory {
    [[nodiscard]] static VkShaderBase *create(VulkanComputeDevice *device,
                                              const vector<uint32_t> &spirv,
                                              const Function &f);
};

}// namespace ocarina
