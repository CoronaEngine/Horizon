#include "RasterizerPipeline.h"

#include <algorithm>
#include <cstring>

#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include "Compiler/ShaderLanguageConverter.h"

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

bool bind_image_descriptor_slot(const HardwareImage &image, uint32_t descriptorSlot)
{
    auto mainDevice = globalHardwareContext.getMainDevice();
    if (!mainDevice)
    {
        return false;
    }

    const uintptr_t imageId = const_cast<HardwareImage &>(image).getImageID();
    if (imageId == 0)
    {
        return false;
    }

    auto imageHandle = globalImageStorages.acquire_write(imageId);
    return mainDevice->resourceManager.storeDescriptorAt(imageHandle, descriptorSlot);
}
} // namespace

void extractStageBindings(const EmbeddedShader::ShaderCodeModule::ShaderResources &resources,
                          std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> &inputs,
                          std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> &outputs)
{
    using BindType = EmbeddedShader::ShaderCodeModule::ShaderResources::BindType;

    for (const auto &bindInfo : resources.bindInfoPool)
    {
        if (bindInfo.bindType == BindType::stageInputs)
        {
            inputs.push_back(bindInfo);
        }
        else if (bindInfo.bindType == BindType::stageOutputs)
        {
            outputs.push_back(bindInfo);
        }
    }
}

VkFormat getVkFormatFromType(const std::string &typeName, uint32_t elementCount)
{
    if (typeName == "float")
    {
        switch (elementCount)
        {
        case 1:
            return VK_FORMAT_R32_SFLOAT;
        case 2:
            return VK_FORMAT_R32G32_SFLOAT;
        case 3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case 4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        default:
            break;
        }
    }
    else if (typeName == "int")
    {
        switch (elementCount)
        {
        case 1:
            return VK_FORMAT_R32_SINT;
        case 2:
            return VK_FORMAT_R32G32_SINT;
        case 3:
            return VK_FORMAT_R32G32B32_SINT;
        case 4:
            return VK_FORMAT_R32G32B32A32_SINT;
        default:
            break;
        }
    }
    else if (typeName == "uint")
    {
        switch (elementCount)
        {
        case 1:
            return VK_FORMAT_R32_UINT;
        case 2:
            return VK_FORMAT_R32G32_UINT;
        case 3:
            return VK_FORMAT_R32G32B32_UINT;
        case 4:
            return VK_FORMAT_R32G32B32A32_UINT;
        default:
            break;
        }
    }

    throw std::runtime_error("Unsupported vertex attribute type: " + typeName);
}

RasterizerPipelineVulkan::RasterizerPipelineVulkan()
{
    executorType = CommandRecordVulkan::ExecutorType::Graphics;
}

