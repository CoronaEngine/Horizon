#include "ComputePipeline.h"

#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include "Compiler/ShaderLanguageConverter.h"
#include <cstring>

namespace
{
using BindType = EmbeddedShader::ShaderCodeModule::ShaderResources::BindType;

constexpr uint32_t kDescriptorHandle32Size = sizeof(uint32_t);
constexpr uint32_t kDescriptorHandle64Size = sizeof(uint32_t) * 2;

bool copy_to_push_constant(HardwarePushConstant &target, uint64_t byteOffset, const void *src, size_t size)
{
    uint8_t *dst = target.getData();
    if (!dst || !src || size == 0)
    {
        return false;
    }

    const uint64_t targetSize = target.getSize();
    if (byteOffset > targetSize || size > targetSize - byteOffset)
    {
        return false;
    }

    std::memcpy(dst + byteOffset, src, size);
    return true;
}

uint32_t descriptor_write_size(uint32_t reflectedTypeSize)
{
    if (reflectedTypeSize == 0)
    {
        return kDescriptorHandle64Size;
    }
    return reflectedTypeSize >= kDescriptorHandle64Size ? kDescriptorHandle64Size : kDescriptorHandle32Size;
}

bool write_descriptor_handle(HardwarePushConstant &target, uint64_t byteOffset, uint32_t reflectedTypeSize, uint32_t descriptorIndex)
{
    const uint32_t writeSize = descriptor_write_size(reflectedTypeSize);
    if (writeSize >= kDescriptorHandle64Size)
    {
        const uint32_t handleData[2] = {descriptorIndex, 0};
        return copy_to_push_constant(target, byteOffset, handleData, sizeof(handleData));
    }
    return copy_to_push_constant(target, byteOffset, &descriptorIndex, sizeof(descriptorIndex));
}

bool is_buffer_resource_bind_type(BindType bindType)
{
    return bindType == BindType::rawBuffer || bindType == BindType::storageBuffer;
}

bool is_image_resource_bind_type(BindType bindType)
{
    return bindType == BindType::sampledImages ||
           bindType == BindType::texture ||
           bindType == BindType::sampler ||
           bindType == BindType::storageTexture;
}
} // namespace

ComputePipelineVulkan::ComputePipelineVulkan()
{
    executorType = CommandRecordVulkan::ExecutorType::Compute;
}

ComputePipelineVulkan::ComputePipelineVulkan(std::string shaderCode,
                                             EmbeddedShader::ShaderLanguage language,
                                             const std::source_location &sourceLocation)
    : ComputePipelineVulkan()
{
    EmbeddedShader::ShaderCodeCompiler compiler(shaderCode,
                                                EmbeddedShader::ShaderStage::ComputeShader,
                                                language,
                                                EmbeddedShader::CompilerOption(),
                                                sourceLocation);

    this->shaderCode = compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV);

    const uint32_t pushConstantSize = this->shaderCode.shaderResources.pushConstantSize;
    if (pushConstantSize > 0)
    {
        this->pushConstant = HardwarePushConstant(pushConstantSize, 0);
    }

    // 初始化 per-pipeline UBO
    uboSize = this->shaderCode.shaderResources.uniformBufferSize;
    if (uboSize > 0)
    {
        tempUBO = HardwarePushConstant(uboSize, 0);
        uboBuffer = HardwareBuffer(uboSize, BufferUsage::UniformBuffer);
    }
}

ComputePipelineVulkan::ComputePipelineVulkan(const EmbeddedShader::ShaderCodeCompiler &compiler,
                                             const std::source_location &sourceLocation)
    : ComputePipelineVulkan()
{
    this->shaderCode = compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, true);

    const uint32_t pushConstantSize = this->shaderCode.shaderResources.pushConstantSize;
    if (pushConstantSize > 0)
    {
        this->pushConstant = HardwarePushConstant(pushConstantSize, 0);
    }

    // 初始化 per-pipeline UBO
    uboSize = this->shaderCode.shaderResources.uniformBufferSize;
    if (uboSize > 0)
    {
        tempUBO = HardwarePushConstant(uboSize, 0);
        uboBuffer = HardwareBuffer(uboSize, BufferUsage::UniformBuffer);
    }
}

