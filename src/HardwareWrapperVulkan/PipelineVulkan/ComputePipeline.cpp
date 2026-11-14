#include "ComputePipeline.h"

#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "Compiler/ShaderLanguageConverter.h"
#include <span>
#include <array>

ComputePipelineVulkan::ComputePipelineVulkan() = default;

ComputePipelineVulkan::ComputePipelineVulkan(std::string shaderCode,
                                             EmbeddedShader::ShaderLanguage /*language*/,
                                             const std::source_location &sourceLocation)
    : ComputePipelineVulkan()
{
    // Keep the legacy signature; Helicon's runtime compiler now consumes Slang source.
    EmbeddedShader::ShaderCodeCompiler compiler(shaderCode,
                                                EmbeddedShader::ShaderStage::ComputeShader,
                                                EmbeddedShader::CompilerOption(),
                                                sourceLocation);

    this->shaderCode = compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV);

    const uint32_t pushConstantSize = this->shaderCode.shaderResources.pushConstantSize;
    if (pushConstantSize > 0)
    {
        this->pushConstant = std::make_unique<uint8_t[]>(pushConstantSize);
    }

    // 初始化 per-pipeline UBO
    uboSize = this->shaderCode.shaderResources.uniformBufferSize;
    if (uboSize > 0)
    {
        tempUBO = std::make_unique<uint8_t[]>(uboSize);
    }
}

ComputePipelineVulkan::ComputePipelineVulkan(EmbeddedShader::ShaderCodeCompiler &compiler,
                                             const std::source_location &sourceLocation)
    : ComputePipelineVulkan()
{
    this->shaderCode = compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV);

    const uint32_t pushConstantSize = this->shaderCode.shaderResources.pushConstantSize;
    if (pushConstantSize > 0)
    {
        this->pushConstant = std::make_unique<uint8_t[]>(pushConstantSize);
    }

    // 初始化 per-pipeline UBO
    uboSize = this->shaderCode.shaderResources.uniformBufferSize;
    if (uboSize > 0)
    {
        tempUBO = std::make_unique<uint8_t[]>(uboSize);
    }
}

ComputePipelineVulkan::ComputePipelineVulkan(const EmbeddedShader::ShaderCodeModule &spirV,
                                             const std::source_location &sourceLocation)
    : ComputePipelineVulkan()
{
    // Reuse SPIR-V and reflection emitted together by the current Slang compiler.
    const auto &resources = spirV.shaderResources;

    this->shaderCode = spirV;

    const uint32_t pushConstantSize = resources.pushConstantSize;
    if (pushConstantSize > 0)
    {
        this->pushConstant = std::make_unique<uint8_t[]>(pushConstantSize);
    }

    uboSize = resources.uniformBufferSize;
    if (uboSize > 0)
    {
        tempUBO = std::make_unique<uint8_t[]>(uboSize);
    }
}

ComputePipelineVulkan::~ComputePipelineVulkan()
{
    VkDevice device = VK_NULL_HANDLE;
    if (const auto mainDevice = getHardwareContext().getMainDevice())
    {
        device = mainDevice->deviceManager.getLogicalDevice();
    }

    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);

        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }

        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        // 清理 per-pipeline UBO 描述符资源
        if (uboDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, uboDescriptorSetLayout, nullptr);
            uboDescriptorSetLayout = VK_NULL_HANDLE;
        }
    }
}

void ComputePipelineVulkan::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    bindings.writeValue({pushConstant.get(), shaderCode.shaderResources.pushConstantSize}, {tempUBO.get(), uboSize},
                        bindType, byteOffset, data, size);
}

void ComputePipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    bindings.writeResource({pushConstant.get(), shaderCode.shaderResources.pushConstantSize}, {tempUBO.get(), uboSize},
                           bindType, byteOffset, typeSize, buffer);
}

void ComputePipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType)
{
    bindings.writeResource({pushConstant.get(), shaderCode.shaderResources.pushConstantSize}, {tempUBO.get(), uboSize},
                           bindType, byteOffset, typeSize, image);
}

