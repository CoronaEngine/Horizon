//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDAShader / CUDASimpleShader / OptixShader.
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

    // Called by VkComputeCommandVisitor::visit(ShaderDispatchCommand*)
    virtual void dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd, VkDeviceAddress params = 0,
                          bool bind_pipeline = true) noexcept = 0;
};

// ── Compute pipeline ──────────────────────────────────────────────────────────

class VkComputeShader final : public VkShaderBase {
private:
    VkPipeline       pipeline_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    // Immediate launches are synchronous; stream launches own their parameter storage.
    VkBufferAllocation params_buf_{};
    uint3              workgroup_size_{1u, 1u, 1u};

    void create_pipeline_layout() noexcept;
    void create_pipeline(const vector<uint32_t> &spirv) noexcept;

public:
    VkComputeShader(VulkanComputeDevice *device,
                    const vector<uint32_t> &spirv,
                    const Function &f);
    ~VkComputeShader() noexcept override;

    void launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept override;
    void dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd, VkDeviceAddress params = 0,
                  bool bind_pipeline = true) noexcept override;
    void compute_fit_size() noexcept override;
};

// ── Factory ──────────────────────────────────────────────────────────────────

struct VkShaderFactory {
    [[nodiscard]] static VkShaderBase *create(VulkanComputeDevice *device,
                                              const vector<uint32_t> &spirv,
                                              const Function &f);
};

}// namespace ocarina