RasterizerPipelineVulkan::RasterizerPipelineVulkan(std::string vertexShaderCode,
                                                   std::string fragmentShaderCode,
                                                   uint32_t multiviewCount,
                                                   EmbeddedShader::ShaderLanguage vertexShaderLanguage,
                                                   EmbeddedShader::ShaderLanguage fragmentShaderLanguage,
                                                   const std::source_location &sourceLocation)
    : RasterizerPipelineVulkan()
{
    // 编译着色器
    EmbeddedShader::ShaderCodeCompiler vertexCompiler(vertexShaderCode,
                                                      EmbeddedShader::ShaderStage::VertexShader,
                                                      vertexShaderLanguage,
                                                      EmbeddedShader::CompilerOption(),
                                                      sourceLocation);

    EmbeddedShader::ShaderCodeCompiler fragmentCompiler(fragmentShaderCode,
                                                        EmbeddedShader::ShaderStage::FragmentShader,
                                                        fragmentShaderLanguage,
                                                        EmbeddedShader::CompilerOption(),
                                                        sourceLocation);

    // 获取 SPIR-V 代码
    vertShaderCode = vertexCompiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV);
    fragShaderCode = fragmentCompiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV);

    // 获取着色器资源
    const auto &vertexResource = vertShaderCode.shaderResources;
    const auto &fragmentResource = fragShaderCode.shaderResources;

    // 提取阶段绑定信息
    extractStageBindings(vertexResource, vertexStageInputs, vertexStageOutputs);
    extractStageBindings(fragmentResource, fragmentStageInputs, fragmentStageOutputs);

    // 初始化缓冲区和渲染目标
    // tempVertexBuffers.resize(vertexStageInputs.size());
    renderTargets.resize(fragmentStageOutputs.size());

    // 设置多视图计数
    this->multiviewCount = multiviewCount;

    // 验证并计算推送常量大小
    const uint32_t vertPushConstSize = vertShaderCode.shaderResources.pushConstantSize;
    const uint32_t fragPushConstSize = fragShaderCode.shaderResources.pushConstantSize;

    if (vertPushConstSize != 0 && fragPushConstSize != 0 && vertPushConstSize != fragPushConstSize)
    {
        throw std::runtime_error("Vertex and fragment shader push constant sizes mismatch");
    }

    pushConstantSize = std::max(vertPushConstSize, fragPushConstSize);

    if (pushConstantSize > 0)
    {
        tempPushConstant = HardwarePushConstant(pushConstantSize, 0);
    }

    // 初始化 per-pipeline UBO
    const uint32_t vertUBOSize = vertexResource.uniformBufferSize;
    const uint32_t fragUBOSize = fragmentResource.uniformBufferSize;
    uboSize = std::max(vertUBOSize, fragUBOSize);
    if (uboSize > 0)
    {
        tempUBO = HardwarePushConstant(uboSize, 0);
        uboBuffer = HardwareBuffer(uboSize, BufferUsage::UniformBuffer);
    }
}

RasterizerPipelineVulkan::RasterizerPipelineVulkan(const EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                                                   const EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                                                   uint32_t multiviewCount,
                                                   const std::source_location &sourceLocation)
    : RasterizerPipelineVulkan()
{
    // 直接从已编译的 ShaderCodeCompiler 获取 SPIR-V 代码
    vertShaderCode = vertexCompiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, true);
    fragShaderCode = fragmentCompiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, true);

    // 获取着色器资源
    const auto &vertexResource = vertShaderCode.shaderResources;
    const auto &fragmentResource = fragShaderCode.shaderResources;

    // 提取阶段绑定信息
    extractStageBindings(vertexResource, vertexStageInputs, vertexStageOutputs);
    extractStageBindings(fragmentResource, fragmentStageInputs, fragmentStageOutputs);

    // 初始化渲染目标
    renderTargets.resize(fragmentStageOutputs.size());

    // 设置多视图计数
    this->multiviewCount = multiviewCount;

    // 验证并计算推送常量大小
    const uint32_t vertPushConstSize = vertShaderCode.shaderResources.pushConstantSize;
    const uint32_t fragPushConstSize = fragShaderCode.shaderResources.pushConstantSize;

    if (vertPushConstSize != 0 && fragPushConstSize != 0 && vertPushConstSize != fragPushConstSize)
    {
        throw std::runtime_error("Vertex and fragment shader push constant sizes mismatch");
    }

    pushConstantSize = std::max(vertPushConstSize, fragPushConstSize);

    if (pushConstantSize > 0)
    {
        tempPushConstant = HardwarePushConstant(pushConstantSize, 0);
    }

    // 初始化 per-pipeline UBO
    const uint32_t vertUBOSize = vertexResource.uniformBufferSize;
    const uint32_t fragUBOSize = fragmentResource.uniformBufferSize;
    uboSize = std::max(vertUBOSize, fragUBOSize);
    if (uboSize > 0)
    {
        tempUBO = HardwarePushConstant(uboSize, 0);
        uboBuffer = HardwareBuffer(uboSize, BufferUsage::UniformBuffer);
    }
}

