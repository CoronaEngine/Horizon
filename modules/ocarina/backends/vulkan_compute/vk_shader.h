//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDAShader / CUDASimpleShader / OptixShader.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/shader.h"
#include "util.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;

// ── shared block-shape heuristic (originally in cuda_shader.cpp) ─────────────

[[nodiscard]] uint3 choose_block_shape(uint3 dispatch_dim, uint max_threads) noexcept;

// ── Base class ────────────────────────────────────────────────────────────────

class VkShaderBase : public Shader<>::Impl {
protected:
    VulkanComputeDevice *device_{};
    const Function      &function_;

    // push-constant layout: 8-byte params device address + 12-byte d_dim
    static constexpr uint32_t c_pc_params_offset = 0u;
    static constexpr uint32_t c_pc_dim_offset    = 8u;
    static constexpr uint32_t c_pc_total_bytes   = 20u;

public:
    VkShaderBase(VulkanComputeDevice *device, const Function &f)
        : device_(device), function_(f) {}
    ~VkShaderBase() override = default;

    // Called by VkComputeCommandVisitor::visit(ShaderDispatchCommand*)
    virtual void dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd) noexcept = 0;
};

// ── Compute pipeline ──────────────────────────────────────────────────────────

class VkComputeShader final : public VkShaderBase {
private:
    VkPipeline       pipeline_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    // params SSBO: one per-stream scratch buffer, lazily allocated
    VkBufferAllocation params_buf_{};
    uint3              workgroup_size_{1u, 1u, 1u};

    void create_pipeline_layout() noexcept;
    void create_pipeline(const vector<uint32_t> &spirv, uint3 wg_size) noexcept;

public:
    VkComputeShader(VulkanComputeDevice *device,
                    const vector<uint32_t> &spirv,
                    const Function &f);
    ~VkComputeShader() noexcept override;

    void launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept override;
    void dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd) noexcept override;
    void compute_fit_size() noexcept override;
};

// ── Raytracing pipeline ───────────────────────────────────────────────────────

class VkRTShader final : public VkShaderBase {
private:
    VkPipeline            rt_pipeline_{VK_NULL_HANDLE};
    VkPipelineLayout      layout_{VK_NULL_HANDLE};
    VkBufferAllocation    sbt_buf_{};
    VkBufferAllocation    params_buf_{};
    VkStridedDeviceAddressRegionKHR raygen_region_{};
    VkStridedDeviceAddressRegionKHR miss_region_{};
    VkStridedDeviceAddressRegionKHR hit_region_{};
    VkStridedDeviceAddressRegionKHR callable_region_{};

    void create_pipeline_layout() noexcept;
    void create_pipeline(const vector<uint32_t> &spirv) noexcept;
    void build_sbt() noexcept;

public:
    VkRTShader(VulkanComputeDevice *device,
               const vector<uint32_t> &spirv,
               const Function &f);
    ~VkRTShader() noexcept override;

    void launch(handle_ty stream, ShaderDispatchCommand *cmd) noexcept override;
    void dispatch(VkCommandBuffer cb, ShaderDispatchCommand *cmd) noexcept override;
};

// ── Factory ──────────────────────────────────────────────────────────────────

struct VkShaderFactory {
    [[nodiscard]] static VkShaderBase *create(VulkanComputeDevice *device,
                                              const vector<uint32_t> &spirv,
                                              const Function &f);
};

}// namespace ocarina
