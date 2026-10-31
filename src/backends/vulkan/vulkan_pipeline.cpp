
#include "vulkan_device.h"
#include "vulkan_pipeline.h"
#include "util.h"
#include "vulkan_shader.h"
#include "vulkan_vertex_buffer.h"
#include "vulkan_driver.h"
#include "vulkan_descriptorset.h"

namespace ocarina {

void VulkanPipelineManager::bind_shader(VkShaderModule shader, int stage) {
    pipeline_key_cache_.shaders[stage] = shader;
}

void VulkanPipelineManager::bind_raster_state(const RasterState& raster_state)
{
    pipeline_key_cache_.raster_state = raster_state;
}

void VulkanPipelineManager::bind_blend_state(const BlendState &blend_state) {
    pipeline_key_cache_.blend_state = blend_state;
}

void VulkanPipelineManager::bind_depth_stencil_state(const DepthStencilState& depth_stencil_state)
{
    pipeline_key_cache_.depth_stencil_state = depth_stencil_state;
}

void VulkanPipelineManager::bind_pipeline_layout(VkPipelineLayout pipeline_layout) {
    pipeline_key_cache_.pipeline_layout = pipeline_layout;
}

void VulkanPipelineManager::bind_vertex_attributes(VkVertexInputAttributeDescription const* attributes,
                                                   VkVertexInputBindingDescription const *binds, uint8_t attr_count, uint8_t bind_desc_count) {
    for (size_t i = 0; i < PipelineKey::MAX_VERTEX_ATTRIBUTES; i++) {
        if (i < attr_count) {
            pipeline_key_cache_.vertex_input_attributes[i] = attributes[i];
        } 
        else {
            pipeline_key_cache_.vertex_input_attributes[i] = {};
        }
    }

    for (size_t i = 0; i < PipelineKey::MAX_VERTEX_ATTRIBUTES; i++) {
        if (i < bind_desc_count) {
            pipeline_key_cache_.vertex_input_binding[i] = binds[i];
        } else {
            pipeline_key_cache_.vertex_input_binding[i] = {};
        }
    }
}

void VulkanPipelineManager::bind_topology(PrimitiveType primitive_type) {
    pipeline_key_cache_.topology = get_vulkan_topology(primitive_type);
}

VulkanPipeline* VulkanPipelineManager::get_or_create_pipeline(const PipelineState &pipeline_state, VulkanDevice *device, VkRenderPass render_pass) {
    for (int i = 0; i < PipelineKey::MAX_SHADER_STAGE; ++i) {
        VulkanShader *shader = reinterpret_cast<VulkanShader *>(pipeline_state.shaders[i]);
        bind_shader(shader->shader_module(), i);
    }
    bind_blend_state(pipeline_state.blend_state);
    bind_depth_stencil_state(pipeline_state.depth_stencil_state);
    bind_raster_state(pipeline_state.raster_state);

    VulkanShader *vertex_shader = reinterpret_cast<VulkanShader*>(pipeline_state.shaders[0]);//VulkanDriver::instance().get_shader(pipeline_state.shaders[0]);
    VulkanShader *pixel_shader = reinterpret_cast<VulkanShader *>(pipeline_state.shaders[1]); //VulkanDriver::instance().get_shader(pipeline_state.shaders[1]);
    VulkanShader *compute_shader = reinterpret_cast<VulkanShader *>(pipeline_state.shaders[2]);//VulkanDriver::instance().get_shader(pipeline_state.shaders[2]);

    VulkanVertexBuffer *vertex_buffer = static_cast<VulkanVertexBuffer *>(pipeline_state.vertex_buffer);
    VulkanVertexStreamBinding *vertex_binding = nullptr;
    if (vertex_buffer) {
        vertex_binding = vertex_buffer->get_or_create_vertex_binding(vertex_shader);

        bind_vertex_attributes(vertex_binding->attribute_descriptions_.data(), vertex_binding->binding_descriptions_.data(),
                               vertex_binding->attribute_descriptions_.size(), vertex_binding->binding_descriptions_.size());

        if (vertex_buffer->is_dirty()) {
            vertex_buffer->upload_data();
        }
    }
    bind_topology(pipeline_state.primitive_type);
    bind_render_pass(render_pass);

    VkDescriptorSetLayout vk_descriptor_set_layouts[MAX_DESCRIPTOR_SETS_PER_SHADER] = {};
    uint32_t layouts_count = 0;
    VulkanShader *shaders[2] = { vertex_shader, pixel_shader };
    std::array<DescriptorSetLayout *, MAX_DESCRIPTOR_SETS_PER_SHADER> descriptor_set_layouts = VulkanDriver::instance().create_descriptor_set_layout(shaders, 2);
    for (uint32_t i = 0; i < MAX_DESCRIPTOR_SETS_PER_SHADER; ++i) {
        if (descriptor_set_layouts[i]) {
            vk_descriptor_set_layouts[i] = static_cast<VulkanDescriptorSetLayout*>(descriptor_set_layouts[i])->layout();
            layouts_count++;
        } else {
            vk_descriptor_set_layouts[i] = VK_NULL_HANDLE;
        }
    }

    uint32_t push_constant_size = vertex_shader->get_push_constant_size();

    pipeline_key_cache_.pipeline_layout = VulkanDriver::instance().get_pipeline_layout(vk_descriptor_set_layouts, layouts_count, push_constant_size);
    for (uint32_t i = 0; i < layouts_count; ++i) {
        if (descriptor_set_layouts[i]) {
            VulkanDescriptorSetLayout* vk_descriptro_set_layout = static_cast<VulkanDescriptorSetLayout *>(descriptor_set_layouts[i]);
            if (vk_descriptro_set_layout->is_global_ubo())
            {
                //global_descriptor_set_writers_->in
            }
        } 
    }
    

    auto it = vulkan_pipelines_.find(pipeline_key_cache_);
    if (it != vulkan_pipelines_.end()) {
        return it->second;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
    vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount = vertex_binding->binding_descriptions_.size();
    vertex_input_state.pVertexBindingDescriptions = vertex_binding->binding_descriptions_.data();
    vertex_input_state.vertexAttributeDescriptionCount = vertex_binding->attribute_descriptions_.size();
    vertex_input_state.pVertexAttributeDescriptions = vertex_binding->attribute_descriptions_.data();

    VkPipelineShaderStageCreateInfo shaderStages[2];
    shaderStages[0] = VkPipelineShaderStageCreateInfo{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].pName = vertex_shader->get_entry_point();
    shaderStages[0].module = pipeline_key_cache_.shaders[0];

    shaderStages[1] = VkPipelineShaderStageCreateInfo{};
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].pName = vertex_shader->get_entry_point();
    shaderStages[1].module = pipeline_key_cache_.shaders[1];

    VkPipelineMultisampleStateCreateInfo multi_sample_state = {};
    multi_sample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multi_sample_state.alphaToCoverageEnable = pipeline_key_cache_.multi_sample_state.alpha_to_coverage_enable ? VK_TRUE : VK_FALSE;
    multi_sample_state.alphaToOneEnable = pipeline_key_cache_.multi_sample_state.alpha_to_one_enable ? VK_TRUE : VK_FALSE;
    multi_sample_state.sampleShadingEnable = pipeline_key_cache_.multi_sample_state.sample_shading_enable ? VK_TRUE : VK_FALSE;
    multi_sample_state.minSampleShading = 1.0f;
    multi_sample_state.pSampleMask = nullptr;
    multi_sample_state.rasterizationSamples = get_vulkan_sample_count_flag_bit(pipeline_key_cache_.multi_sample_state.sample_count);
    multi_sample_state.flags = 0;

    VkPipelineColorBlendAttachmentState blend_attachment_state = {};
    blend_attachment_state.blendEnable = pipeline_key_cache_.blend_state.blend_enable ? VK_TRUE : VK_FALSE;
    blend_attachment_state.colorWriteMask = get_vulkan_color_component_flag_bits(pipeline_key_cache_.blend_state.color_mask);
    blend_attachment_state.alphaBlendOp = get_vulkan_blend_op(pipeline_key_cache_.blend_state.alphaBlendOp);
    blend_attachment_state.colorBlendOp = get_vulkan_blend_op(pipeline_key_cache_.blend_state.colorBlendOp);
    blend_attachment_state.srcAlphaBlendFactor = get_vulkan_blend_factor(pipeline_key_cache_.blend_state.srcalphablend_factor);
    blend_attachment_state.dstAlphaBlendFactor = get_vulkan_blend_factor(pipeline_key_cache_.blend_state.dstalphablend_factor);
    blend_attachment_state.srcColorBlendFactor = get_vulkan_blend_factor(pipeline_key_cache_.blend_state.srccolorblend_factor);
    blend_attachment_state.dstColorBlendFactor = get_vulkan_blend_factor(pipeline_key_cache_.blend_state.dstcolorblend_factor);

    VkPipelineColorBlendStateCreateInfo color_blend_state = {};
    color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.logicOpEnable = VK_FALSE;
    color_blend_state.logicOp = VK_LOGIC_OP_COPY;
    color_blend_state.pAttachments = &blend_attachment_state;

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {};
    depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_state.depthCompareOp = get_vulkan_compare_op(pipeline_key_cache_.depth_stencil_state.depth_compare_op);
    depth_stencil_state.depthTestEnable = pipeline_key_cache_.depth_stencil_state.depth_test_enable ? VK_TRUE : VK_FALSE;
    depth_stencil_state.depthWriteEnable = pipeline_key_cache_.depth_stencil_state.depth_write_enable ? VK_TRUE : VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {};
    rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.cullMode = get_vulkan_cull_mode(pipeline_key_cache_.raster_state.cull_mode);
    rasterization_state.frontFace = get_vulkan_front_face(pipeline_key_cache_.raster_state.front_face);
    rasterization_state.depthBiasEnable = pipeline_key_cache_.raster_state.depth_bias ? VK_TRUE : VK_FALSE;
    rasterization_state.depthClampEnable = pipeline_key_cache_.raster_state.depth_clamp ? VK_TRUE : VK_FALSE;
    rasterization_state.depthBiasSlopeFactor = 0.0f;//pipeline_key_cache_.raster_state.depth_bias_slope_factor;
    rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization_state.lineWidth = 1.0f;
    rasterization_state.depthBiasConstantFactor = 0.0f;//pipeline_key_cache_.raster_state.depth_bias_constant_factor;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {};
    input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = (VkPrimitiveTopology)pipeline_key_cache_.topology;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;

    VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo{};
    pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::array<VkDynamicState, 2> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables.data();
    pipelineDynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
    pipelineDynamicStateCreateInfo.flags = 0;

    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.layout = pipeline_key_cache_.pipeline_layout;
    pipelineCreateInfo.renderPass = pipeline_key_cache_.render_pass;
    pipelineCreateInfo.subpass = 0;
    pipelineCreateInfo.stageCount = 2;
    pipelineCreateInfo.pStages = shaderStages;
    pipelineCreateInfo.pVertexInputState = &vertex_input_state;
    pipelineCreateInfo.pMultisampleState = &multi_sample_state;
    pipelineCreateInfo.pColorBlendState = &color_blend_state;
    pipelineCreateInfo.pDepthStencilState = &depth_stencil_state;
    pipelineCreateInfo.pViewportState = &viewport_state;
    pipelineCreateInfo.pRasterizationState = &rasterization_state;
    pipelineCreateInfo.pInputAssemblyState = &input_assembly_state;
    pipelineCreateInfo.pDynamicState = &pipelineDynamicStateCreateInfo;
    pipelineCreateInfo.basePipelineIndex = -1;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;

    VulkanPipeline *pipeline_entry = ocarina::new_with_allocator<VulkanPipeline>();
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device->logicalDevice(), VK_NULL_HANDLE, 1, &pipelineCreateInfo,
                                               nullptr, &pipeline_entry->pipeline_));
    pipeline_entry->pipeline_layout_ = pipeline_key_cache_.pipeline_layout;
    vulkan_pipelines_.insert(std::make_pair(pipeline_key_cache_, pipeline_entry));
    
    return pipeline_entry;
}