ComputePipelineVulkan::ComputePipelineVulkan(const std::vector<uint32_t> &spirV,
                                             const std::source_location &sourceLocation)
    : ComputePipelineVulkan()
{
    // 直接使用预编译 SPIR-V + spirv-cross 反射（跳过 GLSL→SPIR-V 编译）
    auto resources = EmbeddedShader::ShaderLanguageConverter::spirvCrossReflectedBindInfo(spirV, EmbeddedShader::ShaderLanguage::HLSL);

    this->shaderCode = EmbeddedShader::ShaderCodeModule(spirV, resources);

    const uint32_t pushConstantSize = resources.pushConstantSize;
    if (pushConstantSize > 0)
    {
        this->pushConstant = HardwarePushConstant(pushConstantSize, 0);
    }

    uboSize = resources.uniformBufferSize;
    if (uboSize > 0)
    {
        tempUBO = HardwarePushConstant(uboSize, 0);
        uboBuffer = HardwareBuffer(uboSize, BufferUsage::UniformBuffer);
    }
}

ComputePipelineVulkan::~ComputePipelineVulkan()
{
    VkDevice device = VK_NULL_HANDLE;
    if (const auto mainDevice = globalHardwareContext.getMainDevice())
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
        if (uboDescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device, uboDescriptorPool, nullptr);
            uboDescriptorPool = VK_NULL_HANDLE;
        }
        if (uboDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, uboDescriptorSetLayout, nullptr);
            uboDescriptorSetLayout = VK_NULL_HANDLE;
        }
    }
}

void ComputePipelineVulkan::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    const auto typedBindType = static_cast<BindType>(bindType);
    if (typedBindType == BindType::pushConstantMembers)
    {
        copy_to_push_constant(pushConstant, byteOffset, data, size);
        return;
    }
    if (typedBindType == BindType::uniformBufferMembers)
    {
        if (copy_to_push_constant(tempUBO, byteOffset, data, size))
        {
            uboDescriptorDirty = true;
        }
        return;
    }
}

void ComputePipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    const auto typedBindType = static_cast<BindType>(bindType);
    const uint32_t descriptorIndex = const_cast<HardwareBuffer &>(buffer).storeDescriptor();

    if (typedBindType == BindType::pushConstantMembers)
    {
        write_descriptor_handle(pushConstant, byteOffset, typeSize, descriptorIndex);
        return;
    }
    if (typedBindType == BindType::uniformBufferMembers)
    {
        if (write_descriptor_handle(tempUBO, byteOffset, typeSize, descriptorIndex))
        {
            uboDescriptorDirty = true;
        }
        return;
    }
    if (!is_buffer_resource_bind_type(typedBindType))
    {
        return;
    }

    if (write_descriptor_handle(pushConstant, byteOffset, typeSize, descriptorIndex))
    {
        return;
    }
    if (write_descriptor_handle(tempUBO, byteOffset, typeSize, descriptorIndex))
    {
        uboDescriptorDirty = true;
    }
}

void ComputePipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType)
{
    const auto typedBindType = static_cast<BindType>(bindType);
    const uint32_t descriptorIndex = const_cast<HardwareImage &>(image).storeDescriptor();

    if (typedBindType == BindType::pushConstantMembers)
    {
        write_descriptor_handle(pushConstant, byteOffset, typeSize, descriptorIndex);
        return;
    }
    if (typedBindType == BindType::uniformBufferMembers)
    {
        if (write_descriptor_handle(tempUBO, byteOffset, typeSize, descriptorIndex))
        {
            uboDescriptorDirty = true;
        }
        return;
    }
    if (!is_image_resource_bind_type(typedBindType))
    {
        return;
    }

    if (write_descriptor_handle(pushConstant, byteOffset, typeSize, descriptorIndex))
    {
        return;
    }
    if (write_descriptor_handle(tempUBO, byteOffset, typeSize, descriptorIndex))
    {
        uboDescriptorDirty = true;
    }
}

