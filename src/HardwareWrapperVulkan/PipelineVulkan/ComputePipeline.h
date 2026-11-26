#pragma once

#include <memory>
#include <ktm/ktm.h>

#include "Horizon.h"
#include "Compiler/ShaderCodeCompiler.h"
#include "HardwareWrapperVulkan/HardwareVulkan/DeviceManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "PipelineSnapshot.h"

namespace horizon::detail {
struct ComputePipelineVulkan
{
    ~ComputePipelineVulkan();

    explicit ComputePipelineVulkan(const EmbeddedShader::ShaderCodeModule &module);

    // Direct-access methods: bypass string lookup, use pre-resolved metadata
    void setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType);

private:
    friend class ::HardwareExecutor::Command;
    void createComputePipeline();

    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};

    std::unique_ptr<uint8_t[]> pushConstant;
    EmbeddedShader::ShaderCodeModule shaderCode;

    // Per-pipeline UBO support
    std::unique_ptr<uint8_t[]> tempUBO;
    uint32_t uboSize{0};
    VkDescriptorSetLayout uboDescriptorSetLayout{VK_NULL_HANDLE};
    PipelineBindings bindings;
    UniformSnapshotPool uniformPool;
};
} // namespace horizon::detail

using horizon::detail::ComputePipelineVulkan;

struct HardwareExecutor::Command::Impl::Dispatch final : HardwareExecutor::Command::Impl, CommandRecordVulkan
{
    Dispatch(std::shared_ptr<ComputePipelineVulkan> pipeline, ktm::uvec3 groups,
             const std::vector<ResourceUse>& resources);
    Entry queueEntry(std::shared_ptr<Impl> owner) override { return RecordedCommand(*this, std::move(owner)); }
    ExecutorType getExecutorType() override { return ExecutorType::Compute; }
    void resolveResourceUses(std::span<const ResourceUse> declarations, ResourceAccessList& result) override;
    void commitCommand(HardwareExecutorVulkan& executor) override;

    std::shared_ptr<ComputePipelineVulkan> backend;
    ktm::uvec3 groupCount;
    std::vector<uint8_t> pushConstant;
    std::vector<uint8_t> uniformBytes;
    std::shared_ptr<BindingSnapshot> bindings;
    std::vector<ResourceUse> resourceUses;
    std::shared_ptr<UniformSnapshot> uniform;
};
