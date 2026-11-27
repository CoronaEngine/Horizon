#include "RasterPipeline.h"

#include <algorithm>
#include <atomic>
#include <span>
#include <stdexcept>

#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "Compiler/ShaderUtils.h"

namespace {
VkImageLayout colorAttachmentLayout(const ResourceManager::ImageHardwareWrap& image)
{
    // Descriptor-backed targets stay in their descriptor layout through the pass.
    // The planner orders the color write against the stages that consume it.
    return (image.imageUsage & (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
        ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

bool sameAttachment(const HardwareImage& a, const HardwareImage& b)
{
    const auto* left = ResourceManager::get(a);
    const auto* right = ResourceManager::get(b);
    if (left == right) return true;
    if (!left || !right) return false;
    // Framebuffers use only the first mip of each view.
    return left->imageHandle == right->imageHandle && left->imageFormat == right->imageFormat &&
           left->aspectMask == right->aspectMask && left->baseMipLevel == right->baseMipLevel &&
           left->baseArrayLayer == right->baseArrayLayer && left->arrayLayers == right->arrayLayers;
}
}

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

RasterPipelineVulkan::RasterPipelineVulkan(EmbeddedShader::ShaderCodeModule vertexSpirV,
                                           EmbeddedShader::ShaderCodeModule fragmentSpirV, RasterState state, uint32_t viewCount)
    : rasterState(state), vertShaderCode(std::move(vertexSpirV)), fragShaderCode(std::move(fragmentSpirV))
{
    initializeStageBindings(viewCount);
}

RasterPipelineVulkan::RasterPipelineVulkan(const EmbeddedShader::RasterPipelineObject& shaders,
                                           const ConditionInfo& vertexConditions, const ConditionInfo& fragmentConditions,
                                           RasterState state, uint32_t viewCount)
    : rasterState(state),
      vertShaderCode(shaders.vertex->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, vertexConditions)),
      fragShaderCode(shaders.fragment->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, fragmentConditions))
{
    EmbeddedShader::linkRasterDrawParameter(vertShaderCode, fragShaderCode);
    initializeStageBindings(viewCount);
}

void RasterPipelineVulkan::initializeStageBindings(uint32_t viewCount)
{
    if (viewCount == 0 || viewCount > 32) throw std::invalid_argument("Invalid multiview count");
    const auto& vertex = vertShaderCode.shaderResources;
    const auto& fragment = fragShaderCode.shaderResources;
    extractStageBindings(vertex, vertexStageInputs, vertexStageOutputs);
    extractStageBindings(fragment, fragmentStageInputs, fragmentStageOutputs);
    renderTargets.resize(fragmentStageOutputs.size());
    mergeWithoutUses = EmbeddedShader::rasterShaderCanMergeWithoutUses(vertShaderCode) &&
                       EmbeddedShader::rasterShaderCanMergeWithoutUses(fragShaderCode);
    multiviewCount = viewCount;
    uboSize = std::max(vertex.uniformBufferSize, fragment.uniformBufferSize);
    if (uboSize) tempUBO = std::make_unique<uint8_t[]>(uboSize);
}

RasterPipelineVulkan::GraphicsState::~GraphicsState()
{
    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
    if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
}

RasterPipelineVulkan::FramebufferState::~FramebufferState()
{
    if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
}

RasterPipelineVulkan::~RasterPipelineVulkan()
{
    if (uboDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice(),
                                     uboDescriptorSetLayout, nullptr);
}

void RasterPipelineVulkan::createRenderPass(GraphicsState& state)
{
    auto& renderPass = state.renderPass;
    const auto mainDevice = getHardwareContext().getMainDevice();
    if (!mainDevice)
    {
        throw std::runtime_error("No main device available");
    }

    const auto& features = mainDevice->deviceManager.getFeaturesUtils();
    if (multiviewCount > 1 && (!features.featuresChain.supportsMultiview() ||
                              multiviewCount > features.multiviewProperties.maxMultiviewViewCount))
        throw std::invalid_argument("Multiview count exceeds the enabled device capabilities");

    // 配置附件描述
    std::vector<VkAttachmentDescription> attachments;
    attachments.reserve(state.colorFormats.size() + 1);

    for (size_t i = 0; i < state.colorFormats.size(); ++i)
    {
        VkAttachmentDescription attachment{};
        attachment.format = state.colorFormats[i];
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = state.pass.loadColor ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = state.finalLayouts[i];
        attachment.finalLayout = state.finalLayouts[i];
        attachments.push_back(attachment);
    }

    // 深度附件
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = state.depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = state.pass.loadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments.push_back(depthAttachment);

    // 配置颜色附件引用
    std::vector<VkAttachmentReference> colorReferences;
    colorReferences.reserve(state.colorFormats.size());
    for (uint32_t i = 0; i < state.colorFormats.size(); ++i)
    {
        colorReferences.push_back({i, state.finalLayouts[i]});
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
    const uint32_t viewMask = UINT32_MAX >> (32 - multiviewCount);
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
    renderPassInfo.pNext = multiviewCount > 1 ? &multiviewCreateInfo : nullptr;

    coronaHardwareCheck(vkCreateRenderPass(mainDevice->deviceManager.getLogicalDevice(), &renderPassInfo, nullptr, &renderPass));
}

void RasterPipelineVulkan::createGraphicsPipeline(GraphicsState& state)
{
    auto& graphicsPipeline = state.pipeline;
    auto& pipelineLayout = state.layout;
    const auto renderPass = state.renderPass;
    const auto mainDevice = getHardwareContext().getMainDevice();
    if (!mainDevice)
    {
        throw std::runtime_error("No main device available");
    }

    const VkDevice device = mainDevice->deviceManager.getLogicalDevice();

    if (!mainDevice->deviceManager.getFeaturesUtils().featuresChain.supportsPulledRaster())
        throw std::runtime_error("Legacy raster requires bufferDeviceAddress and shaderDrawParameters");
    if (!vertexStageInputs.empty())
        throw std::invalid_argument("Legacy vertex shaders must use BDA vertex pulling");
    for (const auto* stage : {&vertShaderCode, &fragShaderCode}) {
        const auto& reflection = stage->shaderResources;
        if (reflection.pushConstantSize > sizeof(Root))
            throw std::invalid_argument("Raster shader push constants exceed the indirect draw root size");
    }
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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    // 配置输入装配
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = rasterState.topology == RasterTopology::TriangleStrip
        ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 配置视口状态
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // 配置光栅化
    VkPipelineRasterizationStateCreateInfo rasterInfo{};
    rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterInfo.depthClampEnable = VK_FALSE;
    rasterInfo.rasterizerDiscardEnable = VK_FALSE;
    rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterInfo.lineWidth = 1.0f;
    rasterInfo.cullMode = VK_CULL_MODE_NONE;
    rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterInfo.depthBiasEnable = VK_FALSE;

    // 配置多重采样
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 配置深度模板
    // RasterPipeline 默认用于 3D 几何绘制，因此开启深度测试与深度写入。
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = rasterState.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = rasterState.depthWrite ? VK_TRUE : VK_FALSE;
    switch (rasterState.compare) {
    case RasterCompare::Less: depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; break;
    case RasterCompare::LessEqual: depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
    case RasterCompare::Equal: depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL; break;
    case RasterCompare::Always: depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS; break;
    }
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 配置颜色混合
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = rasterState.blend != RasterBlend::Opaque;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    if (rasterState.blend == RasterBlend::Additive) {
        colorBlendAttachment.srcColorBlendFactor = colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(state.colorFormats.size(), colorBlendAttachment);

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
    pushConstantRange.size = sizeof(Root);

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
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    coronaHardwareCheck(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    // 创建图形管线
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterInfo;
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

HardwareExecutor::Command::Impl::Draw::Draw(std::shared_ptr<RasterPipelineVulkan> backend, std::optional<ktm::uvec2> extent,
                                  std::optional<RasterPass> requestedPass, const std::vector<ResourceUse>& uses)
    : pipeline(std::move(backend)), pass(requestedPass.value_or(RasterPass{})),
      startsPass(requestedPass.has_value()),
      renderTargets(pipeline->renderTargets), resourceUses(uses)
{
    if (!extent) {
        const auto color = std::find_if(renderTargets.begin(), renderTargets.end(), [](const HardwareImage& image) { return bool(image); });
        const HardwareImage& target = color != renderTargets.end() ? *color : pipeline->depthImage;
        if (!target || (color == renderTargets.end() && !pipeline->explicitDepthImage))
            throw std::invalid_argument("An inferred raster extent requires a bound color or depth image");
        extent = ResourceManager::get(target)->imageSize;
    }
    imageSize = *extent;
    if (!imageSize.x || !imageSize.y) throw std::invalid_argument("Raster draw requires a nonzero extent");
    if (!pipeline->explicitDepthImage) {
        const auto* depth = ResourceManager::get(pipeline->depthImage);
        if (!depth || depth->imageSize != imageSize) {
            HardwareImage::CreateInfo info(imageSize.x, imageSize.y, HardwareImage::Format::D32_FLOAT);
            info.usage = HardwareImage::Usage::DepthImage;
            info.arrayLayers = pipeline->multiviewCount;
            pipeline->depthImage = HardwareImage(info);
        }
    }
    depthImage = pipeline->depthImage;
    clearValues.reserve(renderTargets.size() + 1);
    for (const auto& target : renderTargets)
        clearValues.push_back(target ? ResourceManager::get(target)->clearValue : VkClearValue{});
    clearValues.push_back(ResourceManager::get(depthImage)->clearValue);
    uniformBytes = copyParameterBytes(pipeline->tempUBO.get(), pipeline->uboSize);
    bindings = pipeline->bindings.snapshot();
}

bool HardwareExecutor::Command::Impl::Draw::canMerge(const Draw& next) const
{
    if (pipeline != next.pipeline || uniformBytes != next.uniformBytes)
        return false;
    if (bindings != next.bindings && !std::ranges::equal(bindings->values, next.bindings->values,
        [](const PipelineBinding& a, const PipelineBinding& b) {
            return a.offset == b.offset && a.size == b.size && a.uniform == b.uniform &&
                   a.fixedSlot == b.fixedSlot && a.sampled == b.sampled && sameResource(a.resource, b.resource);
        })) return false;

    // With equal read-only shader resources, each group's geometry, packets and parameters
    // add only buffer reads. Storage writes and opaque bindless accesses need a pass boundary.
    if (!pipeline->mergeWithoutUses && resourceUses.empty()) return false;
    if (!std::ranges::equal(resourceUses, next.resourceUses, [](const ResourceUse& a, const ResourceUse& b) {
        return a.access == ResourceAccess::Read && b.access == ResourceAccess::Read && sameResource(a.resource, b.resource);
    })) return false;

    const auto readsAttachment = [&](const PipelineResource& resource) {
        const auto* image = std::get_if<HardwareImage>(&resource);
        if (!image || !*image) return false;
        const auto handle = ResourceManager::get(*image)->imageHandle;
        const auto aliases = [handle](const HardwareImage& attachment) {
            return attachment && ResourceManager::get(attachment)->imageHandle == handle;
        };
        return aliases(depthImage) || std::ranges::any_of(renderTargets, aliases);
    };
    for (const auto& use : resourceUses)
        if (!std::visit([](const auto& resource) { return bool(resource); }, use.resource) || readsAttachment(use.resource))
            return false;
    for (const auto& binding : bindings->values) {
        if (readsAttachment(binding.resource)) return false;
        if (pipeline->mergeWithoutUses || binding.sampled) continue;
        if (const auto* image = std::get_if<HardwareImage>(&binding.resource);
            image && *image && !(ResourceManager::get(*image)->imageUsage & VK_IMAGE_USAGE_STORAGE_BIT)) continue;
        if (!std::ranges::any_of(resourceUses, [&](const ResourceUse& use) {
                return sameResource(binding.resource, use.resource);
            })) return false;
    }
    return true;
}

bool HardwareExecutor::Command::Impl::Draw::sameAttachments(const Draw& next) const
{
    return imageSize == next.imageSize && pipeline->multiviewCount == next.pipeline->multiviewCount &&
           sameAttachment(depthImage, next.depthImage) &&
           std::ranges::equal(renderTargets, next.renderTargets, sameAttachment);
}

void HardwareExecutor::Command::Impl::Draw::prepareFramebuffer()
{
    if (framebuffer) return;
    auto& lastFramebuffer = pipeline->lastFramebuffer;
    if (lastFramebuffer && lastFramebuffer->graphics->pass == pass &&
        lastFramebuffer->size.x == imageSize.x && lastFramebuffer->size.y == imageSize.y &&
        sameAttachment(lastFramebuffer->depth, depthImage) &&
        std::equal(lastFramebuffer->targets.begin(), lastFramebuffer->targets.end(),
                   renderTargets.begin(), renderTargets.end(), sameAttachment)) {
        framebuffer = lastFramebuffer;
        return;
    }
    auto* depth = ResourceManager::get(depthImage);
    std::vector<VkFormat> formats;
    std::vector<VkImageLayout> finalLayouts;
    std::vector<VkImageView> views;
    const auto checkExtent = [&](const auto* image) {
        if (image->imageSize.x < imageSize.x || image->imageSize.y < imageSize.y ||
            image->arrayLayers < static_cast<uint32_t>(pipeline->multiviewCount))
            throw std::runtime_error("Raster attachment is smaller than the invocation extent or view count");
    };
    const auto attachmentView = [](auto* image) {
        if (image->mipLevels == 1 && !image->cube) return image->imageView;
        return image->resourceManager->createImageView(*image, UINT32_MAX, 0);
    };
    for (const auto& target : renderTargets)
    {
        if (!target) throw std::runtime_error("Raster invocation has an unbound color attachment");
        auto* image = ResourceManager::get(target);
        checkExtent(image);
        formats.push_back(image->imageFormat);
        finalLayouts.push_back(colorAttachmentLayout(*image));
        views.push_back(attachmentView(image));
    }
    checkExtent(depth);
    views.push_back(attachmentView(depth));
    std::shared_ptr<RasterPipelineVulkan::GraphicsState> graphics;
    for (const auto& cached : pipeline->graphicsStates)
        if (cached->colorFormats == formats && cached->finalLayouts == finalLayouts &&
            cached->depthFormat == depth->imageFormat && cached->pass == pass)
        { graphics = cached; break; }
    const auto device = getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice();
    if (!graphics)
    {
        graphics = std::make_shared<RasterPipelineVulkan::GraphicsState>();
        graphics->device = device;
        graphics->colorFormats = std::move(formats);
        graphics->finalLayouts = std::move(finalLayouts);
        graphics->depthFormat = depth->imageFormat;
        graphics->pass = pass;
        pipeline->createRenderPass(*graphics);
        pipeline->createGraphicsPipeline(*graphics);
        pipeline->graphicsStates.push_back(graphics);
    }
    auto next = std::make_shared<RasterPipelineVulkan::FramebufferState>();
    next->device = device;
    next->graphics = graphics;
    next->targets = renderTargets;
    next->depth = depthImage;
    next->size = imageSize;
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = graphics->renderPass;
    info.attachmentCount = static_cast<uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = imageSize.x;
    info.height = imageSize.y;
    info.layers = 1;
    coronaHardwareCheck(vkCreateFramebuffer(device, &info, nullptr, &next->framebuffer));
    framebuffer = lastFramebuffer = std::move(next);
}

void RasterPipelineVulkan::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    bindings.writeValue({}, {tempUBO.get(), uboSize},
                        bindType, byteOffset, data, size);
}

void RasterPipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    bindings.writeResource({}, {tempUBO.get(), uboSize},
                           bindType, byteOffset, typeSize, buffer);
}

void RasterPipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location)
{
    if (bindType == EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs)
    {
        if (location < renderTargets.size()) renderTargets[location] = image;
        return;
    }
    bindings.writeResource({}, {tempUBO.get(), uboSize},
                           bindType, byteOffset, typeSize, image, location);
}

namespace {
VkDeviceAddress addressOf(const HardwareBuffer& handle)
{
    auto* buffer = ResourceManager::get(handle);
    std::atomic_ref cached(buffer->deviceAddress);
    if (auto address = cached.load(std::memory_order_relaxed)) return address;
    if (!(buffer->bufferUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
        throw std::invalid_argument("Raster buffer requires device-address usage");
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer->bufferHandle;
    const auto address = vkGetBufferDeviceAddress(buffer->device->getLogicalDevice(), &info);
    cached.store(address, std::memory_order_relaxed);
    return address;
}
}

void HardwareExecutor::Command::Impl::Draw::appendIndirect(const HardwareBuffer& vertices, const HardwareBuffer& indices,
                                                const HardwareBuffer& packets, uint32_t count, const HardwareBuffer& parameters,
                                                std::optional<ScissorRect> scissor)
{
    if (!count) return;
    if (!packets || packets.getElementSize() != sizeof(RasterIndirectPacket) || count > packets.getElementCount() ||
        !(ResourceManager::get(packets)->bufferUsage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT))
        throw std::invalid_argument("Invalid indirect packet buffer or draw count");
    if (!vertices || !indices || vertices.getElementSize() % 4 ||
        (indices.getElementSize() != 2 && indices.getElementSize() != 4))
        throw std::invalid_argument("Invalid GPU raster geometry");
    VkRect2D clip{{0, 0}, {imageSize.x, imageSize.y}};
    if (scissor) {
        const auto x = std::max(0, scissor->x);
        const auto y = std::max(0, scissor->y);
        const auto right = std::min<int64_t>(imageSize.x, int64_t(scissor->x) + scissor->width);
        const auto bottom = std::min<int64_t>(imageSize.y, int64_t(scissor->y) + scissor->height);
        if (right <= x || bottom <= y) return;
        clip = {{x, y}, {uint32_t(right - x), uint32_t(bottom - y)}};
    }
    IndirectGroup group{vertices, indices, packets, parameters, {}, clip, count, {}};
    auto& pool = pipeline->uploadPool;
    {
        std::lock_guard lock(pipeline->uploadMutex);
        auto free = std::find_if(pool.begin(), pool.end(), [](const auto& buffer) { return buffer.use_count() == 1; });
        if (free == pool.end()) {
            group.geometryUpload = pool.emplace_back(std::make_shared<HardwareBuffer>(HardwareBuffer::rasterUpload(sizeof(GeometryEntry))));
        } else {
            group.geometryUpload = *free;
        }
    }

    const GeometryEntry geometry{addressOf(vertices), addressOf(indices), uint32_t(vertices.getElementSize() / 4),
                                 uint32_t(indices.getElementSize()), uint32_t(vertices.getElementCount())};
    if (!group.geometryUpload->copyFromData(&geometry, sizeof(geometry)))
        throw std::runtime_error("Raster geometry upload requires host-visible memory");
    const auto geometryAddress = addressOf(*group.geometryUpload);
    // Use the reserved zero word as a valid base for shaders with no per-draw parameters.
    const auto parameterAddress = parameters ? addressOf(parameters) : geometryAddress + offsetof(GeometryEntry, reserved);
    group.root = {addressOf(packets), geometryAddress, parameterAddress, 0, 0};
    groups.push_back(std::move(group));
    drawCount = count;
}

void HardwareExecutor::Command::Impl::Draw::resolveResourceUses(std::span<const ResourceUse> declarations, ResourceAccessList& result)
{
    constexpr auto shaderStages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    resolveShaderBindings(result, bindings->values, shaderStages);
    for (const auto& group : groups) {
        result.buffer(*ResourceManager::get(group.packets), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | shaderStages,
                      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        result.buffer(*ResourceManager::get(*group.geometryUpload), shaderStages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        if (group.parameters)
            result.buffer(*ResourceManager::get(group.parameters), shaderStages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        for (const auto* buffer : {&group.vertices, &group.indices})
            result.buffer(*ResourceManager::get(*buffer), VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }
    for (const auto& target : renderTargets)
        if (target)
        {
            auto* image = ResourceManager::get(target);
            auto range = imageViewRange(*image);
            range.levelCount = 1;
            range.layerCount = pipeline->multiviewCount;
            result.image(*image, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         colorAttachmentLayout(*image), range);
        }
    if (depthImage)
    {
        auto* image = ResourceManager::get(depthImage);
        auto range = imageViewRange(*image);
        range.levelCount = 1;
        range.layerCount = pipeline->multiviewCount;
        result.image(*image, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, range);
    }
    if (declarations.empty()) {
        resolveDeclaredUses(result, resourceUses, shaderStages);
    } else {
        auto uses = resourceUses;
        uses.insert(uses.end(), declarations.begin(), declarations.end());
        resolveDeclaredUses(result, uses, shaderStages);
    }
}

void HardwareExecutor::Command::Impl::Draw::commitCommand(HardwareExecutorVulkan& hardwareExecutor)
{
    prepareFramebuffer();
    const auto& graphics = *framebuffer->graphics;
    const auto pipelineLayout = graphics.layout;
    const auto commandBuffer = hardwareExecutor.currentRecordQueue->commandBuffer;
    bindings->prepareDescriptors();
    bindings->writeParameters({}, uniformBytes);
    if (!uniform) uniform = pipeline->uniformPool.acquire(uniformBytes, pipeline->uboDescriptorSetLayout);
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = graphics.renderPass;
    info.framebuffer = framebuffer->framebuffer;
    info.renderArea.extent = {imageSize.x, imageSize.y};
    info.clearValueCount = static_cast<uint32_t>(clearValues.size());
    info.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    ++hardwareExecutor.recordedRasterPassCount;
    VkViewport viewport{0, 0, static_cast<float>(imageSize.x), static_cast<float>(imageSize.y), 0, 1};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipeline);
    std::array<VkDescriptorSet, 4> sets{};
    const auto& tables = getHardwareContext().getMainDevice()->resourceManager.bindlessDescriptors;
    for (size_t i = 0; i < 3; ++i)
        sets[i] = bindings->sets[i] ? bindings->sets[i] : tables[i].descriptorSet;
    sets[3] = uniform ? uniform->set : VK_NULL_HANDLE;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, sets[3] ? 4 : 3, sets.data(), 0, nullptr);
    auto& caps = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils();
    const uint32_t limit = caps.featuresChain.getChainHead()->features.multiDrawIndirect
        ? caps.supportedProperties.properties.limits.maxDrawIndirectCount : 1;
    for (const auto& group : groups) {
        vkCmdSetScissor(commandBuffer, 0, 1, &group.scissor);
        const VkBuffer commands = ResourceManager::get(group.packets)->bufferHandle;
        auto root = group.root;
        for (uint32_t first = 0; first < group.count;) {
            const auto count = std::min(group.count - first, limit);
            root.groupBase = first;
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(root), &root);
            vkCmdDrawIndirect(commandBuffer, commands, VkDeviceSize(first) * sizeof(RasterIndirectPacket),
                              count, sizeof(RasterIndirectPacket));
            first += count;
        }
    }

    vkCmdEndRenderPass(commandBuffer);
    for (size_t i = 0; i < renderTargets.size(); ++i)
    {
        auto* image = ResourceManager::get(renderTargets[i]);
        auto range = imageViewRange(*image);
        range.levelCount = 1;
        range.layerCount = pipeline->multiviewCount;
        setImageSubresourceLayout(*image, range, graphics.finalLayouts[i]);
    }
}

std::shared_ptr<RasterPipelineVulkan> RasterPipelineVulkan::variant(
    const EmbeddedShader::RasterPipelineObject& shaders, const ConditionInfo& vertexConditions,
    const ConditionInfo& fragmentConditions) const
{
    auto next = std::make_shared<RasterPipelineVulkan>(shaders, vertexConditions, fragmentConditions, rasterState, multiviewCount);
    if (next->renderTargets.size() == renderTargets.size()) next->renderTargets = renderTargets;
    next->depthImage = depthImage;
    next->explicitDepthImage = explicitDepthImage;
    return next;
}

template<class VS, class FS>
struct RasterPipeline<VS, FS>::Impl
{
    std::shared_ptr<RasterPipelineVulkan> backend;
    std::unique_ptr<EmbeddedShader::RasterPipelineObject> variants;
    EmbeddedShader::ShaderCodeCompiler::ConditionInfo vertexConditions, fragmentConditions;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries;
    size_t explicitTargetCount{}; // The last entries override reflected outputs.
    std::vector<ResourceUse> resourceUses;

    static Impl& checked(const std::unique_ptr<Impl>& impl)
    {
        if (!impl || !impl->backend) throw std::logic_error("Empty raster pipeline handle");
        return *impl;
    }

    void refreshShaderVariant();
};

template<class VS, class FS>
RasterPipeline<VS, FS>::RasterPipeline() = default;

template<class VS, class FS>
RasterPipeline<VS, FS>::RasterPipeline(EmbeddedShader::VertexEntry vertex, EmbeddedShader::FragmentEntry fragment,
                                       RasterState state, EmbeddedShader::CompilerOption options, uint32_t views,
                                       std::source_location location)
    : impl_(std::make_unique<Impl>())
{
    if (vertex.module || fragment.module) {
        if (!vertex.module || !fragment.module)
            throw std::invalid_argument("Raster pipeline requires two EDSL stages or two precompiled stages");
        impl_->backend = std::make_shared<RasterPipelineVulkan>(*vertex.module, *fragment.module, state, views);
        return;
    }

    auto shaders = EmbeddedShader::RasterPipelineObject::compile(
        std::move(vertex), std::move(fragment), std::move(options), location);
    impl_->vertexConditions = shaders.vertex->getCurrentConditionInfo();
    impl_->fragmentConditions = shaders.fragment->getCurrentConditionInfo();
    impl_->backend = std::make_shared<RasterPipelineVulkan>(shaders, impl_->vertexConditions, impl_->fragmentConditions, state, views);
    impl_->autoBindEntries = std::move(shaders.autoBindEntries);
    if (!impl_->vertexConditions.empty() || !impl_->fragmentConditions.empty())
        impl_->variants = std::make_unique<EmbeddedShader::RasterPipelineObject>(std::move(shaders));
}

template<class VS, class FS>
RasterPipeline<VS, FS>::~RasterPipeline() = default;

template<class VS, class FS>
RasterPipeline<VS, FS>::RasterPipeline(RasterPipeline&&) noexcept = default;

template<class VS, class FS>
RasterPipeline<VS, FS>& RasterPipeline<VS, FS>::operator=(RasterPipeline&&) noexcept = default;

template<class VS, class FS>
RasterPipeline<VS, FS>& RasterPipeline<VS, FS>::uses(std::initializer_list<ResourceUse> resources)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->resourceUses.assign(resources);
    return *this;
}

template<class VS, class FS>
RasterPipeline<VS, FS>& RasterPipeline<VS, FS>::bindRenderTarget(uint32_t location, void** resource)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->autoBindEntries.push_back({resource, 0, 0,
        static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs), location});
    ++impl_->explicitTargetCount;
    return *this;
}

template<class VS, class FS>
void RasterPipeline<VS, FS>::setDepthImage(HardwareImage& depthImage)
{
    Impl::checked(impl_).backend->setDepthImage(depthImage);
}

template<class VS, class FS>
HardwareImage RasterPipeline<VS, FS>::getDepthImage()
{
    return Impl::checked(impl_).backend->getDepthImage();
}

template<class VS, class FS>
HardwareExecutor::Command RasterPipeline<VS, FS>::operator()(
    const HardwareBuffer& vertices, const HardwareBuffer& indices, const HardwareBuffer& packets, uint32_t count,
    const HardwareBuffer& parameters, std::optional<ScissorRect> scissor,
    std::optional<RasterPass> pass, std::optional<ktm::uvec2> extent)
{
    if (!count && !pass) return HardwareExecutor::Command(nullptr);
    auto& pipeline = Impl::checked(impl_);
    if (pipeline.variants) pipeline.refreshShaderVariant();
    auto& backend = *pipeline.backend;
    // Auto-bind: read current resource from each EDSL proxy's back-pointer
    for (const auto& entry : pipeline.autoBindEntries)
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
                backend.setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType, entry.location);
        }
    }
    auto draw = std::make_shared<HardwareExecutor::Command::Impl::Draw>(pipeline.backend, extent, pass, pipeline.resourceUses);
    draw->appendIndirect(vertices, indices, packets, count, parameters, scissor);
    if (!draw->drawCount && !pass) return HardwareExecutor::Command(nullptr);
    return HardwareExecutor::Command(std::move(draw));
}

template<class VS, class FS>
void RasterPipeline<VS, FS>::Impl::refreshShaderVariant()
{
    auto vertex = variants->vertex->getCurrentConditionInfo();
    auto fragment = variants->fragment->getCurrentConditionInfo();
    if (vertex == vertexConditions && fragment == fragmentConditions) return;
    auto next = backend->variant(*variants, vertex, fragment);
    variants->updateAutoBind(vertex, fragment);
    auto& bindings = variants->autoBindEntries;
    bindings.insert(bindings.end(), autoBindEntries.end() - explicitTargetCount, autoBindEntries.end());
    autoBindEntries = std::move(bindings);
    backend = std::move(next);
    vertexConditions = std::move(vertex);
    fragmentConditions = std::move(fragment);
}

template struct RasterPipeline<void, void>;
template struct EmbeddedShader::BoundField<RasterPipeline<>>;
