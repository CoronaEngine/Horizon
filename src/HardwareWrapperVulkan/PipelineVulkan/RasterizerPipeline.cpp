#include "RasterizerPipeline.h"

#include <algorithm>
#include <cstring>
#include <span>

#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "Compiler/ShaderLanguageConverter.h"
#include "Compiler/ShaderUtils.h"

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

RasterizerPipelineVulkan::RasterizerPipelineVulkan() = default;

RasterizerPipelineVulkan::RasterizerPipelineVulkan(std::string vertexShaderCode,
                                                   std::string fragmentShaderCode,
                                                   uint32_t multiviewCount,
                                                   EmbeddedShader::ShaderLanguage /*vertexShaderLanguage*/,
                                                   EmbeddedShader::ShaderLanguage /*fragmentShaderLanguage*/,
                                                   const std::source_location &sourceLocation)
    : RasterizerPipelineVulkan()
{
    // Keep the legacy signature; Helicon's runtime compiler now consumes Slang source.
    EmbeddedShader::CompilerOption options;
    options.compileHLSL = false;
    EmbeddedShader::ShaderCodeCompiler vertexCompiler(vertexShaderCode,
                                                      EmbeddedShader::ShaderStage::VertexShader,
                                                      options,
                                                      sourceLocation);

    EmbeddedShader::ShaderCodeCompiler fragmentCompiler(fragmentShaderCode,
                                                        EmbeddedShader::ShaderStage::FragmentShader,
                                                        options,
                                                        sourceLocation);

    // 获取 SPIR-V 代码
    vertShaderCode = vertexCompiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, EmbeddedShader::Ast::Parser::getBindless());
    fragShaderCode = fragmentCompiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, EmbeddedShader::Ast::Parser::getBindless());
    initializeStageBindings(multiviewCount);
}

RasterizerPipelineVulkan::RasterizerPipelineVulkan(EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                                                   EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                                                   uint32_t multiviewCount,
                                                   const std::source_location &sourceLocation)
    : RasterizerPipelineVulkan()
{
    // 直接从已编译的 ShaderCodeCompiler 获取 SPIR-V 代码
    vertShaderCode = vertexCompiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, vertexCompiler.getCompilerOption().enableBindless, vertexCompiler.getCurrentConditionInfo());
    fragShaderCode = fragmentCompiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, fragmentCompiler.getCompilerOption().enableBindless, fragmentCompiler.getCurrentConditionInfo());
    if (vertexCompiler.getCompilerOption().horizonVulkan && fragmentCompiler.getCompilerOption().horizonVulkan)
        EmbeddedShader::linkRasterDrawParameter(vertShaderCode, fragShaderCode);
    initializeStageBindings(multiviewCount);
}

RasterizerPipelineVulkan::RasterizerPipelineVulkan(const EmbeddedShader::ShaderCodeModule &vertexSpirV,
                                                   const EmbeddedShader::ShaderCodeModule &fragmentSpirV,
                                                   uint32_t multiviewCount,
                                                   const std::source_location &sourceLocation)
    : RasterizerPipelineVulkan()
{
    vertShaderCode = vertexSpirV;
    fragShaderCode = fragmentSpirV;
    initializeStageBindings(multiviewCount);
}

void RasterizerPipelineVulkan::initializeStageBindings(uint32_t viewCount)
{
    const auto& vertex = vertShaderCode.shaderResources;
    const auto& fragment = fragShaderCode.shaderResources;
    extractStageBindings(vertex, vertexStageInputs, vertexStageOutputs);
    extractStageBindings(fragment, fragmentStageInputs, fragmentStageOutputs);
    renderTargets.resize(fragmentStageOutputs.size());
    multiviewCount = viewCount;
    uboSize = std::max(vertex.uniformBufferSize, fragment.uniformBufferSize);
    if (uboSize) tempUBO = std::make_unique<uint8_t[]>(uboSize);
}