RasterizerPipelineVulkan::RasterizerPipelineVulkan(const std::vector<uint32_t> &vertexSpirV,
                                                   const std::vector<uint32_t> &fragmentSpirV,
                                                   uint32_t multiviewCount,
                                                   const std::source_location &sourceLocation)
    : RasterizerPipelineVulkan()
{
    // 直接使用预编译 SPIR-V + spirv-cross 反射（跳过 glslang 编译）
    auto vertResources = EmbeddedShader::ShaderLanguageConverter::spirvCrossReflectedBindInfo(vertexSpirV, EmbeddedShader::ShaderLanguage::HLSL);
    auto fragResources = EmbeddedShader::ShaderLanguageConverter::spirvCrossReflectedBindInfo(fragmentSpirV, EmbeddedShader::ShaderLanguage::HLSL);

    vertShaderCode = EmbeddedShader::ShaderCodeModule(vertexSpirV, vertResources);
    fragShaderCode = EmbeddedShader::ShaderCodeModule(fragmentSpirV, fragResources);

    extractStageBindings(vertResources, vertexStageInputs, vertexStageOutputs);
    extractStageBindings(fragResources, fragmentStageInputs, fragmentStageOutputs);

    renderTargets.resize(fragmentStageOutputs.size());
    this->multiviewCount = multiviewCount;

    const uint32_t vertPushConstSize = vertResources.pushConstantSize;
    const uint32_t fragPushConstSize = fragResources.pushConstantSize;
    if (vertPushConstSize != 0 && fragPushConstSize != 0 && vertPushConstSize != fragPushConstSize)
        throw std::runtime_error("Vertex and fragment shader push constant sizes mismatch");

    pushConstantSize = std::max(vertPushConstSize, fragPushConstSize);
    if (pushConstantSize > 0)
        tempPushConstant = HardwarePushConstant(pushConstantSize, 0);

    const uint32_t vertUBOSize = vertResources.uniformBufferSize;
    const uint32_t fragUBOSize = fragResources.uniformBufferSize;
    uboSize = std::max(vertUBOSize, fragUBOSize);
    if (uboSize > 0)
    {
        tempUBO = HardwarePushConstant(uboSize, 0);
        uboBuffer = HardwareBuffer(uboSize, BufferUsage::UniformBuffer);
    }
}

RasterizerPipelineVulkan::~RasterizerPipelineVulkan()
{
    VkDevice device = VK_NULL_HANDLE;
    if (const auto mainDevice = globalHardwareContext.getMainDevice())
    {
        device = mainDevice->deviceManager.getLogicalDevice();
    }

    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);

        if (frameBuffers != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device, frameBuffers, nullptr);
            frameBuffers = VK_NULL_HANDLE;
        }

        if (graphicsPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, graphicsPipeline, nullptr);
            graphicsPipeline = VK_NULL_HANDLE;
        }

        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
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

