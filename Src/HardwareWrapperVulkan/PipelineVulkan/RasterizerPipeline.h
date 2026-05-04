#pragma once

#include <vector>

#include "Horizon.h"
#include "Compiler/ShaderCodeCompiler.h"
#include "HardwareWrapperVulkan/HardwareVulkan/DeviceManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"

struct RasterizerPipelineVulkan : public CommandRecordVulkan
{
  public:
    RasterizerPipelineVulkan();
    ~RasterizerPipelineVulkan() override;

    RasterizerPipelineVulkan(std::string vertexShaderCode,
                             std::string fragmentShaderCode,
                             uint32_t multiviewCount = 1,
                             EmbeddedShader::ShaderLanguage vertexShaderLanguage = EmbeddedShader::ShaderLanguage::GLSL,
                             EmbeddedShader::ShaderLanguage fragmentShaderLanguage = EmbeddedShader::ShaderLanguage::GLSL,
                             const std::source_location &sourceLocation = std::source_location::current());

    // 从已编译的 ShaderCodeCompiler 构造
    RasterizerPipelineVulkan(const EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                             const EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                             uint32_t multiviewCount = 1,
                             const std::source_location &sourceLocation = std::source_location::current());

    // 从预编译 SPIR-V 二进制构造（跳过 GLSL→SPIR-V 编译）
    RasterizerPipelineVulkan(const std::vector<uint32_t> &vertexSpirV,
                             const std::vector<uint32_t> &fragmentSpirV,
                             uint32_t multiviewCount = 1,
                             const std::source_location &sourceLocation = std::source_location::current());

    void setDepthImage(HardwareImage &depthImage)
    {
        this->depthImage = depthImage;
    }

    void setDepthEnabled(bool enabled)
    {
        if (depthEnabled != enabled)
        {
            depthEnabled = enabled;
            graphicsPipelineDirty = true;
        }
    }

    //void setDepthWriteEnabled(bool enabled)
    //{
    //    if (depthWriteEnabled != enabled)
    //    {
    //        depthWriteEnabled = enabled;
    //        graphicsPipelineDirty = true;
    //    }
    //}

    [[nodiscard]] HardwareImage &getDepthImage()
    {
        return depthImage;
    }

    // std::variant<HardwarePushConstant, HardwareBuffer*, HardwareImage*> operator[](const std::string& resourceName);

    // Direct-access methods: bypass string lookup, use pre-resolved metadata
    void setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location = 0);

    RasterizerPipelineVulkan *operator()(uint16_t width, uint16_t height);

    CommandRecordVulkan *record(const HardwareBuffer &indexBuffer, const HardwareBuffer &vertexBuffer);
    CommandRecordVulkan *record(const HardwareBuffer &indexBuffer,
                                const HardwareBuffer &vertexBuffer,
                                const DrawIndexedParams &params);

    ExecutorType getExecutorType() override
    {
        return CommandRecordVulkan::ExecutorType::Graphics;
    }

    void commitCommand(HardwareExecutorVulkan &hardwareExecutorVulkan) override;
    RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutorVulkan) override;

  private:
    struct TriangleGeomMesh
    {
        HardwareBuffer indexBuffer;
        HardwareBuffer vertexBuffer;
        DrawIndexedParams drawParams;
        HardwarePushConstant pushConstant;
    };
    std::vector<TriangleGeomMesh> geomMeshesRecord;

    void createRenderPass(int multiviewCount = 1);
    void createGraphicsPipeline(EmbeddedShader::ShaderCodeModule &vertShaderCode,
                                EmbeddedShader::ShaderCodeModule &fragShaderCode);
    void createFramebuffers(ktm::uvec2 imageSize);

    [[nodiscard]] VkFormat getVkFormatFromType(const std::string &typeName, uint32_t elementCount) const;

    ktm::uvec2 imageSize = {0, 0};
    uint32_t pushConstantSize{0};
    int multiviewCount{1};

    VkRenderPass renderPass{VK_NULL_HANDLE};
    VkPipeline graphicsPipeline{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkFramebuffer frameBuffers{VK_NULL_HANDLE};

    bool depthEnabled{true};
    //bool depthWriteEnabled{true};
    bool graphicsPipelineDirty{false};

    HardwareImage depthImage;
    std::vector<HardwareImage> renderTargets;

    EmbeddedShader::ShaderCodeModule vertShaderCode;
    EmbeddedShader::ShaderCodeModule fragShaderCode;

    CommandRecordVulkan dumpCommandRecordVulkan;
    HardwarePushConstant tempPushConstant;
    // std::vector<HardwareBuffer> tempVertexBuffers;

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

    std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> vertexStageInputs;
    std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> vertexStageOutputs;
    std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> fragmentStageInputs;
    std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> fragmentStageOutputs;
};