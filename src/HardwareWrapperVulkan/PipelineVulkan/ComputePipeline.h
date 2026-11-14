#pragma once

#include <memory>
#include <ktm/ktm.h>

#include "Horizon_old.h"
#include "Compiler/ShaderCodeCompiler.h"
#include "HardwareWrapperVulkan/HardwareVulkan/DeviceManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "PipelineSnapshot.h"

struct ComputePipelineVulkan : std::enable_shared_from_this<ComputePipelineVulkan>
{
  public:
    ComputePipelineVulkan();
    ~ComputePipelineVulkan();

    ComputePipelineVulkan(std::string shaderCode, EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL, const std::source_location &sourceLocation = std::source_location::current());

    // 新构造函数：接受已编译的 ShaderCodeCompiler，避免重复编译
    ComputePipelineVulkan(EmbeddedShader::ShaderCodeCompiler &compiler, const std::source_location &sourceLocation = std::source_location::current());

    // 从 SPIR-V + Slang 反射构造
    ComputePipelineVulkan(const EmbeddedShader::ShaderCodeModule &spirV, const std::source_location &sourceLocation = std::source_location::current());

    // Direct-access methods: bypass string lookup, use pre-resolved metadata
    void setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType);

    ComputePipelineVulkan *operator()(uint16_t x, uint16_t y, uint16_t z);

    struct Dispatch : CommandRecordVulkan
    {
        std::shared_ptr<ComputePipelineVulkan> pipeline;
        ktm::uvec3 groupCount{};
        std::vector<uint8_t> pushConstant;
        std::vector<uint8_t> uniformBytes;
        std::shared_ptr<BindingSnapshot> bindings;
        std::shared_ptr<UniformSnapshot> uniform;
        ExecutorType getExecutorType() override { return ExecutorType::Compute; }
        void resolveResourceUses(std::span<const ResourceUse> declarations, ResourceAccessList& result) override;
        void commitCommand(HardwareExecutorVulkan& executor) override;
    };
    std::shared_ptr<Dispatch> capture();

  private:
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
    void commitCommand(HardwareExecutorVulkan& executor, Dispatch& dispatch);

    ktm::uvec3 groupCount = {0, 0, 0};
};