ComputePipelineVulkan *ComputePipelineVulkan::operator()(uint16_t x, uint16_t y, uint16_t z)
{
    groupCount = {x, y, z};
    return this;
}

CommandRecordVulkan::RequiredBarriers ComputePipelineVulkan::getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor)
{
    RequiredBarriers requiredBarriers;
    requiredBarriers.memoryBarriers.resize(1);

    auto &barrier = requiredBarriers.memoryBarriers[0];
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    return requiredBarriers;
}

void ComputePipelineVulkan::createComputePipeline()
{
    const auto mainDevice = globalHardwareContext.getMainDevice();
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
        createPerPipelineUBODescriptor();
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

void ComputePipelineVulkan::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{
    // 延迟创建管线
    if (pipelineLayout == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE)
    {
        createComputePipeline();
    }

    const VkCommandBuffer commandBuffer = hardwareExecutor.currentRecordQueue->commandBuffer;

    // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    // 上传 UBO 数据并更新描述符
    if (uboSize > 0 && tempUBO.getData())
    {
        uboBuffer.copyFromData(tempUBO.getData(), uboSize);
        if (uboDescriptorDirty)
        {
            updateUBODescriptor();
            uboDescriptorDirty = false;
        }
    }

    // 绑定描述符集
    std::vector<VkDescriptorSet> descriptorSets;
    descriptorSets.reserve(4);
    for (size_t i = 0; i < 3; ++i)
    {
        descriptorSets.push_back(globalHardwareContext.getMainDevice()->resourceManager.bindlessDescriptors[i].descriptorSet);
    }
    if (uboSize > 0)
    {
        descriptorSets.push_back(uboDescriptorSet);
    }

    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout,
                            0,
                            static_cast<uint32_t>(descriptorSets.size()),
                            descriptorSets.data(),
                            0,
                            nullptr);

    // 推送常量
    if (const void *data = pushConstant.getData(); data != nullptr)
    {
        const uint32_t pushConstantSize = shaderCode.shaderResources.pushConstantSize;
        if (pushConstantSize > 0)
        {
            vkCmdPushConstants(commandBuffer,
                               pipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT,
                               0,
                               pushConstantSize,
                               data);
        }
    }

    // 调度计算任务
    vkCmdDispatch(commandBuffer, groupCount.x, groupCount.y, groupCount.z);
}

void ComputePipelineVulkan::createPerPipelineUBODescriptor()
{
    const auto mainDevice = globalHardwareContext.getMainDevice();
    if (!mainDevice)
    {
        throw std::runtime_error("No main device available");
    }
    const VkDevice device = mainDevice->deviceManager.getLogicalDevice();

    // 创建描述符集布局 - 单个 UBO 实例
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
    layoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;

    coronaHardwareCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &uboDescriptorSetLayout));

    // 创建描述符池
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    coronaHardwareCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &uboDescriptorPool));

    // 分配描述符集
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = uboDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &uboDescriptorSetLayout;

    coronaHardwareCheck(vkAllocateDescriptorSets(device, &allocInfo, &uboDescriptorSet));

    uboDescriptorDirty = true;
}

void ComputePipelineVulkan::updateUBODescriptor()
{
    const auto mainDevice = globalHardwareContext.getMainDevice();
    if (!mainDevice)
    {
        return;
    }
    const VkDevice device = mainDevice->deviceManager.getLogicalDevice();

    auto bufferHandle = globalBufferStorages.acquire_read(uboBuffer.getBufferID());

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = bufferHandle->bufferHandle;
    bufferInfo.offset = 0;
    bufferInfo.range = uboSize;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = uboDescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