RasterizerPipelineVulkan::GraphicsState::~GraphicsState()
{
    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
    if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
}

RasterizerPipelineVulkan::FramebufferState::~FramebufferState()
{
    if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
}

RasterizerPipelineVulkan::~RasterizerPipelineVulkan()
{
    if (const auto mainDevice = getHardwareContext().getMainDevice())
    {
        const auto device = mainDevice->deviceManager.getLogicalDevice();
        vkDeviceWaitIdle(device);
        lastFramebuffer.reset();
        graphicsStates.clear();
        if (uboDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, uboDescriptorSetLayout, nullptr);
    }
}

void RasterizerPipelineVulkan::createRenderPass(GraphicsState& state)
{
    auto& renderPass = state.renderPass;
    const auto mainDevice = getHardwareContext().getMainDevice();
    if (!mainDevice)
    {
        throw std::runtime_error("No main device available");
    }

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
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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

void RasterizerPipelineVulkan::createGraphicsPipeline(GraphicsState& state)
{
    auto& graphicsPipeline = state.pipeline;
    auto& pipelineLayout = state.layout;
    const auto renderPass = state.renderPass;
    const RasterState& raster = state.state;
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
    depthStencil.depthTestEnable = raster.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = raster.depthWrite ? VK_TRUE : VK_FALSE;
    switch (raster.compare) {
    case RasterCompare::Less: depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; break;
    case RasterCompare::LessEqual: depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
    case RasterCompare::Equal: depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL; break;
    case RasterCompare::Always: depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS; break;
    }
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 配置颜色混合（ImGui 标准 Alpha 混合）
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = raster.blend != RasterBlend::Opaque;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    if (raster.blend == RasterBlend::Additive) {
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

std::shared_ptr<RasterizerPipelineVulkan::Batch> RasterizerPipelineVulkan::capture(uint16_t width, uint16_t height, RasterPass pass)
{
    auto batch = std::make_shared<Batch>();
    batch->pipeline = shared_from_this();
    const ktm::uvec2 imageSize{width, height};
    batch->imageSize = imageSize;
    batch->state = rasterState;
    batch->pass = pass;
    batch->renderTargets = renderTargets;
    if (explicitDepthImage) batch->depthImage = depthImage;
    else if (depthImage)
    {
        const auto* depth = legacyImageFromId(depthImage.getImageID());
        if (depth->imageSize.x == imageSize.x && depth->imageSize.y == imageSize.y)
            batch->depthImage = depthImage;
    }
    for (const auto& target : renderTargets)
        batch->clearValues.push_back(target ? legacyImageFromId(target.getImageID())->clearValue : VkClearValue{});
    VkClearValue depthClear{};
    depthClear.depthStencil = {1.0f, 0};
    batch->clearValues.push_back(batch->depthImage ? legacyImageFromId(batch->depthImage.getImageID())->clearValue : depthClear);
    batch->uniformBytes = copyParameterBytes(tempUBO.get(), uboSize);
    batch->bindings = bindings.snapshot();
    return batch;
}

void RasterizerPipelineVulkan::prepareDepth(Batch& batch)
{
    if (batch.depthImage || !batch.imageSize.x || !batch.imageSize.y) return;
    HardwareImageCreateInfo info(batch.imageSize.x, batch.imageSize.y, ImageFormat::D32_FLOAT);
    info.usage = ImageUsage::DepthImage;
    info.arrayLayers = multiviewCount;
    batch.depthImage = HardwareImage(info);
    if (!explicitDepthImage) depthImage = batch.depthImage;
}

void RasterizerPipelineVulkan::prepareFramebuffer(Batch& batch)
{
    if (batch.framebuffer) return;
    prepareDepth(batch);
    if (!batch.depthImage || !batch.imageSize.x || !batch.imageSize.y)
        throw std::runtime_error("Rasterizer invocation requires a nonzero render extent");
    auto* depth = legacyImageFromId(batch.depthImage.getImageID());
    std::vector<VkFormat> formats;
    std::vector<VkImageLayout> finalLayouts;
    std::vector<VkImageView> views;
    const auto checkExtent = [&](const auto* image) {
        if (image->imageSize.x < batch.imageSize.x || image->imageSize.y < batch.imageSize.y ||
            image->arrayLayers < static_cast<uint32_t>(multiviewCount))
            throw std::runtime_error("Rasterizer attachment is smaller than the invocation extent or view count");
    };
    for (const auto& target : batch.renderTargets)
    {
        if (!target) throw std::runtime_error("Rasterizer invocation has an unbound color attachment");
        const auto* image = legacyImageFromId(target.getImageID());
        checkExtent(image);
        formats.push_back(image->imageFormat);
        finalLayouts.push_back((image->imageUsage & (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        views.push_back(image->imageView);
    }
    checkExtent(depth);
    views.push_back(depth->imageView);
    std::shared_ptr<GraphicsState> graphics;
    for (const auto& state : graphicsStates)
        if (state->colorFormats == formats && state->finalLayouts == finalLayouts &&
            state->depthFormat == depth->imageFormat && state->state == batch.state && state->pass == batch.pass)
        { graphics = state; break; }
    const auto device = getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice();
    if (!graphics)
    {
        graphics = std::make_shared<GraphicsState>();
        graphics->device = device;
        graphics->colorFormats = std::move(formats);
        graphics->finalLayouts = std::move(finalLayouts);
        graphics->depthFormat = depth->imageFormat;
        graphics->state = batch.state;
        graphics->pass = batch.pass;
        createRenderPass(*graphics);
        createGraphicsPipeline(*graphics);
        graphicsStates.push_back(graphics);
    }
    if (lastFramebuffer && lastFramebuffer->graphics == graphics &&
        lastFramebuffer->size.x == batch.imageSize.x && lastFramebuffer->size.y == batch.imageSize.y &&
        legacyImageFromId(lastFramebuffer->depth.getImageID())->imageView == depth->imageView &&
        lastFramebuffer->targets.size() == batch.renderTargets.size())
    {
        bool same = true;
        for (size_t i = 0; i < batch.renderTargets.size(); ++i)
            same &= legacyImageFromId(lastFramebuffer->targets[i].getImageID())->imageView == views[i];
        if (same) { batch.framebuffer = lastFramebuffer; return; }
    }
    auto framebuffer = std::make_shared<FramebufferState>();
    framebuffer->device = device;
    framebuffer->graphics = graphics;
    framebuffer->targets = batch.renderTargets;
    framebuffer->depth = batch.depthImage;
    framebuffer->size = batch.imageSize;
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = graphics->renderPass;
    info.attachmentCount = static_cast<uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = batch.imageSize.x;
    info.height = batch.imageSize.y;
    info.layers = 1;
    coronaHardwareCheck(vkCreateFramebuffer(device, &info, nullptr, &framebuffer->framebuffer));
    batch.framebuffer = lastFramebuffer = std::move(framebuffer);
}

void RasterizerPipelineVulkan::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    bindings.writeValue({}, {tempUBO.get(), uboSize},
                        bindType, byteOffset, data, size);
}

void RasterizerPipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    bindings.writeResource({}, {tempUBO.get(), uboSize},
                           bindType, byteOffset, typeSize, buffer);
}

void RasterizerPipelineVulkan::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location)
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
    const auto* buffer = legacyBufferFromId(handle.getBufferID());
    if (!(buffer->bufferUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
        throw std::invalid_argument("Raster buffer requires device-address usage");
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer->bufferHandle;
    return vkGetBufferDeviceAddress(buffer->device->getLogicalDevice(), &info);
}
}

void RasterizerPipelineVulkan::Batch::append(const RasterGeometry& geometry, const RasterDraw& range,
                                             const void* data, size_t size)
{
    if (externalPackets) throw std::logic_error("GPU packets already bound to this batch");
    if (!geometry.vertices || !geometry.indices) throw std::invalid_argument("Missing raster geometry");
    const auto indexBytes = geometry.indices.getElementSize();
    if ((indexBytes != 2 && indexBytes != 4) || geometry.vertices.getElementSize() % 4)
        throw std::invalid_argument("Raster geometry requires uint16/uint32 indices and a word-aligned vertex stride");
    const auto available = geometry.indices.getElementCount();
    if (range.firstIndex > available || range.indexCount > available - range.firstIndex)
        throw std::out_of_range("Raster draw exceeds index buffer");
    const auto count = range.indexCount ? range.indexCount : uint32_t(available - range.firstIndex);
    if (!count || !range.instanceCount) return;
    VkRect2D clip{{0, 0}, {imageSize.x, imageSize.y}};
    if (range.scissor) {
        const auto& rect = *range.scissor;
        auto x = std::max(0, rect.x), y = std::max(0, rect.y);
        auto right = std::min<int64_t>(imageSize.x, int64_t(rect.x) + rect.width);
        auto bottom = std::min<int64_t>(imageSize.y, int64_t(rect.y) + rect.height);
        if (right <= x || bottom <= y) return;
        clip = {{x, y}, {uint32_t(right - x), uint32_t(bottom - y)}};
    }
    const auto key = std::pair{geometry.vertices.getBufferID(), geometry.indices.getBufferID()};
    auto found = geometryIndices.find(key);
    if (found == geometryIndices.end()) {
        if (geometry.vertices.getElementCount() * (geometry.vertices.getElementSize() / 4) > UINT32_MAX)
            throw std::length_error("Raster geometry exceeds the addressable word range");
        auto inserted = geometryIndices.emplace(key, static_cast<uint32_t>(geometries.size())).first;
        try { geometries.push_back(geometry); }
        catch (...) { geometryIndices.erase(inserted); throw; }
        found = inserted;
    }
    const uint32_t geom = found->second;
    const size_t offset = (parameters.size() + 15) & ~size_t(15);
    if (size > UINT32_MAX || offset > UINT32_MAX - size || packets.size() >= UINT32_MAX / sizeof(Packet))
        throw std::length_error("Raster batch exceeds the supported upload size");
    if (size) {
        parameters.resize(offset + size);
        std::memcpy(parameters.data() + offset, data, size);
    }
    const auto first = static_cast<uint32_t>(packets.size());
    const bool newRun = runs.empty() || std::memcmp(&runs.back().scissor, &clip, sizeof(clip)) != 0;
    if (newRun) runs.push_back({clip, first, 0});
    try { packets.push_back({count, range.instanceCount, 0, range.firstInstance, geom, range.firstIndex, range.vertexOffset, uint32_t(offset / 4)}); }
    catch (...) { if (newRun) runs.pop_back(); throw; }
    ++runs.back().count;
}

void RasterizerPipelineVulkan::Batch::appendIndirect(const RasterGeometry& geometry, const HardwareBuffer& buffer,
                                                     uint32_t count, const void* data, size_t size)
{
    if (!count) return;
    if (!packets.empty() || externalPackets) throw std::logic_error("GPU packets require an empty batch");
    if (!buffer || buffer.getElementSize() != sizeof(Packet) || count > buffer.getElementCount() ||
        !(legacyBufferFromId(buffer.getBufferID())->bufferUsage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT))
        throw std::invalid_argument("Invalid indirect packet buffer or draw count");
    if (!geometry.vertices || !geometry.indices || geometry.vertices.getElementSize() % 4 ||
        (geometry.indices.getElementSize() != 2 && geometry.indices.getElementSize() != 4))
        throw std::invalid_argument("Invalid GPU raster geometry");
    if (size > UINT32_MAX) throw std::length_error("Draw parameters exceed 4 GiB");
    std::vector<uint8_t> bytes(size);
    if (size) std::memcpy(bytes.data(), data, size);
    geometries.reserve(1);
    runs.reserve(1);
    geometries.push_back(geometry);
    runs.push_back({{{0, 0}, {imageSize.x, imageSize.y}}, 0, count});
    parameters = std::move(bytes);
    externalPackets = buffer;
}

void RasterizerPipelineVulkan::Batch::prepareUpload()
{
    if ((packets.empty() && !externalPackets) || upload) return;
    const size_t geometryOffset = packets.size() * sizeof(Packet);
    const size_t parameterOffset = (geometryOffset + geometries.size() * sizeof(GeometryEntry) + 15) & ~size_t(15);
    const size_t bytes = parameterOffset + std::max<size_t>(parameters.size(), 4);
    if (bytes > UINT32_MAX) throw std::length_error("Raster batch exceeds 4 GiB");
    auto& pool = pipeline->uploadPool;
    auto free = std::find_if(pool.begin(), pool.end(), [](const auto& buffer) { return buffer.use_count() == 1; });
    if (free == pool.end() || (*free)->getElementSize() < bytes) {
        auto buffer = std::make_shared<HardwareBuffer>(uint32_t(bytes), BufferUsage::RasterIndirect);
        if (free == pool.end()) free = pool.insert(free, buffer);
        else *free = std::move(buffer);
    }
    auto buffer = *free;
    auto* mapped = static_cast<uint8_t*>(buffer->getMappedData());
    if (!mapped) throw std::runtime_error("Raster upload requires host-visible memory");
    if (geometryOffset) std::memcpy(mapped, packets.data(), geometryOffset);
    auto* entries = reinterpret_cast<GeometryEntry*>(mapped + geometryOffset);
    for (const auto& geometry : geometries)
        *entries++ = {addressOf(geometry.vertices), addressOf(geometry.indices),
                      uint32_t(geometry.vertices.getElementSize() / 4), uint32_t(geometry.indices.getElementSize()),
                      uint32_t(geometry.vertices.getElementCount())};
    if (!parameters.empty()) std::memcpy(mapped + parameterOffset, parameters.data(), parameters.size());
    auto* native = legacyBufferFromId(buffer->getBufferID());
    native->resourceManager->flushMapped(*native, bytes);
    const auto base = addressOf(*buffer);
    root = {externalPackets ? addressOf(externalPackets) : base, base + geometryOffset, base + parameterOffset, 0};
    upload = std::move(buffer);
}

void RasterizerPipelineVulkan::Batch::commitCommand(HardwareExecutorVulkan& executor)
{
    pipeline->commitCommand(executor, *this);
}

void RasterizerPipelineVulkan::Batch::resolveResourceUses(std::span<const ResourceUse> declarations, ResourceAccessList& result)
{
    pipeline->prepareDepth(*this);
    constexpr auto shaderStages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    resolveShaderBindings(result, bindings->values, shaderStages);
    prepareUpload();
    if (upload)
        result.buffer(*legacyBufferFromId(upload->getBufferID()), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | shaderStages,
                      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    if (externalPackets)
        result.buffer(*legacyBufferFromId(externalPackets.getBufferID()), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | shaderStages,
                      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    for (const auto& geometry : geometries)
    {
        for (const auto* buffer : {&geometry.vertices, &geometry.indices})
            result.buffer(*legacyBufferFromId(buffer->getBufferID()), VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }
    for (const auto& target : renderTargets)
        if (target)
        {
            auto* image = legacyImageFromId(target.getImageID());
            auto range = imageViewRange(*image);
            range.levelCount = 1;
            range.layerCount = pipeline->multiviewCount;
            result.image(*image, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, range);
        }
    if (depthImage)
    {
        auto* image = legacyImageFromId(depthImage.getImageID());
        auto range = imageViewRange(*image);
        range.levelCount = 1;
        range.layerCount = pipeline->multiviewCount;
        result.image(*image, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, range);
    }
    resolveDeclaredUses(result, declarations, shaderStages);
}

void RasterizerPipelineVulkan::commitCommand(HardwareExecutorVulkan& hardwareExecutor, Batch& batch)
{
    prepareFramebuffer(batch);
    const auto& graphics = *batch.framebuffer->graphics;
    const auto pipelineLayout = graphics.layout;
    const auto imageSize = batch.imageSize;
    const auto commandBuffer = hardwareExecutor.currentRecordQueue->commandBuffer;
    batch.bindings->prepareDescriptors();
    batch.bindings->writeParameters({}, batch.uniformBytes);
    if (!batch.uniform) batch.uniform = uniformPool.acquire(batch.uniformBytes, uboDescriptorSetLayout);
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = graphics.renderPass;
    info.framebuffer = batch.framebuffer->framebuffer;
    info.renderArea.extent = {imageSize.x, imageSize.y};
    info.clearValueCount = static_cast<uint32_t>(batch.clearValues.size());
    info.pClearValues = batch.clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0, 0, static_cast<float>(imageSize.x), static_cast<float>(imageSize.y), 0, 1};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D fullScissor{{0, 0}, {imageSize.x, imageSize.y}};
    vkCmdSetScissor(commandBuffer, 0, 1, &fullScissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipeline);
    std::array<VkDescriptorSet, 4> sets{};
    const auto& tables = getHardwareContext().getMainDevice()->resourceManager.bindlessDescriptors;
    for (size_t i = 0; i < 3; ++i)
        sets[i] = batch.bindings->sets[i] ? batch.bindings->sets[i] : tables[i].descriptorSet;
    sets[3] = batch.uniform ? batch.uniform->set : VK_NULL_HANDLE;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, sets[3] ? 4 : 3, sets.data(), 0, nullptr);
    auto& caps = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils();
    const uint32_t limit = caps.featuresChain.getChainHead()->features.multiDrawIndirect
        ? caps.supportedProperties.properties.limits.maxDrawIndirectCount : 1;
    for (const auto& run : batch.runs) {
        vkCmdSetScissor(commandBuffer, 0, 1, &run.scissor);
        for (uint32_t i = 0; i < run.count;) {
            batch.root.groupBase = run.first + i;
            const auto count = std::min(run.count - i, limit);
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(Root), &batch.root);
            const auto& commands = batch.externalPackets ? batch.externalPackets : *batch.upload;
            vkCmdDrawIndirect(commandBuffer, legacyBufferFromId(commands.getBufferID())->bufferHandle,
                              VkDeviceSize(batch.root.groupBase) * sizeof(Packet), count, sizeof(Packet));
            i += count;
        }
    }

    vkCmdEndRenderPass(commandBuffer);
    for (size_t i = 0; i < batch.renderTargets.size(); ++i)
    {
        auto* image = legacyImageFromId(batch.renderTargets[i].getImageID());
        auto range = imageViewRange(*image);
        range.levelCount = 1;
        range.layerCount = multiviewCount;
        setImageSubresourceLayout(*image, range, graphics.finalLayouts[i]);
    }
    // Batch owns draw resources, attachments and descriptor allocations until GPU completion.
}

std::shared_ptr<RasterizerPipelineVulkan> RasterizerPipelineVulkan::variant(
    EmbeddedShader::ShaderCodeCompiler& vertex, EmbeddedShader::ShaderCodeCompiler& fragment) const
{
    auto next = std::make_shared<RasterizerPipelineVulkan>(vertex, fragment, multiviewCount);
    if (next->renderTargets.size() == renderTargets.size()) next->renderTargets = renderTargets;
    next->rasterState = rasterState;
    next->depthImage = depthImage;
    next->explicitDepthImage = explicitDepthImage;
    return next;
}