ComputePipelineVulkan *ComputePipelineVulkan::operator()(uint16_t x, uint16_t y, uint16_t z)
{
    groupCount = {x, y, z};
    return this;
}

void ComputePipelineVulkan::createComputePipeline()
{
    const auto mainDevice = getHardwareContext().getMainDevice();
    if (!mainDevice)
    {
        throw std::runtime_error("No main device available");
    }

    const VkDevice device = mainDevice->deviceManager.getLogicalDevice();

    // 创建着色器模块
    VkShaderModule shaderModule = mainDevice->resourceManager.createShaderModule(shaderCode);

    // 配置计算着色器阶段
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    // 配置推送常量
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = shaderCode.shaderResources.pushConstantSize;

    // 获取描述符集布局
    // 创建 per-pipeline UBO 描述符（如果有 UBO）
    if (uboSize > 0)
    {
        if (!uboDescriptorSetLayout)
            uboDescriptorSetLayout = createUniformSnapshotLayout(device);
    }

    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.reserve(4);
    for (size_t i = 0; i < 3; ++i)
    {
        setLayouts.push_back(mainDevice->resourceManager.bindlessDescriptors[i].descriptorSetLayout);
    }
    if (uboSize > 0)
    {
        setLayouts.push_back(uboDescriptorSetLayout);
    }

    // 创建管线布局
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = pushConstantRange.size > 0 ? 1 : 0;
    pipelineLayoutInfo.pPushConstantRanges = pushConstantRange.size > 0 ? &pushConstantRange : nullptr;

    coronaHardwareCheck(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    // 创建计算管线
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout;

    coronaHardwareCheck(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

    // 清理着色器模块
    vkDestroyShaderModule(device, shaderModule, nullptr);
}

std::shared_ptr<ComputePipelineVulkan::Dispatch> ComputePipelineVulkan::capture()
{
    auto dispatch = std::make_shared<Dispatch>();
    dispatch->pipeline = this;
    dispatch->groupCount = groupCount;
    dispatch->pushConstant = copyParameterBytes(pushConstant.get(), shaderCode.shaderResources.pushConstantSize);
    dispatch->uniformBytes = copyParameterBytes(tempUBO.get(), uboSize);
    dispatch->bindings = bindings.snapshot();
    return dispatch;
}

void ComputePipelineVulkan::Dispatch::commitCommand(HardwareExecutorVulkan& executor)
{
    pipeline->commitCommand(executor, *this);
}

void ComputePipelineVulkan::Dispatch::resolveResourceUses(std::span<const ResourceUse> declarations, ResourceAccessList& result)
{
    resolveShaderBindings(result, bindings->values, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    resolveDeclaredUses(result, declarations, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
}

void ComputePipelineVulkan::commitCommand(HardwareExecutorVulkan& executor, Dispatch& dispatch)
{
    if (!pipelineLayout || !pipeline) createComputePipeline();
    const auto commandBuffer = executor.currentRecordQueue->commandBuffer;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    dispatch.bindings->prepareDescriptors();
    dispatch.bindings->writeParameters(dispatch.pushConstant, dispatch.uniformBytes);
    if (!dispatch.uniform) dispatch.uniform = uniformPool.acquire(dispatch.uniformBytes, uboDescriptorSetLayout);
    std::array<VkDescriptorSet, 4> sets{};
    for (size_t i = 0; i < 3; ++i)
        sets[i] = dispatch.bindings->sets[i] ? dispatch.bindings->sets[i] : getHardwareContext().getMainDevice()->resourceManager.bindlessDescriptors[i].descriptorSet;
    sets[3] = (dispatch.uniform ? dispatch.uniform->set : VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                            0, sets[3] ? 4 : 3, sets.data(), 0, nullptr);
    if (!dispatch.pushConstant.empty())
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, static_cast<uint32_t>(dispatch.pushConstant.size()), dispatch.pushConstant.data());
    vkCmdDispatch(commandBuffer, dispatch.groupCount.x, dispatch.groupCount.y, dispatch.groupCount.z);
}
