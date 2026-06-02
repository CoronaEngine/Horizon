#include "vulkan_rasterizer_pipeline.h"

#include "hardware_wrapper/validation/hardware_validation.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace Corona::Horizon
{
    namespace
    {
        using BindType = EmbeddedShader::ShaderCodeModule::ShaderResources::BindType;

        [[nodiscard]] bool is_push_constant_member(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::pushConstantMembers);
        }

        [[nodiscard]] bool is_stage_output(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::stageOutputs);
        }

        [[nodiscard]] VkPrimitiveTopology to_vk_topology(PrimitiveTopology topology) noexcept
        {
            switch (topology)
            {
            case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case PrimitiveTopology::TriangleList:
            default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            }
        }

        [[nodiscard]] VkPolygonMode to_vk_polygon_mode(PolygonFillMode mode) noexcept
        {
            switch (mode)
            {
            case PolygonFillMode::Line: return VK_POLYGON_MODE_LINE;
            case PolygonFillMode::Point: return VK_POLYGON_MODE_POINT;
            case PolygonFillMode::Fill:
            default: return VK_POLYGON_MODE_FILL;
            }
        }

        [[nodiscard]] VkCullModeFlags to_vk_cull_mode(CullMode mode) noexcept
        {
            switch (mode)
            {
            case CullMode::None: return VK_CULL_MODE_NONE;
            case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
            case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
            }

            return VK_CULL_MODE_BACK_BIT;
        }

        [[nodiscard]] VkFrontFace to_vk_front_face(FrontFace front_face) noexcept
        {
            return front_face == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
        }

        [[nodiscard]] VkCompareOp to_vk_compare_op(CompareOp op) noexcept
        {
            switch (op)
            {
            case CompareOp::Never: return VK_COMPARE_OP_NEVER;
            case CompareOp::Less: return VK_COMPARE_OP_LESS;
            case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
            case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
            case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
            }

            return VK_COMPARE_OP_ALWAYS;
        }

        [[nodiscard]] VkStencilOp to_vk_stencil_op(StencilOp op) noexcept
        {
            switch (op)
            {
            case StencilOp::Keep: return VK_STENCIL_OP_KEEP;
            case StencilOp::Zero: return VK_STENCIL_OP_ZERO;
            case StencilOp::Replace: return VK_STENCIL_OP_REPLACE;
            case StencilOp::IncrementAndClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            case StencilOp::DecrementAndClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            case StencilOp::Invert: return VK_STENCIL_OP_INVERT;
            case StencilOp::IncrementAndWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
            case StencilOp::DecrementAndWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
            }

            return VK_STENCIL_OP_KEEP;
        }

        [[nodiscard]] VkStencilOpState to_vk_stencil_state(const DepthStencilOpDesc& desc, uint32_t read_mask, uint32_t write_mask, uint32_t reference) noexcept
        {
            return {
                .failOp = to_vk_stencil_op(desc.fail_op),
                .passOp = to_vk_stencil_op(desc.pass_op),
                .depthFailOp = to_vk_stencil_op(desc.depth_fail_op),
                .compareOp = to_vk_compare_op(desc.compare_op),
                .compareMask = read_mask,
                .writeMask = write_mask,
                .reference = reference,
            };
        }

        [[nodiscard]] VkBlendFactor to_vk_blend_factor(BlendFactor factor) noexcept
        {
            switch (factor)
            {
            case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
            case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
            case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            }

            return VK_BLEND_FACTOR_ONE;
        }

        [[nodiscard]] VkBlendOp to_vk_blend_op(BlendOp op) noexcept
        {
            switch (op)
            {
            case BlendOp::Add: return VK_BLEND_OP_ADD;
            case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
            case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
            case BlendOp::Min: return VK_BLEND_OP_MIN;
            case BlendOp::Max: return VK_BLEND_OP_MAX;
            }

            return VK_BLEND_OP_ADD;
        }

        [[nodiscard]] VkColorComponentFlags to_vk_color_mask(ColorWriteMask mask) noexcept
        {
            VkColorComponentFlags flags = 0;
            if ((mask & ColorWriteMask::R) == ColorWriteMask::R)
                flags |= VK_COLOR_COMPONENT_R_BIT;
            if ((mask & ColorWriteMask::G) == ColorWriteMask::G)
                flags |= VK_COLOR_COMPONENT_G_BIT;
            if ((mask & ColorWriteMask::B) == ColorWriteMask::B)
                flags |= VK_COLOR_COMPONENT_B_BIT;
            if ((mask & ColorWriteMask::A) == ColorWriteMask::A)
                flags |= VK_COLOR_COMPONENT_A_BIT;
            return flags;
        }

        [[nodiscard]] VkPipelineColorBlendAttachmentState to_vk_blend_attachment(const BlendAttachmentDesc& desc) noexcept
        {
            VkPipelineColorBlendAttachmentState state {};
            state.blendEnable = desc.blend_enabled ? VK_TRUE : VK_FALSE;
            state.srcColorBlendFactor = to_vk_blend_factor(desc.src_color_blend_factor);
            state.dstColorBlendFactor = to_vk_blend_factor(desc.dst_color_blend_factor);
            state.colorBlendOp = to_vk_blend_op(desc.color_blend_op);
            state.srcAlphaBlendFactor = to_vk_blend_factor(desc.src_alpha_blend_factor);
            state.dstAlphaBlendFactor = to_vk_blend_factor(desc.dst_alpha_blend_factor);
            state.alphaBlendOp = to_vk_blend_op(desc.alpha_blend_op);
            state.colorWriteMask = to_vk_color_mask(desc.color_write_mask);
            return state;
        }

        [[nodiscard]] VkSampleCountFlagBits to_vk_sample_count(SampleCount sample_count) noexcept
        {
            switch (sample_count)
            {
            case SampleCount::Count2: return VK_SAMPLE_COUNT_2_BIT;
            case SampleCount::Count4: return VK_SAMPLE_COUNT_4_BIT;
            case SampleCount::Count8: return VK_SAMPLE_COUNT_8_BIT;
            case SampleCount::Count16: return VK_SAMPLE_COUNT_16_BIT;
            case SampleCount::Count1:
            default: return VK_SAMPLE_COUNT_1_BIT;
            }
        }

        [[nodiscard]] std::optional<VkFormat> vertex_attribute_format(const EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo& input) noexcept
        {
            const uint32_t components = std::max(1u, std::min<uint32_t>(4u, static_cast<uint32_t>(input.elementCount == 0 ? input.typeSize / 4 : input.elementCount)));

            if (input.typeName == "uint")
            {
                switch (components)
                {
                case 1: return VK_FORMAT_R32_UINT;
                case 2: return VK_FORMAT_R32G32_UINT;
                case 3: return VK_FORMAT_R32G32B32_UINT;
                default: return VK_FORMAT_R32G32B32A32_UINT;
                }
            }

            if (input.typeName == "int")
            {
                switch (components)
                {
                case 1: return VK_FORMAT_R32_SINT;
                case 2: return VK_FORMAT_R32G32_SINT;
                case 3: return VK_FORMAT_R32G32B32_SINT;
                default: return VK_FORMAT_R32G32B32A32_SINT;
                }
            }

            switch (components)
            {
            case 1: return VK_FORMAT_R32_SFLOAT;
            case 2: return VK_FORMAT_R32G32_SFLOAT;
            case 3: return VK_FORMAT_R32G32B32_SFLOAT;
            default: return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
        }

        [[nodiscard]] BufferRef buffer_ref(const HardwareBuffer& buffer)
        {
            return { static_cast<const ResourceHandle&>(buffer) };
        }

        [[nodiscard]] ImageRef image_ref(const HardwareImage& image)
        {
            return { static_cast<const ResourceHandle&>(image) };
        }

        [[nodiscard]] bool add_overflows(uint64_t lhs, size_t rhs) noexcept
        {
            if constexpr (sizeof(size_t) > sizeof(uint64_t))
            {
                if (rhs > std::numeric_limits<uint64_t>::max())
                    return true;
            }

            return lhs > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(rhs);
        }

        [[nodiscard]] DrawIndexedParams normalize_draw_params(const HardwareBuffer& index_buffer, DrawIndexedParams params)
        {
            if (params.index_count != 0)
                return params;

            const uint64_t element_count = index_buffer.get_element_count();
            if (params.first_index >= element_count)
                return params;

            const uint64_t resolved_count = element_count - params.first_index;
            if (resolved_count > std::numeric_limits<uint32_t>::max())
                throw std::overflow_error("RasterizerPipeline draw index_count exceeds uint32_t.");

            params.index_count = static_cast<uint32_t>(resolved_count);
            return params;
        }

        [[nodiscard]] DrawIndexedDesc to_draw_desc(const DrawIndexedParams& params)
        {
            DrawIndexedDesc desc;
            desc.index_count = params.index_count;
            desc.instance_count = 1;
            desc.first_index = params.first_index;
            desc.vertex_offset = params.vertex_offset;
            desc.first_instance = 0;
            desc.index_type = params.index_type;
            desc.enable_scissor = params.enable_scissor;
            desc.scissor = params.scissor;
            return desc;
        }

        [[nodiscard]] VkShaderModule create_shader_module(VkDevice device, const EmbeddedShader::ShaderCodeModule& module, const char* label)
        {
            if (!std::holds_alternative<std::vector<uint32_t>>(module.shaderCode))
            {
                throw std::logic_error(std::string(label) + " shader must contain SPIR-V for Vulkan pipeline creation.");
            }

            const std::vector<uint32_t>& code = std::get<std::vector<uint32_t>>(module.shaderCode);
            if (code.empty())
            {
                throw std::logic_error(std::string(label) + " shader SPIR-V is empty.");
            }

            VkShaderModuleCreateInfo create_info {};
            create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            create_info.codeSize = code.size() * sizeof(uint32_t);
            create_info.pCode = code.data();

            VkShaderModule shader = VK_NULL_HANDLE;
            VkResult result = vkCreateShaderModule(device, &create_info, nullptr, &shader);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(std::string("vkCreateShaderModule failed for ") + label + ". VkResult=" + std::to_string(static_cast<int>(result)));
            }

            return shader;
        }
    }

    VulkanRasterizerPipeline::VulkanRasterizerPipeline(RasterizerPipelineDesc desc,
                                                       std::source_location source_location)
        : desc_(std::move(desc)),
          source_location_(source_location)
    {
        (void)validate_rasterizer_pipeline_desc(desc_);

        const uint32_t constant_size = push_constant_size();
        if (constant_size != 0)
            push_constant_data_.resize(constant_size);
    }

    VulkanRasterizerPipeline::~VulkanRasterizerPipeline()
    {
        std::lock_guard lock(mutex_);
        destroy_pipeline_cache_unlocked();
    }

    RasterizerPipelineDesc VulkanRasterizerPipeline::desc() const
    {
        std::lock_guard lock(mutex_);
        return desc_;
    }

    uint32_t VulkanRasterizerPipeline::push_constant_size() const noexcept
    {
        return std::max(desc_.vertex_shader.module.shaderResources.pushConstantSize,
                        desc_.fragment_shader.module.shaderResources.pushConstantSize);
    }

    void VulkanRasterizerPipeline::destroy_pipeline_cache_unlocked() noexcept
    {
        for (PipelineState& state : pipeline_cache_)
        {
            if (state.key.device != VK_NULL_HANDLE)
            {
                if (state.pipeline != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(state.key.device, state.pipeline, nullptr);
                    state.pipeline = VK_NULL_HANDLE;
                }

                if (state.layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(state.key.device, state.layout, nullptr);
                    state.layout = VK_NULL_HANDLE;
                }
            }
        }

        pipeline_cache_.clear();
    }

    VulkanRasterizerPipeline::PipelineState VulkanRasterizerPipeline::create_graphics_pipeline_unlocked(const PipelineKey& key) const
    {
        if (key.device == VK_NULL_HANDLE)
            throw std::logic_error("RasterizerPipeline graphics pipeline creation requires a valid VkDevice.");

        if (key.color_format == VK_FORMAT_UNDEFINED && key.depth_format == VK_FORMAT_UNDEFINED)
            throw std::logic_error("RasterizerPipeline graphics pipeline creation requires at least one attachment format.");

        VkShaderModule vertex_shader = create_shader_module(key.device, desc_.vertex_shader.module, "vertex");
        VkShaderModule fragment_shader = VK_NULL_HANDLE;

        try
        {
            fragment_shader = create_shader_module(key.device, desc_.fragment_shader.module, "fragment");

            VkPipelineShaderStageCreateInfo shader_stages[2] {};
            shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shader_stages[0].module = vertex_shader;
            shader_stages[0].pName = "main";
            shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shader_stages[1].module = fragment_shader;
            shader_stages[1].pName = "main";

            std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> inputs;
            for (const auto& input : desc_.vertex_shader.module.shaderResources.bindInfoPool)
            {
                if (input.bindType == EmbeddedShader::ShaderCodeModule::ShaderResources::stageInputs)
                    inputs.push_back(input);
            }

            std::ranges::sort(inputs, [](const auto& left, const auto& right) {
                return left.location < right.location;
            });

            std::vector<VkVertexInputAttributeDescription> attributes;
            attributes.reserve(inputs.size());

            uint32_t next_offset = 0;
            for (const auto& input : inputs)
            {
                std::optional<VkFormat> format = vertex_attribute_format(input);
                if (!format)
                    continue;

                VkVertexInputAttributeDescription attribute {};
                attribute.location = input.location;
                attribute.binding = 0;
                attribute.format = *format;
                attribute.offset = input.byteOffset != 0 ? static_cast<uint32_t>(input.byteOffset) : next_offset;
                attributes.push_back(attribute);

                const uint32_t byte_size = input.typeSize != 0 ? input.typeSize : 4;
                next_offset = attribute.offset + byte_size;
            }

            VkVertexInputBindingDescription binding {};
            binding.binding = 0;
            binding.stride = key.vertex_stride != 0 ? key.vertex_stride : next_offset;
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkPipelineVertexInputStateCreateInfo vertex_input {};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = binding.stride != 0 ? 1u : 0u;
            vertex_input.pVertexBindingDescriptions = binding.stride != 0 ? &binding : nullptr;
            vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
            vertex_input.pVertexAttributeDescriptions = attributes.empty() ? nullptr : attributes.data();

            VkPipelineInputAssemblyStateCreateInfo input_assembly {};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = to_vk_topology(desc_.rasterizer.topology);
            input_assembly.primitiveRestartEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewport_state {};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterization {};
            rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterization.depthClampEnable = desc_.rasterizer.depth_clamp_enabled ? VK_TRUE : VK_FALSE;
            rasterization.rasterizerDiscardEnable = desc_.rasterizer.rasterizer_discard_enabled ? VK_TRUE : VK_FALSE;
            rasterization.polygonMode = to_vk_polygon_mode(desc_.rasterizer.fill_mode);
            rasterization.cullMode = to_vk_cull_mode(desc_.rasterizer.cull_mode);
            rasterization.frontFace = to_vk_front_face(desc_.rasterizer.front_face);
            rasterization.depthBiasEnable = VK_FALSE;
            rasterization.lineWidth = desc_.rasterizer.line_width;

            VkPipelineMultisampleStateCreateInfo multisample {};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = to_vk_sample_count(desc_.multisample.sample_count);
            multisample.sampleShadingEnable = desc_.multisample.sample_shading_enabled ? VK_TRUE : VK_FALSE;
            multisample.minSampleShading = desc_.multisample.min_sample_shading;

            VkPipelineDepthStencilStateCreateInfo depth_stencil {};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = desc_.depth_stencil.depth_test_enabled && key.depth_format != VK_FORMAT_UNDEFINED ? VK_TRUE : VK_FALSE;
            depth_stencil.depthWriteEnable = desc_.depth_stencil.depth_write_enabled && key.depth_format != VK_FORMAT_UNDEFINED ? VK_TRUE : VK_FALSE;
            depth_stencil.depthCompareOp = to_vk_compare_op(desc_.depth_stencil.depth_compare_op);
            depth_stencil.depthBoundsTestEnable = VK_FALSE;
            depth_stencil.stencilTestEnable = desc_.depth_stencil.stencil_test_enabled ? VK_TRUE : VK_FALSE;
            depth_stencil.front = to_vk_stencil_state(desc_.depth_stencil.front,
                                                      desc_.depth_stencil.stencil_read_mask,
                                                      desc_.depth_stencil.stencil_write_mask,
                                                      desc_.depth_stencil.stencil_reference);
            depth_stencil.back = to_vk_stencil_state(desc_.depth_stencil.back,
                                                     desc_.depth_stencil.stencil_read_mask,
                                                     desc_.depth_stencil.stencil_write_mask,
                                                     desc_.depth_stencil.stencil_reference);

            const BlendAttachmentDesc blend_desc = desc_.blend.attachments.empty()
                ? BlendStateDesc::alpha_blend_attachment()
                : desc_.blend.attachments.front();
            VkPipelineColorBlendAttachmentState blend_attachment = to_vk_blend_attachment(blend_desc);

            VkPipelineColorBlendStateCreateInfo color_blend {};
            color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blend.logicOpEnable = desc_.blend.logic_op_enabled ? VK_TRUE : VK_FALSE;
            color_blend.logicOp = VK_LOGIC_OP_COPY;
            color_blend.attachmentCount = key.color_format == VK_FORMAT_UNDEFINED ? 0u : 1u;
            color_blend.pAttachments = key.color_format == VK_FORMAT_UNDEFINED ? nullptr : &blend_attachment;

            VkDynamicState dynamic_states[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };

            VkPipelineDynamicStateCreateInfo dynamic_state {};
            dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
            dynamic_state.pDynamicStates = dynamic_states;

            VkPushConstantRange push_constant_range {};
            const uint32_t push_constant_bytes = push_constant_size();
            if (push_constant_bytes != 0)
            {
                push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                push_constant_range.offset = 0;
                push_constant_range.size = push_constant_bytes;
            }

            VkPipelineLayoutCreateInfo layout_info {};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.pushConstantRangeCount = push_constant_bytes != 0 ? 1u : 0u;
            layout_info.pPushConstantRanges = push_constant_bytes != 0 ? &push_constant_range : nullptr;

            PipelineState state;
            state.key = key;

            VkResult result = vkCreatePipelineLayout(key.device, &layout_info, nullptr, &state.layout);
            if (result != VK_SUCCESS)
                throw std::runtime_error("vkCreatePipelineLayout failed for RasterizerPipeline. VkResult=" + std::to_string(static_cast<int>(result)));

            VkPipelineRenderingCreateInfo rendering_info {};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.viewMask = desc_.multiview_count > 1 && desc_.multiview_count < 32
                ? ((uint32_t { 1 } << desc_.multiview_count) - 1u)
                : 0u;
            rendering_info.colorAttachmentCount = key.color_format == VK_FORMAT_UNDEFINED ? 0u : 1u;
            rendering_info.pColorAttachmentFormats = key.color_format == VK_FORMAT_UNDEFINED ? nullptr : &key.color_format;
            rendering_info.depthAttachmentFormat = key.depth_format;
            rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

            VkGraphicsPipelineCreateInfo pipeline_info {};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.pNext = &rendering_info;
            pipeline_info.stageCount = 2;
            pipeline_info.pStages = shader_stages;
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterization;
            pipeline_info.pMultisampleState = &multisample;
            pipeline_info.pDepthStencilState = &depth_stencil;
            pipeline_info.pColorBlendState = &color_blend;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = state.layout;
            pipeline_info.renderPass = VK_NULL_HANDLE;
            pipeline_info.subpass = 0;
            pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

            result = vkCreateGraphicsPipelines(key.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &state.pipeline);
            if (result != VK_SUCCESS)
            {
                vkDestroyPipelineLayout(key.device, state.layout, nullptr);
                state.layout = VK_NULL_HANDLE;
                throw std::runtime_error("vkCreateGraphicsPipelines failed for RasterizerPipeline. VkResult=" + std::to_string(static_cast<int>(result)));
            }

            vkDestroyShaderModule(key.device, fragment_shader, nullptr);
            vkDestroyShaderModule(key.device, vertex_shader, nullptr);
            return state;
        }
        catch (...)
        {
            if (fragment_shader != VK_NULL_HANDLE)
                vkDestroyShaderModule(key.device, fragment_shader, nullptr);
            if (vertex_shader != VK_NULL_HANDLE)
                vkDestroyShaderModule(key.device, vertex_shader, nullptr);
            throw;
        }
    }

    VulkanRasterizerPipeline::GraphicsPipeline VulkanRasterizerPipeline::graphics_pipeline(VkDevice device,
                                                                                          VkFormat color_format,
                                                                                          VkFormat depth_format,
                                                                                          uint32_t vertex_stride)
    {
        PipelineKey key {
            .device = device,
            .color_format = color_format,
            .depth_format = depth_format,
            .vertex_stride = vertex_stride,
        };

        std::lock_guard lock(mutex_);
        auto found = std::ranges::find_if(pipeline_cache_, [&](const PipelineState& state) {
            return state.key == key;
        });

        if (found == pipeline_cache_.end())
        {
            pipeline_cache_.push_back(create_graphics_pipeline_unlocked(key));
            found = std::prev(pipeline_cache_.end());
        }

        return {
            .layout = found->layout,
            .pipeline = found->pipeline,
        };
    }

    void VulkanRasterizerPipeline::set_extent(uint16_t width, uint16_t height)
    {
        std::lock_guard lock(mutex_);
        width_ = width;
        height_ = height;
    }

    void VulkanRasterizerPipeline::set_push_constant_direct(uint64_t byte_offset,
                                                            const void* data,
                                                            size_t size,
                                                            int32_t bind_type)
    {
        if (!is_push_constant_member(bind_type) || size == 0)
            return;

        if (data == nullptr)
            throw std::invalid_argument("RasterizerPipeline push constant data must not be null.");

        if (add_overflows(byte_offset, size))
            throw std::overflow_error("RasterizerPipeline push constant range overflow.");

        const uint64_t end = byte_offset + static_cast<uint64_t>(size);
        if (end > std::numeric_limits<size_t>::max())
            throw std::overflow_error("RasterizerPipeline push constant range is too large for host memory.");

        std::lock_guard lock(mutex_);
        const uint32_t declared_size = push_constant_size();
        if (declared_size != 0 && end > declared_size)
            throw std::out_of_range("RasterizerPipeline push constant write exceeds reflected push constant size.");

        if (push_constant_data_.size() < static_cast<size_t>(end))
            push_constant_data_.resize(static_cast<size_t>(end));

        std::memcpy(push_constant_data_.data() + static_cast<size_t>(byte_offset), data, size);
    }

    void VulkanRasterizerPipeline::set_resource_direct(uint64_t byte_offset,
                                                       uint32_t type_size,
                                                       const HardwareBuffer& buffer,
                                                       int32_t bind_type)
    {
        std::lock_guard lock(mutex_);

        auto found = std::ranges::find_if(bound_buffers_, [&](const BoundBuffer& bound) {
            return bound.byte_offset == byte_offset &&
                   bound.type_size == type_size &&
                   bound.bind_type == bind_type;
        });

        BoundBuffer binding {
            .byte_offset = byte_offset,
            .type_size = type_size,
            .bind_type = bind_type,
            .buffer = buffer,
        };

        if (found == bound_buffers_.end())
            bound_buffers_.push_back(std::move(binding));
        else
            *found = std::move(binding);
    }

    void VulkanRasterizerPipeline::set_resource_direct(uint64_t byte_offset,
                                                       uint32_t type_size,
                                                       const HardwareImage& image,
                                                       int32_t bind_type,
                                                       uint32_t location)
    {
        std::lock_guard lock(mutex_);

        auto found = std::ranges::find_if(bound_images_, [&](const BoundImage& bound) {
            if (is_stage_output(bind_type) || is_stage_output(bound.bind_type))
                return bound.bind_type == bind_type && bound.location == location;

            return bound.byte_offset == byte_offset &&
                   bound.type_size == type_size &&
                   bound.bind_type == bind_type &&
                   bound.location == location;
        });

        BoundImage binding {
            .byte_offset = byte_offset,
            .type_size = type_size,
            .bind_type = bind_type,
            .location = location,
            .image = image,
        };

        if (found == bound_images_.end())
            bound_images_.push_back(std::move(binding));
        else
            *found = std::move(binding);
    }

    void VulkanRasterizerPipeline::add_auto_bind_entry(EmbeddedShader::AutoBindEntry entry)
    {
        std::lock_guard lock(mutex_);
        desc_.auto_bind_entries.push_back(std::move(entry));
    }

    std::vector<EmbeddedShader::AutoBindEntry> VulkanRasterizerPipeline::auto_bind_entries() const
    {
        std::lock_guard lock(mutex_);
        return desc_.auto_bind_entries;
    }

    void VulkanRasterizerPipeline::record(const ResourceHandle& pipeline,
                                          const HardwareBuffer& index_buffer,
                                          const HardwareBuffer& vertex_buffer,
                                          const DrawIndexedParams& params)
    {
        if (!validate_rasterizer_pipeline_record(index_buffer, vertex_buffer, params))
            return;

        RecordedDraw draw;
        draw.pipeline = ResourceBridge::token(pipeline);
        draw.index_buffer = index_buffer;
        draw.vertex_buffer = vertex_buffer;
        draw.params = normalize_draw_params(index_buffer, params);

        std::lock_guard lock(mutex_);
        draw.push_constant_data = push_constant_data_;
        draws_.push_back(std::move(draw));
    }

    void VulkanRasterizerPipeline::record(const HardwareBuffer& index_buffer,
                                          const HardwareBuffer& vertex_buffer,
                                          const DrawIndexedParams& params)
    {
        record({}, index_buffer, vertex_buffer, params);
    }

    void VulkanRasterizerPipeline::clear_records()
    {
        std::lock_guard lock(mutex_);
        draws_.clear();
    }

    VulkanRasterizerPipeline::Snapshot VulkanRasterizerPipeline::snapshot() const
    {
        std::lock_guard lock(mutex_);
        return {
            .desc = desc_,
            .width = width_,
            .height = height_,
            .buffers = bound_buffers_,
            .images = bound_images_,
            .draws = draws_,
        };
    }

    CommandBatch VulkanRasterizerPipeline::command_batch() const
    {
        Snapshot state = snapshot();
        CommandBatch batch;

        const BoundImage* first_color = nullptr;
        for (const BoundImage& image : state.images)
        {
            if (!is_stage_output(image.bind_type) || !image.image)
                continue;

            if (first_color == nullptr || image.location < first_color->location)
                first_color = &image;
        }

        const bool has_rendering_scope = first_color != nullptr && state.width != 0 && state.height != 0;
        if (has_rendering_scope)
        {
            batch << begin_rendering(
                {
                    .color = image_ref(first_color->image),
                    .depth = {},
                    .width = state.width,
                    .height = state.height,
                });
        }

        for (const RecordedDraw& draw : state.draws)
        {
            DrawIndexedDesc draw_desc = to_draw_desc(draw.params);
            ResourceBridge::set(draw_desc.pipeline, draw.pipeline.lock());
            draw_desc.push_constant_data = draw.push_constant_data;

            batch << draw_indexed(buffer_ref(draw.index_buffer),
                                  buffer_ref(draw.vertex_buffer),
                                  std::move(draw_desc));
        }

        if (has_rendering_scope)
            batch << end_rendering();

        return batch;
    }
}
