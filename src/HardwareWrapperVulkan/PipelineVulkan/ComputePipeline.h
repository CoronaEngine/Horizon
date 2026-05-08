#pragma once

#include <ktm/ktm.h>

#include "Horizon.h"
#include "Compiler/ShaderCodeCompiler.h"
#include "HardwareWrapperVulkan/HardwareVulkan/DeviceManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"

struct ComputePipelineVulkan : public CommandRecordVulkan
{
  public:
    ComputePipelineVulkan();
    ~ComputePipelineVulkan() override;

    ComputePipelineVulkan(std::string shaderCode, EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL, const std::source_location &sourceLocation = std::source_location::current());

    // 新构造函数：接受已编译的 ShaderCodeCompiler，避免重复编译
    ComputePipelineVulkan(const EmbeddedShader::ShaderCodeCompiler &compiler, const std::source_location &sourceLocation = std::source_location::current());

    // 从预编译 SPIR-V 二进制构造（跳过 GLSL→SPIR-V 编译）
    ComputePipelineVulkan(const std::vector<uint32_t> &spirV, const std::source_location &sourceLocation = std::source_location::current());

    // std::variant<HardwarePushConstant> operator[](const std::string& resourceName);

    // Direct-access methods: bypass string lookup, use pre-resolved metadata
    void setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType);

    ComputePipelineVulkan *operator()(uint16_t x, uint16_t y, uint16_t z);

    ExecutorType getExecutorType() override
    {
        return CommandRecordVulkan::ExecutorType::Compute;
    }

    void commitCommand(HardwareExecutorVulkan &hardwareExecutor) override;
    RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor) override;

  private:
    void createComputePipeline();

    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};

    HardwarePushConstant pushConstant;
    EmbeddedShader::ShaderCodeModule shaderCode;

    // Per-pipeline UBO support
    HardwarePushConstant tempUBO;
    HardwareBuffer uboBuffer;
    uint32_t uboSize{0};
    VkDescriptorPool uboDescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout uboDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSet uboDescriptorSet{VK_NULL_HANDLE};
    bool uboDescriptorDirty{true};
    void createPerPipelineUBODescriptor();
    void updateUBODescriptor();

    ktm::uvec3 groupCount = {0, 0, 0};
};