void VulkanPipelineManager::clear(VulkanDevice *device) {
    for (auto &iter : vulkan_pipelines_) {
        vkDestroyPipelineLayout(device->logicalDevice(), iter.second->pipeline_layout_, nullptr);
        vkDestroyPipeline(device->logicalDevice(), iter.second->pipeline_, nullptr);
        ocarina::delete_with_allocator(iter.second);
    }
    vulkan_pipelines_.clear();
}

VkPipelineLayout VulkanPipelineManager::get_pipeline_layout(VulkanDevice *device, VkDescriptorSetLayout *descriptset_layouts, 
    uint8_t descriptset_layouts_count, uint32_t push_constant_size) {
    assert(descriptset_layouts_count != 0);

    PipelineLayoutKey pipeline_layout_key;
    pipeline_layout_key.descriptor_set_count = descriptset_layouts_count;
    for (uint8_t i = 0; i < descriptset_layouts_count; ++i) {
        pipeline_layout_key.descriptor_set_layouts[i] = descriptset_layouts[i];
    }

    auto it = pipeline_layouts_.find(pipeline_layout_key);
    if (it != pipeline_layouts_.end()) {
        return it->second;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = descriptset_layouts_count;
    pipelineLayoutInfo.pSetLayouts = descriptset_layouts;

    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
    push_constant_range.offset = 0;
    push_constant_range.size = push_constant_size;
    pipelineLayoutInfo.pPushConstantRanges = &push_constant_range;
    pipelineLayoutInfo.pushConstantRangeCount = push_constant_size > 0 ? 1 : 0;

    VkPipelineLayout pipeline_layout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device->logicalDevice(), &pipelineLayoutInfo, nullptr, &pipeline_layout));

    pipeline_layouts_.insert(std::make_pair(pipeline_layout_key, pipeline_layout));
    return pipeline_layout;
}

}// namespace ocarina


