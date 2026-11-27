#include "ComputePipeline.h"

#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include <span>
#include <array>
#include <stdexcept>

template<class CS>
struct ComputePipeline<CS>::Impl
{
    std::shared_ptr<ComputePipelineVulkan> backend;
    std::unique_ptr<EmbeddedShader::ComputePipelineObject> variants;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries;
    std::vector<ResourceUse> resourceUses;

    static Impl& checked(const std::unique_ptr<Impl>& impl)
    {
        if (!impl || !impl->backend) throw std::runtime_error("Empty ComputePipeline<> handle");
        return *impl;
    }
};

template<class CS>
ComputePipeline<CS>::ComputePipeline() = default;

template<class CS>
ComputePipeline<CS>::ComputePipeline(EmbeddedShader::ComputeEntry shader, ktm::uvec3 numthreads,
                                     EmbeddedShader::CompilerOption options, const std::source_location& location)
    : impl_(std::make_unique<Impl>())
{
    if (shader.module) {
        impl_->backend = std::make_shared<ComputePipelineVulkan>(*shader.module);
        return;
    }
    auto compiled = shader.compile(numthreads, std::move(options), location);
    impl_->backend = std::make_shared<ComputePipelineVulkan>(compiled);
    impl_->autoBindEntries = std::move(compiled.autoBindEntries);
    if (!compiled.conditions.empty())
        impl_->variants = std::make_unique<EmbeddedShader::ComputePipelineObject>(std::move(compiled));
}

template<class CS>
ComputePipeline<CS>::~ComputePipeline() = default;

template<class CS>
ComputePipeline<CS>::ComputePipeline(ComputePipeline&&) noexcept = default;

template<class CS>
ComputePipeline<CS>& ComputePipeline<CS>::operator=(ComputePipeline&&) noexcept = default;

template<class CS>
ComputePipeline<CS>& ComputePipeline<CS>::uses(std::span<const ResourceUse> resources)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->resourceUses.assign(resources.begin(), resources.end());
    return *this;
}

template<class CS>
HardwareExecutor::Command ComputePipeline<CS>::operator()(uint16_t x, uint16_t y, uint16_t z)
{
    auto& impl = Impl::checked(impl_);
    if (auto& variants = impl.variants)
    {
        auto conditions = variants->compute->getCurrentConditionInfo();
        if (conditions != variants->conditions)
        {
            impl.backend = std::make_shared<ComputePipelineVulkan>(variants->updateAutoBind(conditions));
            impl.autoBindEntries = std::move(variants->autoBindEntries);
            variants->conditions = std::move(conditions);
        }
    }

    // Auto-bind: read current resource from each EDSL proxy's back-pointer
    auto& backend = *impl.backend;
    for (const auto& entry : impl.autoBindEntries)
    {
        if (entry.boundValueRef)
        {
            backend.setPushConstantDirect(entry.byteOffset, entry.boundValueRef, entry.boundValueSize, entry.bindType);
        }
        else if (entry.boundResourceRef && *entry.boundResourceRef)
        {
            void* res = *entry.boundResourceRef;
            if (entry.boundResourceIsBuffer)
                backend.setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareBuffer*>(res), entry.bindType);
            else
                backend.setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType);
        }
    }
    return HardwareExecutor::Command(std::make_shared<HardwareExecutor::Command::Impl::Dispatch>(
        impl.backend, ktm::uvec3{x, y, z}, impl.resourceUses));
}

template struct ComputePipeline<void>;
template struct EmbeddedShader::BoundField<ComputePipeline<>>;

ComputePipelineVulkan::ComputePipelineVulkan(const EmbeddedShader::ComputePipelineObject& shader)
    : ComputePipelineVulkan(shader.compute->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, shader.conditions)) {}

ComputePipelineVulkan::ComputePipelineVulkan(EmbeddedShader::ShaderCodeModule module)
    : shaderCode(std::move(module))
{
    const auto &resources = shaderCode.shaderResources;

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
    if (!pipeline && !pipelineLayout && !uboDescriptorSetLayout) return;
    VkDevice device = VK_NULL_HANDLE;
    if (const auto mainDevice = getHardwareContext().getMainDevice())
    {
        device = mainDevice->deviceManager.getLogicalDevice();
    }

    if (device != VK_NULL_HANDLE)
    {
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

HardwareExecutor::Command::Impl::Dispatch::Dispatch(std::shared_ptr<ComputePipelineVulkan> pipeline, ktm::uvec3 groups,
                                          const std::vector<ResourceUse>& resources)
    : backend(std::move(pipeline)), groupCount(groups),
      pushConstant(copyParameterBytes(backend->pushConstant.get(), backend->shaderCode.shaderResources.pushConstantSize)),
      uniformBytes(copyParameterBytes(backend->tempUBO.get(), backend->uboSize)),
      bindings(backend->bindings.snapshot()), resourceUses(resources) {}

void HardwareExecutor::Command::Impl::Dispatch::resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList& result)
{
    resolveShaderBindings(result, bindings->values, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    resolveDeclaredUses(result, resourceUses, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
}

void HardwareExecutor::Command::Impl::Dispatch::commitCommand(HardwareExecutorVulkan& executor)
{
    if (!backend->pipelineLayout || !backend->pipeline) backend->createComputePipeline();
    const auto commandBuffer = executor.currentRecordQueue->commandBuffer;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, backend->pipeline);
    bindings->prepareDescriptors();
    bindings->writeParameters(pushConstant, uniformBytes);
    if (!uniform) uniform = backend->uniformPool.acquire(uniformBytes, backend->uboDescriptorSetLayout);
    std::array<VkDescriptorSet, 4> sets{};
    const auto& tables = getHardwareContext().getMainDevice()->resourceManager.bindlessDescriptors;
    for (size_t i = 0; i < 3; ++i)
        sets[i] = bindings->sets[i] ? bindings->sets[i] : tables[i].descriptorSet;
    sets[3] = uniform ? uniform->set : VK_NULL_HANDLE;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, backend->pipelineLayout,
                            0, sets[3] ? 4 : 3, sets.data(), 0, nullptr);
    if (!pushConstant.empty())
        vkCmdPushConstants(commandBuffer, backend->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, static_cast<uint32_t>(pushConstant.size()), pushConstant.data());
    vkCmdDispatch(commandBuffer, groupCount.x, groupCount.y, groupCount.z);
}