void RasterizerPipelineVulkan::createRenderPass(int multiviewCount)
{
    const auto mainDevice = globalHardwareContext.getMainDevice();
    if (!mainDevice)
    {
        throw std::runtime_error("No main device available");
    }

    // 配置附件描述
    std::vector<VkAttachmentDescription> attachments;
    attachments.reserve(renderTargets.size() + 1);

    // 颜色附件
    for (const auto &renderTarget : renderTargets)
    {
        VkAttachmentDescription attachment{};
        VkImageUsageFlags imageUsage{};
        {
            auto const handle = globalImageStorages.acquire_read(renderTarget.getImageID());
            attachment.format = handle->imageFormat;
            imageUsage = handle->imageUsage;
        }
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // 如果图像同时用作 storage image，渲染完成后转换为 GENERAL 布局
        // 这样 compute shader 可以正确访问它
        attachment.finalLayout = (imageUsage & VK_IMAGE_USAGE_STORAGE_BIT) 
                                     ? VK_IMAGE_LAYOUT_GENERAL 
                                     : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments.push_back(attachment);
    }

    // 深度附件
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments.push_back(depthAttachment);

    // 配置颜色附件引用
    std::vector<VkAttachmentReference> colorReferences;
    colorReferences.reserve(renderTargets.size());
    for (uint32_t i = 0; i < renderTargets.size(); ++i)
    {
        colorReferences.push_back({i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    }

    // 配置深度附件引用
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = static_cast<uint32_t>(colorReferences.size());
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // 配置子通道
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
    subpass.pColorAttachments = colorReferences.data();
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // 配置子通道依赖
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = 0;

    // 配置多视图
    const uint32_t viewMask = (1u << multiviewCount) - 1;
    VkRenderPassMultiviewCreateInfo multiviewCreateInfo{};
    multiviewCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
    multiviewCreateInfo.subpassCount = 1;
    multiviewCreateInfo.pViewMasks = &viewMask;
    multiviewCreateInfo.correlationMaskCount = 0;

    // 创建渲染通道
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    renderPassInfo.pNext = &multiviewCreateInfo;

    coronaHardwareCheck(vkCreateRenderPass(mainDevice->deviceManager.getLogicalDevice(), &renderPassInfo, nullptr, &renderPass));
}

void RasterizerPipelineVulkan::createGraphicsPipeline(EmbeddedShader::ShaderCodeModule &vertShaderCode,
                                                      EmbeddedShader::ShaderCodeModule &fragShaderCode)
{
    const auto mainDevice = globalHardwareContext.getMainDevice();
    if (!mainDevice)
    {
        throw std::runtime_error("No main device available");
    }

    const VkDevice device = mainDevice->deviceManager.getLogicalDevice();

    // 创建着色器模块
    VkShaderModule vertShaderModule = mainDevice->resourceManager.createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = mainDevice->resourceManager.createShaderModule(fragShaderCode);

    // 配置着色器阶段
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // 配置顶点输入
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(vertexStageInputs.size());
    for (size_t i = 0; i < attributeDescriptions.size(); i++)
    {
        attributeDescriptions[i].binding = 0;
        attributeDescriptions[i].location = vertexStageInputs[i].location;
        attributeDescriptions[i].format = getVkFormatFromType(vertexStageInputs[i].typeName, vertexStageInputs[i].elementCount);
        attributeDescriptions[i].offset = 0;
    }

    uint32_t strideSize = 0;
    for (size_t i = 0; i < attributeDescriptions.size(); i++)
    {
        strideSize += vertexStageInputs[i].typeSize;
        for (size_t j = 0; j < attributeDescriptions.size(); j++)
        {
            if (attributeDescriptions[j].location > attributeDescriptions[i].location)
            {
                attributeDescriptions[j].offset += vertexStageInputs[i].typeSize;
            }
        }
    }

    std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = strideSize;
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 配置输入装配
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 配置视口状态
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // 配置光栅化
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 配置多重采样
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 配置深度模板
    // RasterizerPipeline 默认用于 3D 几何绘制，因此开启深度测试与深度写入。
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = depthEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = depthEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = depthEnabled ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 配置颜色混合（ImGui 标准 Alpha 混合）
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(renderTargets.size(), colorBlendAttachment);

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    // 配置动态状态
    std::vector<VkDynamicState> dynamicStates =
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // 配置推送常量
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = pushConstantSize;

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
    pipelineLayoutInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;
    pipelineLayoutInfo.pPushConstantRanges = pushConstantSize > 0 ? &pushConstantRange : nullptr;

    coronaHardwareCheck(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    // 创建图形管线
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    coronaHardwareCheck(vkCreateGraphicsPipelines(device,
                                                  VK_NULL_HANDLE,
                                                  1,
                                                  &pipelineInfo,
                                                  nullptr,
                                                  &graphicsPipeline));

    // 清理着色器模块
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void RasterizerPipelineVulkan::createFramebuffers(ktm::uvec2 imageSize)
{
    const auto mainDevice = globalHardwareContext.getMainDevice();
    if (!mainDevice)
    {
        throw std::runtime_error("No main device available");
    }

    // 收集所有附件的图像视图
    std::vector<VkImageView> attachments;
    attachments.reserve(renderTargets.size() + 1);

    for (const auto &renderTarget : renderTargets)
    {
        auto const handle = globalImageStorages.acquire_read(renderTarget.getImageID());
        attachments.push_back(handle->imageView);
    }
    {
        auto const depthHandle = globalImageStorages.acquire_read(depthImage.getImageID());
        attachments.push_back(depthHandle->imageView);
    }

    // 创建帧缓冲
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = imageSize.x;
    framebufferInfo.height = imageSize.y;
    framebufferInfo.layers = 1;

    coronaHardwareCheck(vkCreateFramebuffer(mainDevice->deviceManager.getLogicalDevice(),
                                            &framebufferInfo,
                                            nullptr,
                                            &frameBuffers));
}

void RasterizerPipelineVulkan::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    const auto typedBindType = static_cast<BindType>(bindType);
    if (typedBindType == BindType::pushConstantMembers)
    {
        copy_to_push_constant(tempPushConstant, byteOffset, data, size);
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

void RasterizerPipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    const auto typedBindType = static_cast<BindType>(bindType);
    const uint32_t descriptorIndex = const_cast<HardwareBuffer &>(buffer).storeDescriptor();

    if (typedBindType == BindType::pushConstantMembers)
    {
        write_descriptor_handle(tempPushConstant, byteOffset, typeSize, descriptorIndex);
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

    if (write_descriptor_handle(tempPushConstant, byteOffset, typeSize, descriptorIndex))
    {
        return;
    }
    if (write_descriptor_handle(tempUBO, byteOffset, typeSize, descriptorIndex))
    {
        uboDescriptorDirty = true;
    }
}

void RasterizerPipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location)
{
    const auto typedBindType = static_cast<BindType>(bindType);

    // stageOutputs → render target
    if (typedBindType == BindType::stageOutputs)
    {
        if (location < renderTargets.size())
            renderTargets[location] = image;
        return;
    }

    const uint32_t descriptorIndex = const_cast<HardwareImage &>(image).storeDescriptor();

    if (typedBindType == BindType::pushConstantMembers)
    {
        write_descriptor_handle(tempPushConstant, byteOffset, typeSize, descriptorIndex);
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

    if (write_descriptor_handle(tempPushConstant, byteOffset, typeSize, descriptorIndex))
    {
        return;
    }
    if (write_descriptor_handle(tempUBO, byteOffset, typeSize, descriptorIndex))
    {
        uboDescriptorDirty = true;
        return;
    }

    bind_image_descriptor_slot(image, location);
}

RasterizerPipelineVulkan *RasterizerPipelineVulkan::operator()(uint16_t width, uint16_t height)
{
    imageSize = {width, height};
    return this;
}

CommandRecordVulkan *RasterizerPipelineVulkan::record(const HardwareBuffer &indexBuffer, const HardwareBuffer &vertexBuffer)
{
    DrawIndexedParams params;
    return record(indexBuffer, vertexBuffer, params);
}

CommandRecordVulkan *RasterizerPipelineVulkan::record(const HardwareBuffer &indexBuffer,
                                                      const HardwareBuffer &vertexBuffer,
                                                      const DrawIndexedParams &params)
{
    TriangleGeomMesh mesh;
    mesh.indexBuffer = indexBuffer;
    mesh.vertexBuffer = vertexBuffer;
    mesh.drawParams = params;

    mesh.pushConstant = tempPushConstant;

    // 重置临时推送常量
    if (pushConstantSize > 0)
    {
        tempPushConstant = HardwarePushConstant(pushConstantSize, 0);
    }

    geomMeshesRecord.push_back(std::move(mesh));

    return &dumpCommandRecordVulkan;
}

CommandRecordVulkan::RequiredBarriers RasterizerPipelineVulkan::getRequiredBarriers(HardwareExecutorVulkan &hardwareExecutor)
{
    RequiredBarriers requiredBarriers;

    // 内存屏障
    requiredBarriers.memoryBarriers.resize(1);
    auto &memoryBarrier = requiredBarriers.memoryBarriers[0];
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memoryBarrier.pNext = nullptr;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    // 图像屏障
    VkImageMemoryBarrier2 imageBarrierTemplate{};
    imageBarrierTemplate.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrierTemplate.pNext = nullptr;
    imageBarrierTemplate.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrierTemplate.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrierTemplate.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrierTemplate.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrierTemplate.subresourceRange.baseMipLevel = 0;
    imageBarrierTemplate.subresourceRange.levelCount = 1;
    imageBarrierTemplate.subresourceRange.baseArrayLayer = 0;
    imageBarrierTemplate.subresourceRange.layerCount = 1;

    // 颜色附件屏障
    for (auto &renderTarget : renderTargets)
    {
        {
            auto const handle = globalImageStorages.acquire_read(renderTarget.getImageID());
            VkImageMemoryBarrier2 imageBarrier = imageBarrierTemplate;
            imageBarrier.image = handle->imageHandle;
            imageBarrier.oldLayout = handle->imageLayout;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            imageBarrier.subresourceRange.aspectMask = handle->aspectMask;
            requiredBarriers.imageBarriers.push_back(imageBarrier);
        }

        // 更新图像布局为 render pass 的 finalLayout
        // 如果图像用作 storage image，finalLayout 为 GENERAL；否则为 COLOR_ATTACHMENT_OPTIMAL
        {
            auto handle = globalImageStorages.acquire_write(renderTarget.getImageID());
            handle->imageLayout = (handle->imageUsage & VK_IMAGE_USAGE_STORAGE_BIT) 
                                      ? VK_IMAGE_LAYOUT_GENERAL 
                                      : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }

    // 深度附件屏障
    if (depthImage)
    {
        {
            auto const handle = globalImageStorages.acquire_read(depthImage.getImageID());
            VkImageMemoryBarrier2 imageBarrier = imageBarrierTemplate;
            imageBarrier.image = handle->imageHandle;
            imageBarrier.oldLayout = handle->imageLayout;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            imageBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            imageBarrier.subresourceRange.aspectMask = handle->aspectMask;
            requiredBarriers.imageBarriers.push_back(imageBarrier);
        }

        // 更新图像布局
        {
            auto handle = globalImageStorages.acquire_write(depthImage.getImageID());
            handle->imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
    }

    // 缓冲区屏障
    VkBufferMemoryBarrier2 bufferBarrierTemplate{};
    bufferBarrierTemplate.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    bufferBarrierTemplate.pNext = nullptr;
    bufferBarrierTemplate.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    bufferBarrierTemplate.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    bufferBarrierTemplate.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrierTemplate.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrierTemplate.offset = 0;
    bufferBarrierTemplate.size = VK_WHOLE_SIZE;

    for (const auto &mesh : geomMeshesRecord)
    {
        // 索引缓冲区屏障

        VkBufferMemoryBarrier2 indexBarrier = bufferBarrierTemplate;
        {
            auto handle = globalBufferStorages.acquire_read(mesh.indexBuffer.getBufferID());
            indexBarrier.buffer = handle->bufferHandle;
        }
        indexBarrier.dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        indexBarrier.dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT;
        requiredBarriers.bufferBarriers.push_back(indexBarrier);

        // 顶点缓冲区屏障
        // for (const auto& vertexBuffer : mesh.vertexBuffers)
        {
            VkBufferMemoryBarrier2 vertexBarrier = bufferBarrierTemplate;
            {
                auto handle = globalBufferStorages.acquire_read(mesh.vertexBuffer.getBufferID());
                vertexBarrier.buffer = handle->bufferHandle;
            }
            vertexBarrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            vertexBarrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            requiredBarriers.bufferBarriers.push_back(vertexBarrier);
        }
    }

    return requiredBarriers;
}

void RasterizerPipelineVulkan::commitCommand(HardwareExecutorVulkan &hardwareExecutor)
{

    const auto mainDevice = globalHardwareContext.getMainDevice();

    // 延迟创建管线
    if (pipelineLayout == VK_NULL_HANDLE || graphicsPipeline == VK_NULL_HANDLE)
    {
        // 创建默认深度图像
        if (!depthImage)
        {
            HardwareImageCreateInfo depthCreateInfo;
            depthCreateInfo.width = imageSize.x;
            depthCreateInfo.height = imageSize.y;
            depthCreateInfo.format = ImageFormat::D32_FLOAT;
            depthCreateInfo.usage = ImageUsage::DepthImage;
            depthCreateInfo.arrayLayers = 1;
            depthCreateInfo.mipLevels = 1;
            // depthCreateInfo.initialData = nullptr;

            depthImage = HardwareImage(depthCreateInfo);
        }

        // 转换深度图像布局
        {
            auto const handle = globalImageStorages.acquire_write(depthImage.getImageID());
            mainDevice->resourceManager.transitionImageLayout(hardwareExecutor.currentRecordQueue->commandBuffer,
                                                              *handle,
                                                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        }
        createRenderPass(multiviewCount);
        createGraphicsPipeline(vertShaderCode, fragShaderCode);
        createFramebuffers(imageSize);
        graphicsPipelineDirty = false;
    }
    else if (graphicsPipelineDirty)
    {
        if (graphicsPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(mainDevice->deviceManager.getLogicalDevice(), graphicsPipeline, nullptr);
            graphicsPipeline = VK_NULL_HANDLE;
        }

        createGraphicsPipeline(vertShaderCode, fragShaderCode);
        graphicsPipelineDirty = false;
    }

    const VkCommandBuffer commandBuffer = hardwareExecutor.currentRecordQueue->commandBuffer;

    // 配置渲染通道
    std::vector<VkClearValue> clearValues;
    clearValues.reserve(renderTargets.size() + 1);

    for (const auto &renderTarget : renderTargets)
    {
        auto const handle = globalImageStorages.acquire_read(renderTarget.getImageID());
        clearValues.push_back(handle->clearValue);
    }

    {
        auto const handle = globalImageStorages.acquire_read(depthImage.getImageID());
        clearValues.push_back(handle->clearValue);
    }

    {
        auto const handle = globalImageStorages.acquire_read(depthImage.getImageID());
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = frameBuffers;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent.width = handle->imageSize.x;
        renderPassInfo.renderArea.extent.height = handle->imageSize.y;
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // 设置动态视口和裁剪区
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(handle->imageSize.x);
        viewport.height = static_cast<float>(handle->imageSize.y);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent.width = handle->imageSize.x;
        scissor.extent.height = handle->imageSize.y;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

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
        descriptorSets.push_back(mainDevice->resourceManager.bindlessDescriptors[i].descriptorSet);
    }
    if (uboSize > 0)
    {
        descriptorSets.push_back(uboDescriptorSet);
    }

    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout,
                            0,
                            static_cast<uint32_t>(descriptorSets.size()),
                            descriptorSets.data(),
                            0,
                            nullptr);

    // 绘制所有几何网格
    for (const auto &mesh : geomMeshesRecord)
    {
        if (mesh.drawParams.enableScissor)
        {
            const int32_t x = std::max<int32_t>(0, mesh.drawParams.scissor.x);
            const int32_t y = std::max<int32_t>(0, mesh.drawParams.scissor.y);
            const int32_t right = std::min<int32_t>(static_cast<int32_t>(imageSize.x),
                                                    mesh.drawParams.scissor.x + static_cast<int32_t>(mesh.drawParams.scissor.width));
            const int32_t bottom = std::min<int32_t>(static_cast<int32_t>(imageSize.y),
                                                     mesh.drawParams.scissor.y + static_cast<int32_t>(mesh.drawParams.scissor.height));

            if (right <= x || bottom <= y)
            {
                continue;
            }

            VkRect2D scissor{};
            scissor.offset = {x, y};
            scissor.extent.width = static_cast<uint32_t>(right - x);
            scissor.extent.height = static_cast<uint32_t>(bottom - y);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        }

        // 收集顶点缓冲区
        // std::vector<VkBuffer> vertexBuffers;
        // std::vector<VkDeviceSize> offsets;
        // vertexBuffers.reserve(mesh.vertexBuffers.size());
        // offsets.reserve(mesh.vertexBuffers.size());

        // for (const auto& vertexBuffer : mesh.vertexBuffers) {
        //     auto handle = globalBufferStorages.acquire_read(*vertexBuffer.getBufferID());
        //     vertexBuffers.push_back(handle->bufferHandle);
        //     offsets.push_back(0);
        // }

        // 绑定顶点缓冲区
        {
            auto vertexBufferHandle = globalBufferStorages.acquire_read(mesh.vertexBuffer.getBufferID());
            VkBuffer vertexBuffers[] = {vertexBufferHandle->bufferHandle};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer,
                                   0,
                                   1,
                                   vertexBuffers,
                                   offsets);
        }
        // 绑定索引缓冲区
        {
            auto handle = globalBufferStorages.acquire_read(mesh.indexBuffer.getBufferID());
            VkIndexType index_type = VK_INDEX_TYPE_UINT16;
            switch (mesh.drawParams.indexType)
            {
            case IndexType::UInt16:
                index_type = VK_INDEX_TYPE_UINT16;
                break;
            case IndexType::UInt32:
                index_type = VK_INDEX_TYPE_UINT32;
                break;
            case IndexType::Auto:
            default:
                if (handle->elementSize == sizeof(uint32_t))
                {
                    index_type = VK_INDEX_TYPE_UINT32;
                }
                break;
            }

            vkCmdBindIndexBuffer(commandBuffer,
                                 handle->bufferHandle,
                                 0,
                                 index_type);
        }

        // 推送常量
        if (const void *pushConstData = mesh.pushConstant.getData(); pushConstData != nullptr && pushConstantSize > 0)
        {
            vkCmdPushConstants(commandBuffer,
                               pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               pushConstantSize,
                               pushConstData);
        }

        {
            auto handle = globalBufferStorages.acquire_read(mesh.indexBuffer.getBufferID());
            const uint32_t draw_index_count =
                mesh.drawParams.indexCount > 0 ? mesh.drawParams.indexCount : static_cast<uint32_t>(handle->elementCount);
            vkCmdDrawIndexed(commandBuffer,
                             draw_index_count,
                             1,
                             mesh.drawParams.firstIndex,
                             mesh.drawParams.vertexOffset,
                             0);
        }
    }

    vkCmdEndRenderPass(commandBuffer);

    // Keep resources alive until GPU execution completes
    if (!geomMeshesRecord.empty())
    {
        auto resourceHolder = std::make_shared<ResourceHolderCommand>();
        resourceHolder->buffers.reserve(geomMeshesRecord.size() * 2);

        for (const auto &mesh : geomMeshesRecord)
        {
            if (mesh.indexBuffer)
            {
                resourceHolder->buffers.push_back(mesh.indexBuffer);
            }
            if (mesh.vertexBuffer)
            {
                resourceHolder->buffers.push_back(mesh.vertexBuffer);
            }
        }

        hardwareExecutor.pendingResources.push_back(resourceHolder);
    }

    // 清空记录
    geomMeshesRecord.clear();
}

VkFormat RasterizerPipelineVulkan::getVkFormatFromType(const std::string &typeName, uint32_t elementCount) const
{
    return ::getVkFormatFromType(typeName, elementCount);
}

void RasterizerPipelineVulkan::createPerPipelineUBODescriptor()
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

void RasterizerPipelineVulkan::updateUBODescriptor()
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
