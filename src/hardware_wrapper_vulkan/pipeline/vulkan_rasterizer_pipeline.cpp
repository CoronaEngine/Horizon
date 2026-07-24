#include "vulkan_rasterizer_pipeline.h"

#include "horizon_profiling.h"

#include "hardware_wrapper/validation/hardware_validation.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "hardware_wrapper_vulkan/hardware/execution_profile.h"
#include "hardware_wrapper_vulkan/resource_pool.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "hardware_wrapper/diagnostics.h"

namespace Corona::Horizon
{
    namespace
    {
        using BindType = EmbeddedShader::ShaderCodeModule::ShaderResources::BindType;
        using BufferStore = ResourceStore<BufferWrap, BufferReleaser>;
        using ImageStore = ResourceStore<ImageWrap, ImageReleaser>;

        void name_vulkan_object(VkDevice device, VkObjectType object_type, uint64_t object_handle, const std::string& name) noexcept
        {
            if (vkSetDebugUtilsObjectNameEXT == nullptr || device == VK_NULL_HANDLE || object_handle == 0 || name.empty())
            {
                return;
            }

            VkDebugUtilsObjectNameInfoEXT name_info {};
            name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            name_info.objectType = object_type;
            name_info.objectHandle = object_handle;
            name_info.pObjectName = name.c_str();
            (void)vkSetDebugUtilsObjectNameEXT(device, &name_info);
        }

        [[nodiscard]] bool is_push_constant_member(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::pushConstantMembers);
        }

        [[nodiscard]] bool is_stage_output(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::stageOutputs);
        }

        [[nodiscard]] bool is_storage_buffer_bind(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::storageBuffer) ||
                   bind_type == static_cast<int32_t>(BindType::rawBuffer);
        }

        [[nodiscard]] bool is_storage_image_bind(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::storageTexture);
        }

        [[nodiscard]] bool is_sampled_image_bind(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::sampledImages) ||
                   bind_type == static_cast<int32_t>(BindType::texture) ||
                   bind_type == static_cast<int32_t>(BindType::sampler);
        }

        [[nodiscard]] bool is_uniform_buffer_member(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::uniformBufferMembers);
        }

        [[nodiscard]] bool is_direct_resource_bind(int32_t bind_type) noexcept
        {
            return is_storage_buffer_bind(bind_type) ||
                   is_storage_image_bind(bind_type) ||
                   is_sampled_image_bind(bind_type);
        }

        struct BindingCoordinates
        {
            uint32_t set { 0 };
            uint32_t binding { 0 };
        };

        [[nodiscard]] BindingCoordinates reflected_binding_coordinates(const EmbeddedShader::ShaderCodeModule& module,
                                                                       const EmbeddedShader::AutoBindEntry& entry) noexcept
        {
            for (const auto& info : module.shaderResources.bindInfoPool)
            {
                if (static_cast<int32_t>(info.bindType) != entry.bindType)
                    continue;
                if (info.byteOffset != entry.byteOffset)
                    continue;
                if (entry.typeSize != 0 && info.typeSize != 0 && info.typeSize != entry.typeSize)
                    continue;
                return { info.set, info.binding };
            }

            return {};
        }

        [[nodiscard]] BindingCoordinates reflected_binding_coordinates(const RasterizerPipelineDesc& desc,
                                                                       const EmbeddedShader::AutoBindEntry& entry) noexcept
        {
            BindingCoordinates coordinates = reflected_binding_coordinates(desc.vertex_shader.module, entry);
            if (coordinates.set != 0 || coordinates.binding != 0)
                return coordinates;

            return reflected_binding_coordinates(desc.fragment_shader.module, entry);
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

        [[nodiscard]] BufferStore::Read read_buffer_resource(const ResourceHandle& handle)
        {
            return read<BufferStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] BufferStore::Write write_buffer_resource(const ResourceHandle& handle)
        {
            return write<BufferStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] ImageStore::Write write_image_resource(const ResourceHandle& handle)
        {
            return write<ImageStore>(ResourceBridge::token(handle));
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

        [[nodiscard]] uint32_t descriptor_write_size(uint32_t reflected_type_size) noexcept
        {
            constexpr uint32_t descriptor_handle32_size = sizeof(uint32_t);
            constexpr uint32_t descriptor_handle64_size = sizeof(uint32_t) * 2;
            if (reflected_type_size == 0)
                return descriptor_handle64_size;

            return reflected_type_size >= descriptor_handle64_size ? descriptor_handle64_size : descriptor_handle32_size;
        }

        void write_bytes(std::vector<std::byte>& target,
                         uint32_t declared_size,
                         uint64_t byte_offset,
                         const void* data,
                         size_t size,
                         const char* label)
        {
            if (size == 0)
                return;
            if (data == nullptr)
                throw std::invalid_argument(std::string(label) + " data must not be null.");
            if (add_overflows(byte_offset, size))
                throw std::overflow_error(std::string(label) + " range overflow.");

            const uint64_t end = byte_offset + static_cast<uint64_t>(size);
            if (end > std::numeric_limits<size_t>::max())
                throw std::overflow_error(std::string(label) + " range is too large for host memory.");
            if (declared_size != 0 && end > declared_size)
                throw std::out_of_range(std::string(label) + " write exceeds reflected size.");

            if (target.size() < static_cast<size_t>(end))
                target.resize(static_cast<size_t>(end));

            std::memcpy(target.data() + static_cast<size_t>(byte_offset), data, size);
        }

        void write_descriptor_handle(std::vector<std::byte>& target,
                                     uint32_t declared_size,
                                     uint64_t byte_offset,
                                     uint32_t reflected_type_size,
                                     uint32_t descriptor_index,
                                     const char* label)
        {
            const uint32_t write_size = descriptor_write_size(reflected_type_size);
            if (write_size >= sizeof(uint32_t) * 2)
            {
                const uint32_t handle_data[2] = { descriptor_index, 0 };
                write_bytes(target, declared_size, byte_offset, handle_data, sizeof(handle_data), label);
                return;
            }

            write_bytes(target, declared_size, byte_offset, &descriptor_index, sizeof(descriptor_index), label);
        }

        [[nodiscard]] bool is_bindless_reserved_binding(const EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo& info) noexcept
        {
            if (info.binding != 0)
                return false;

            if (info.set == ResourceManager::bindless_texture_set)
                return is_sampled_image_bind(static_cast<int32_t>(info.bindType)) && info.elementCount != 1;
            if (info.set == ResourceManager::bindless_storage_buffer_set)
                return info.bindType == BindType::rawBuffer || info.bindType == BindType::storageBuffer;
            if (info.set == ResourceManager::bindless_storage_image_set)
                return info.bindType == BindType::storageTexture;

            return false;
        }

        [[nodiscard]] bool is_bindless_table(const EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo& info) noexcept
        {
            return is_bindless_reserved_binding(info) && info.elementCount != 1;
        }

        [[nodiscard]] bool uses_bindless_descriptors(const EmbeddedShader::ShaderCodeModule& module) noexcept
        {
            for (const auto& info : module.shaderResources.bindInfoPool)
            {
                if (is_bindless_reserved_binding(info))
                    return true;
            }

            return false;
        }

        [[nodiscard]] bool uses_bindless_descriptors(const RasterizerPipelineDesc& desc) noexcept
        {
            return uses_bindless_descriptors(desc.vertex_shader.module) ||
                   uses_bindless_descriptors(desc.fragment_shader.module);
        }

        void add_uniform_buffer(std::vector<UniformBufferBindingData>& buffers, uint32_t set, uint32_t binding, uint32_t size)
        {
            if (size == 0)
                return;

            auto found = std::ranges::find_if(buffers, [&](const UniformBufferBindingData& item) {
                return item.set == set && item.binding == binding;
            });
            if (found == buffers.end())
            {
                UniformBufferBindingData item;
                item.set = set;
                item.binding = binding;
                item.data.resize(size);
                buffers.push_back(std::move(item));
                return;
            }

            if (found->data.size() < size)
                found->data.resize(size);
        }

        void append_reflected_uniform_buffers(std::vector<UniformBufferBindingData>& buffers,
                                              const EmbeddedShader::ShaderCodeModule& module)
        {
            bool found = false;
            for (const auto& info : module.shaderResources.bindInfoPool)
            {
                if (info.bindType != BindType::uniformBuffers)
                    continue;

                found = true;
                const uint32_t size = info.typeSize != 0 ? info.typeSize : module.shaderResources.uniformBufferSize;
                add_uniform_buffer(buffers, info.set, info.binding, size);
            }

            if (!found && module.shaderResources.uniformBufferSize != 0)
                add_uniform_buffer(buffers, 0, 0, module.shaderResources.uniformBufferSize);
        }

        [[nodiscard]] std::vector<UniformBufferBindingData> reflected_uniform_buffers(const RasterizerPipelineDesc& desc)
        {
            std::vector<UniformBufferBindingData> buffers;
            append_reflected_uniform_buffers(buffers, desc.vertex_shader.module);
            append_reflected_uniform_buffers(buffers, desc.fragment_shader.module);
            return buffers;
        }

        void write_uniform_member(std::vector<UniformBufferBindingData>& buffers,
                                  uint32_t set,
                                  uint32_t binding,
                                  uint64_t byte_offset,
                                  const void* data,
                                  size_t size)
        {
            auto found = std::ranges::find_if(buffers, [&](const UniformBufferBindingData& item) {
                return item.set == set && item.binding == binding;
            });
            if (found == buffers.end() && buffers.size() == 1)
                found = buffers.begin();
            if (found == buffers.end())
                throw std::out_of_range("RasterizerPipeline uniform member does not match any reflected uniform buffer.");

            if (found->data.size() > std::numeric_limits<uint32_t>::max())
                throw std::overflow_error("RasterizerPipeline uniform buffer is too large.");

            write_bytes(found->data,
                        static_cast<uint32_t>(found->data.size()),
                        byte_offset,
                        data,
                        size,
                        "RasterizerPipeline uniform buffer");
        }

        [[nodiscard]] uint32_t store_storage_buffer_descriptor(const HardwareBuffer& buffer)
        {
            BufferStore::Write native = write_buffer_resource(static_cast<const ResourceHandle&>(buffer));
            if (!native || native->buffer_handle == VK_NULL_HANDLE)
                throw std::logic_error("RasterizerPipeline bindless storage buffer requires a valid HardwareBuffer.");

            ResourceManager* manager = native->resource_manager != nullptr ? native->resource_manager : &resource_manager();
            return manager->store_descriptor(*native);
        }

        [[nodiscard]] uint32_t store_sampled_image_descriptor(const HardwareImage& image)
        {
            ImageStore::Write native = write_image_resource(static_cast<const ResourceHandle&>(image));
            if (!native || native->image_view == VK_NULL_HANDLE)
                throw std::logic_error("RasterizerPipeline bindless sampled image requires a valid HardwareImage.");

            ResourceManager* manager = native->resource_manager != nullptr ? native->resource_manager : &resource_manager();
            return manager->store_sampled_descriptor(*native);
        }

        [[nodiscard]] uint32_t store_storage_image_descriptor(const HardwareImage& image)
        {
            ImageStore::Write native = write_image_resource(static_cast<const ResourceHandle&>(image));
            if (!native || native->image_view == VK_NULL_HANDLE)
                throw std::logic_error("RasterizerPipeline bindless storage image requires a valid HardwareImage.");

            ResourceManager* manager = native->resource_manager != nullptr ? native->resource_manager : &resource_manager();
            return manager->store_storage_descriptor(*native);
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
            desc.debug_label = params.debug_label;
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

        struct TransientDescriptorSet
        {
            VkDevice device { VK_NULL_HANDLE };
            VkDescriptorPool pool { VK_NULL_HANDLE };
            std::vector<HardwareBuffer> buffers;

            ~TransientDescriptorSet()
            {
                HORIZON_PROFILE_SCOPE_N("TransientDescriptorSet::destroy");
                if (device != VK_NULL_HANDLE && pool != VK_NULL_HANDLE)
                    vkDestroyDescriptorPool(device, pool, nullptr);
                buffers.clear(); // 让 uniform buffer 的销毁也计入本 zone
            }
        };
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
        uniform_buffers_ = reflected_uniform_buffers(desc_);

        // 为每个 UBO binding 创建持久 GPU buffer
        ubo_buffers_.reserve(uniform_buffers_.size());
        for (auto& ubo : uniform_buffers_)
        {
            if (ubo.data.empty())
            {
                ubo_buffers_.emplace_back();
                continue;
            }
            HardwareBuffer buf = HardwareBuffer::from_bytes(
                std::span<const std::byte>(ubo.data),
                1, BufferUsageFlags::Uniform, "RasterizerPipeline.ubo_persistent");
            ubo.gpu_buffer = buf;
            ubo_buffers_.push_back(std::move(buf));
        }
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

                for (PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
                {
                    if (set_layout.layout != VK_NULL_HANDLE)
                    {
                        vkDestroyDescriptorSetLayout(state.key.device, set_layout.layout, nullptr);
                        set_layout.layout = VK_NULL_HANDLE;
                    }
                }

                for (VkDescriptorSetLayout layout : state.empty_descriptor_set_layouts)
                {
                    if (layout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(state.key.device, layout, nullptr);
                }
            }
        }

        pipeline_cache_.clear();
    }

    VulkanRasterizerPipeline::PipelineState VulkanRasterizerPipeline::create_graphics_pipeline_unlocked(const PipelineKey& key) const
    {
        if (key.device == VK_NULL_HANDLE)
            throw std::logic_error("RasterizerPipeline graphics pipeline creation requires a valid VkDevice.");

        if (key.color_formats[0] == VK_FORMAT_UNDEFINED && key.depth_format == VK_FORMAT_UNDEFINED)
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
            rasterization.cullMode = VK_CULL_MODE_NONE;
            rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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

            // If blending is requested but the color attachment format does not advertise
            // VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT (e.g. integer render targets like
            // RGBA32_UINT), forcing blendEnable would trip Vulkan validation / runtime errors.
            // Honor the user's blend factors/ops but clamp blendEnable to false and warn so the
            // dropped blend is visible rather than silently swallowed.
            if (blend_attachment.blendEnable == VK_TRUE && key.color_formats[0] != VK_FORMAT_UNDEFINED)
            {
                const VkPhysicalDevice physical_device = device_manager().physical_device();
                if (physical_device != VK_NULL_HANDLE)
                {
                    VkFormatProperties format_properties {};
                    vkGetPhysicalDeviceFormatProperties(physical_device, key.color_formats[0], &format_properties);
                    if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) == 0)
                    {
                        blend_attachment.blendEnable = VK_FALSE;
                        Diagnostics::write(Diagnostics::Level::Warning,
                                           "HORIZON PIPELINE",
                                           "Color attachment format (VkFormat=" + std::to_string(static_cast<int>(key.color_formats[0])) +
                                               ") does not support VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT; "
                                               "blending was requested but has been disabled for this pipeline.");
                    }
                }
            }

            uint32_t color_attachment_count = 0;
            for (VkFormat format : key.color_formats)
            {
                if (format == VK_FORMAT_UNDEFINED)
                    break;
                ++color_attachment_count;
            }

            // 所有颜色附件复用 attachment 0 的混合状态
            std::array<VkPipelineColorBlendAttachmentState, 4> blend_attachments;
            blend_attachments.fill(blend_attachment);

            VkPipelineColorBlendStateCreateInfo color_blend {};
            color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blend.logicOpEnable = desc_.blend.logic_op_enabled ? VK_TRUE : VK_FALSE;
            color_blend.logicOp = VK_LOGIC_OP_COPY;
            color_blend.attachmentCount = color_attachment_count;
            color_blend.pAttachments = color_attachment_count != 0 ? blend_attachments.data() : nullptr;

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

            PipelineState state;
            state.key = key;
            state.uses_bindless = uses_bindless_descriptors(desc_);

            auto add_descriptor_binding = [&](uint32_t set, uint32_t binding, VkDescriptorType descriptor_type) {
                if (state.uses_bindless && set < ResourceManager::bindless_descriptor_set_count)
                {
                    throw std::logic_error("RasterizerPipeline ordinary descriptors cannot use Horizon bindless reserved sets 0-2.");
                }

                auto set_found = std::ranges::find_if(state.descriptor_set_layouts, [set](const PipelineState::DescriptorSetLayout& layout) {
                    return layout.set == set;
                });
                if (set_found == state.descriptor_set_layouts.end())
                {
                    PipelineState::DescriptorSetLayout layout;
                    layout.set = set;
                    state.descriptor_set_layouts.push_back(std::move(layout));
                    set_found = std::prev(state.descriptor_set_layouts.end());
                }

                auto binding_found = std::ranges::find_if(set_found->bindings, [binding](const PipelineState::DescriptorBindingLayout& layout) {
                    return layout.binding == binding;
                });
                if (binding_found != set_found->bindings.end())
                {
                    if (binding_found->descriptor_type != descriptor_type)
                        throw std::logic_error("RasterizerPipeline reflection maps different descriptor types to the same set/binding.");
                    return;
                }

                set_found->bindings.push_back({ set, binding, descriptor_type });
            };

            for (const UniformBufferBindingData& uniform_buffer : reflected_uniform_buffers(desc_))
            {
                add_descriptor_binding(uniform_buffer.set, uniform_buffer.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            }

            std::ranges::sort(state.descriptor_set_layouts, [](const auto& left, const auto& right) {
                return left.set < right.set;
            });
            for (PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
            {
                std::ranges::sort(set_layout.bindings, [](const auto& left, const auto& right) {
                    return left.binding < right.binding;
                });
            }

            for (PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
            {
                std::vector<VkDescriptorSetLayoutBinding> descriptor_bindings;
                descriptor_bindings.reserve(set_layout.bindings.size());
                for (const PipelineState::DescriptorBindingLayout& binding_layout : set_layout.bindings)
                {
                    VkDescriptorSetLayoutBinding layout_binding {};
                    layout_binding.binding = binding_layout.binding;
                    layout_binding.descriptorType = binding_layout.descriptor_type;
                    layout_binding.descriptorCount = 1;
                    layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                    descriptor_bindings.push_back(layout_binding);
                }

                VkDescriptorSetLayoutCreateInfo descriptor_layout_info {};
                descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                descriptor_layout_info.bindingCount = static_cast<uint32_t>(descriptor_bindings.size());
                descriptor_layout_info.pBindings = descriptor_bindings.data();

                VkResult descriptor_result = vkCreateDescriptorSetLayout(key.device,
                                                                         &descriptor_layout_info,
                                                                         nullptr,
                                                                         &set_layout.layout);
                if (descriptor_result != VK_SUCCESS)
                {
                    throw std::runtime_error("vkCreateDescriptorSetLayout failed for RasterizerPipeline. VkResult=" +
                                             std::to_string(static_cast<int>(descriptor_result)));
                }
            }

            bool has_set_layouts = state.uses_bindless || !state.descriptor_set_layouts.empty();
            uint32_t max_set = state.uses_bindless ? ResourceManager::bindless_descriptor_set_count - 1u : 0u;
            for (const PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
                max_set = std::max(max_set, set_layout.set);

            std::array<VkDescriptorSetLayout, ResourceManager::bindless_descriptor_set_count> bindless_layouts {};
            std::vector<VkDescriptorSetLayout> set_layouts;
            if (has_set_layouts)
            {
                set_layouts.resize(static_cast<size_t>(max_set) + 1u, VK_NULL_HANDLE);
                if (state.uses_bindless)
                {
                    bindless_layouts = resource_manager().bindless_descriptor_set_layouts();
                    for (uint32_t set = 0; set < ResourceManager::bindless_descriptor_set_count; ++set)
                        set_layouts[set] = bindless_layouts[set];
                }

                for (const PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
                    set_layouts[set_layout.set] = set_layout.layout;

                for (VkDescriptorSetLayout& set_layout : set_layouts)
                {
                    if (set_layout != VK_NULL_HANDLE)
                        continue;

                    VkDescriptorSetLayoutCreateInfo empty_layout_info {};
                    empty_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                    VkDescriptorSetLayout empty_layout = VK_NULL_HANDLE;
                    VkResult empty_result = vkCreateDescriptorSetLayout(key.device, &empty_layout_info, nullptr, &empty_layout);
                    if (empty_result != VK_SUCCESS)
                    {
                        throw std::runtime_error("vkCreateDescriptorSetLayout(empty) failed for RasterizerPipeline. VkResult=" +
                                                 std::to_string(static_cast<int>(empty_result)));
                    }

                    state.empty_descriptor_set_layouts.push_back(empty_layout);
                    set_layout = empty_layout;
                }
            }

            VkPipelineLayoutCreateInfo layout_info {};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
            layout_info.pSetLayouts = set_layouts.empty() ? nullptr : set_layouts.data();
            layout_info.pushConstantRangeCount = push_constant_bytes != 0 ? 1u : 0u;
            layout_info.pPushConstantRanges = push_constant_bytes != 0 ? &push_constant_range : nullptr;

            VkResult result = vkCreatePipelineLayout(key.device, &layout_info, nullptr, &state.layout);
            if (result != VK_SUCCESS)
            {
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   "vkCreatePipelineLayout failed for RasterizerPipeline. VkResult=" + std::to_string(static_cast<int>(result)));
                throw std::runtime_error("vkCreatePipelineLayout failed for RasterizerPipeline. VkResult=" + std::to_string(static_cast<int>(result)));
            }

            const std::string pipeline_name = desc_.debug_name.empty() ? "horizon.rasterizer_pipeline" : desc_.debug_name;
            name_vulkan_object(key.device,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                               reinterpret_cast<uint64_t>(state.layout),
                               pipeline_name + ".layout");

            VkPipelineRenderingCreateInfo rendering_info {};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.viewMask = desc_.multiview_count > 1 && desc_.multiview_count < 32
                ? ((uint32_t { 1 } << desc_.multiview_count) - 1u)
                : 0u;
            rendering_info.colorAttachmentCount = color_attachment_count;
            rendering_info.pColorAttachmentFormats = color_attachment_count != 0 ? key.color_formats.data() : nullptr;
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
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   "vkCreateGraphicsPipelines failed for RasterizerPipeline. VkResult=" + std::to_string(static_cast<int>(result)));
                vkDestroyPipelineLayout(key.device, state.layout, nullptr);
                state.layout = VK_NULL_HANDLE;
                for (PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
                {
                    if (set_layout.layout != VK_NULL_HANDLE)
                    {
                        vkDestroyDescriptorSetLayout(key.device, set_layout.layout, nullptr);
                        set_layout.layout = VK_NULL_HANDLE;
                    }
                }
                for (VkDescriptorSetLayout layout : state.empty_descriptor_set_layouts)
                {
                    if (layout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(key.device, layout, nullptr);
                }
                throw std::runtime_error("vkCreateGraphicsPipelines failed for RasterizerPipeline. VkResult=" + std::to_string(static_cast<int>(result)));
            }

            name_vulkan_object(key.device,
                               VK_OBJECT_TYPE_PIPELINE,
                               reinterpret_cast<uint64_t>(state.pipeline),
                               pipeline_name + ".pipeline");

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
                                                                                          const std::array<VkFormat, 4>& color_formats,
                                                                                          VkFormat depth_format,
                                                                                          uint32_t vertex_stride)
    {
        PipelineKey key {
            .device = device,
            .color_formats = color_formats,
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
            .uses_bindless = found->uses_bindless,
        };
    }

    VulkanRasterizerPipeline::PreparedDraw VulkanRasterizerPipeline::prepare_draw(VkDevice device,
                                                                                  const std::array<VkFormat, 4>& color_formats,
                                                                                  VkFormat depth_format,
                                                                                  uint32_t vertex_stride,
                                                                                  const DrawIndexedDesc& draw)
    {
        HORIZON_PROFILE_SCOPE_N("RasterizerPipeline::prepare_draw");

        PipelineKey key {
            .device = device,
            .color_formats = color_formats,
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

        PreparedDraw prepared {
            .layout = found->layout,
            .pipeline = found->pipeline,
            .uses_bindless = found->uses_bindless,
        };

        if (found->descriptor_set_layouts.empty())
            return prepared;

        auto descriptor_owner = std::make_shared<TransientDescriptorSet>();
        descriptor_owner->device = device;

        uint32_t uniform_buffer_count = 0;
        for (const PipelineState::DescriptorSetLayout& set_layout : found->descriptor_set_layouts)
        {
            for (const PipelineState::DescriptorBindingLayout& binding : set_layout.bindings)
            {
                if (binding.descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    ++uniform_buffer_count;
            }
        }

        if (uniform_buffer_count == 0)
            return prepared;

        std::vector<VkDescriptorPoolSize> pool_sizes;
        if (uniform_buffer_count != 0)
            pool_sizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_buffer_count });

        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = static_cast<uint32_t>(found->descriptor_set_layouts.size());
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        VkResult result = VK_SUCCESS;
        {
            HORIZON_PROFILE_SCOPE_N("prepare_draw::create_descriptor_pool");
            result = vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_owner->pool);
        }
        note_descriptor_pool_create();
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateDescriptorPool failed for RasterizerPipeline draw. VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }

        std::vector<VkDescriptorSetLayout> layouts;
        layouts.reserve(found->descriptor_set_layouts.size());
        for (const PipelineState::DescriptorSetLayout& set_layout : found->descriptor_set_layouts)
            layouts.push_back(set_layout.layout);

        std::vector<VkDescriptorSet> descriptor_sets(layouts.size(), VK_NULL_HANDLE);

        VkDescriptorSetAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_owner->pool;
        alloc_info.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        alloc_info.pSetLayouts = layouts.data();

        {
            HORIZON_PROFILE_SCOPE_N("prepare_draw::allocate_descriptor_sets");
            result = vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets.data());
        }
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkAllocateDescriptorSets failed for RasterizerPipeline draw. VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }

        prepared.descriptor_sets.reserve(found->descriptor_set_layouts.size());
        for (size_t index = 0; index < found->descriptor_set_layouts.size(); ++index)
        {
            prepared.descriptor_sets.push_back(
                {
                    .set = found->descriptor_set_layouts[index].set,
                    .descriptor_set = descriptor_sets[index],
                });
        }

        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkWriteDescriptorSet> writes;
        buffer_infos.reserve(uniform_buffer_count);
        writes.reserve(uniform_buffer_count);

        for (size_t set_index = 0; set_index < found->descriptor_set_layouts.size(); ++set_index)
        {
            const PipelineState::DescriptorSetLayout& set_layout = found->descriptor_set_layouts[set_index];
            VkDescriptorSet descriptor_set = descriptor_sets[set_index];
            for (const PipelineState::DescriptorBindingLayout& binding_layout : set_layout.bindings)
            {
                VkWriteDescriptorSet write {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptor_set;
                write.dstBinding = binding_layout.binding;
                write.dstArrayElement = 0;
                write.descriptorCount = 1;
                write.descriptorType = binding_layout.descriptor_type;

                if (binding_layout.descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                {
                    auto uniform = std::ranges::find_if(draw.uniform_buffers, [&](const UniformBufferBindingData& item) {
                        return item.set == binding_layout.set && item.binding == binding_layout.binding;
                    });
                    if (uniform == draw.uniform_buffers.end() || !uniform->gpu_buffer)
                        throw std::logic_error("RasterizerPipeline uniform buffer descriptor is missing persistent GPU buffer.");

                    // 直接使用持久 buffer，跳过 from_bytes VkBuffer 分配
                    BufferStore::Read buffer = read_buffer_resource(uniform->gpu_buffer);
                    if (!buffer || buffer->buffer_handle == VK_NULL_HANDLE)
                        throw std::logic_error("RasterizerPipeline uniform buffer descriptor requires a valid HardwareBuffer.");

                    VkDescriptorBufferInfo info {};
                    info.buffer = buffer->buffer_handle;
                    info.offset = 0;
                    info.range = buffer->logical_size();
                    buffer_infos.push_back(info);
                    write.pBufferInfo = &buffer_infos.back();
                    // gpu_buffer 由 pipeline / draw 持有，无需 descriptor_owner 延长 lifetime
                }
                else
                {
                    continue;
                }

                writes.push_back(write);
            }
        }

        {
            HORIZON_PROFILE_SCOPE_N("prepare_draw::update_descriptor_sets");
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        prepared.descriptor_set_lifetime = std::move(descriptor_owner);
        return prepared;
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
                                                            int32_t bind_type,
                                                            uint32_t set,
                                                            uint32_t binding)
    {
        if (size == 0)
            return;

        HORIZON_PROFILE_SCOPE_N("RasterizerPipeline::set_uniform");

        std::lock_guard lock(mutex_);
        if (is_push_constant_member(bind_type))
        {
            write_bytes(push_constant_data_, push_constant_size(), byte_offset, data, size, "RasterizerPipeline push constant");
            return;
        }

        if (is_uniform_buffer_member(bind_type))
        {
            write_uniform_member(uniform_buffers_, set, binding, byte_offset, data, size);

            // 原地 flush 到持久 GPU buffer（UBO 是批次公共数据，所有 draw 共享同一份，
            // 批次内重复写入直接替换 byte，无需隔离）
            auto it = std::ranges::find_if(uniform_buffers_, [&](const UniformBufferBindingData& u) {
                return u.set == set && u.binding == binding;
            });
            if (it == uniform_buffers_.end() && uniform_buffers_.size() == 1)
                it = uniform_buffers_.begin();
            if (it != uniform_buffers_.end())
            {
                const size_t idx = static_cast<size_t>(std::distance(uniform_buffers_.begin(), it));
                if (idx < ubo_buffers_.size() && ubo_buffers_[idx])
                    ubo_buffers_[idx].write_bytes(
                        std::span<const std::byte>(static_cast<const std::byte*>(data), size),
                        byte_offset);
            }
            return;
        }
    }

    void VulkanRasterizerPipeline::set_resource_direct(uint64_t byte_offset,
                                                       uint32_t type_size,
                                                       const HardwareBuffer& buffer,
                                                       int32_t bind_type,
                                                       uint32_t set,
                                                       uint32_t binding_index)
    {
        if (uses_bindless_descriptors(desc_) && is_storage_buffer_bind(bind_type))
        {
            const uint32_t descriptor_index = store_storage_buffer_descriptor(buffer);
            std::lock_guard lock(mutex_);
            write_descriptor_handle(push_constant_data_,
                                    push_constant_size(),
                                    byte_offset,
                                    type_size,
                                    descriptor_index,
                                    "RasterizerPipeline bindless storage buffer");

            auto found = std::ranges::find_if(bound_buffers_, [&](const BoundBuffer& bound) {
                return bound.byte_offset == byte_offset &&
                       bound.type_size == type_size &&
                       bound.bind_type == bind_type &&
                       bound.set == set &&
                       bound.binding == binding_index;
            });

            BoundBuffer binding {
                .byte_offset = byte_offset,
                .type_size = type_size,
                .bind_type = bind_type,
                .set = set,
                .binding = binding_index,
                .buffer = buffer,
            };

            if (found == bound_buffers_.end())
                bound_buffers_.push_back(std::move(binding));
            else
                *found = std::move(binding);
            return;
        }

        std::lock_guard lock(mutex_);

        auto found = std::ranges::find_if(bound_buffers_, [&](const BoundBuffer& bound) {
            return bound.byte_offset == byte_offset &&
                   bound.type_size == type_size &&
                   bound.bind_type == bind_type &&
                   bound.set == set &&
                   bound.binding == binding_index;
        });

        BoundBuffer binding {
            .byte_offset = byte_offset,
            .type_size = type_size,
            .bind_type = bind_type,
            .set = set,
            .binding = binding_index,
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
                                                       uint32_t location,
                                                       uint32_t set,
                                                       uint32_t binding_index)
    {
        if (uses_bindless_descriptors(desc_) && is_direct_resource_bind(bind_type) && !is_stage_output(bind_type))
        {
            uint32_t descriptor_index = 0;
            if (is_storage_image_bind(bind_type))
                descriptor_index = store_storage_image_descriptor(image);
            else if (is_sampled_image_bind(bind_type))
                descriptor_index = store_sampled_image_descriptor(image);
            else
                return;

            std::lock_guard lock(mutex_);
            write_descriptor_handle(push_constant_data_,
                                    push_constant_size(),
                                    byte_offset,
                                    type_size,
                                    descriptor_index,
                                    "RasterizerPipeline bindless image");

            auto found = std::ranges::find_if(bound_images_, [&](const BoundImage& bound) {
                if (is_stage_output(bind_type) || is_stage_output(bound.bind_type))
                    return bound.bind_type == bind_type && bound.location == location;

                return bound.byte_offset == byte_offset &&
                       bound.type_size == type_size &&
                       bound.bind_type == bind_type &&
                       bound.location == location &&
                       bound.set == set &&
                       bound.binding == binding_index;
            });

            BoundImage binding {
                .byte_offset = byte_offset,
                .type_size = type_size,
                .bind_type = bind_type,
                .location = location,
                .set = set,
                .binding = binding_index,
                .image = image,
            };

            if (found == bound_images_.end())
                bound_images_.push_back(std::move(binding));
            else
                *found = std::move(binding);
            return;
        }

        std::lock_guard lock(mutex_);

        auto found = std::ranges::find_if(bound_images_, [&](const BoundImage& bound) {
            if (is_stage_output(bind_type) || is_stage_output(bound.bind_type))
                return bound.bind_type == bind_type && bound.location == location;

            return bound.byte_offset == byte_offset &&
                   bound.type_size == type_size &&
                   bound.bind_type == bind_type &&
                   bound.location == location &&
                   bound.set == set &&
                   bound.binding == binding_index;
        });

        BoundImage binding {
            .byte_offset = byte_offset,
            .type_size = type_size,
            .bind_type = bind_type,
            .location = location,
            .set = set,
            .binding = binding_index,
            .image = image,
        };

        if (found == bound_images_.end())
            bound_images_.push_back(std::move(binding));
        else
            *found = std::move(binding);
    }

    void VulkanRasterizerPipeline::set_depth_target(const HardwareImage& image)
    {
        std::lock_guard lock(mutex_);
        depth_target_ = image;
    }

    void VulkanRasterizerPipeline::add_auto_bind_entry(EmbeddedShader::AutoBindEntry entry)
    {
        std::lock_guard lock(mutex_);
        auto found = std::ranges::find_if(desc_.auto_bind_entries, [&](const EmbeddedShader::AutoBindEntry& existing) {
            return existing.boundResourceRef == entry.boundResourceRef &&
                   existing.bindType == entry.bindType &&
                   existing.location == entry.location;
        });

        if (found == desc_.auto_bind_entries.end())
            desc_.auto_bind_entries.push_back(std::move(entry));
        else
            *found = std::move(entry);
    }

    void VulkanRasterizerPipeline::bind_auto_resources()
    {
        struct AutoValue
        {
            EmbeddedShader::AutoBindEntry entry;
            BindingCoordinates coordinates;
        };

        std::vector<std::pair<EmbeddedShader::AutoBindEntry, HardwareImage>> images;
        std::vector<AutoValue> values;
        {
            std::lock_guard lock(mutex_);
            images.reserve(desc_.auto_bind_entries.size());
            values.reserve(desc_.auto_bind_entries.size());
            for (const EmbeddedShader::AutoBindEntry& entry : desc_.auto_bind_entries)
            {
                if (entry.boundResourceRef != nullptr && *entry.boundResourceRef != nullptr)
                {
                    images.push_back({ entry, *static_cast<HardwareImage*>(*entry.boundResourceRef) });
                    continue;
                }

                if (entry.boundValueRef != nullptr && entry.boundValueSize != 0)
                {
                    values.push_back({
                        entry,
                        reflected_binding_coordinates(desc_, entry),
                    });
                }
            }
        }

        for (const auto& [entry, image] : images)
        {
            set_resource_direct(entry.byteOffset,
                                entry.typeSize,
                                image,
                                entry.bindType,
                                entry.location,
                                0,
                                entry.location);
        }

        for (const AutoValue& value : values)
        {
            set_push_constant_direct(value.entry.byteOffset,
                                     value.entry.boundValueRef,
                                     value.entry.boundValueSize,
                                     value.entry.bindType,
                                     value.coordinates.set,
                                     value.coordinates.binding);
        }
    }

    std::vector<EmbeddedShader::AutoBindEntry> VulkanRasterizerPipeline::auto_bind_entries() const
    {
        std::lock_guard lock(mutex_);
        return desc_.auto_bind_entries;
    }

    void VulkanRasterizerPipeline::record(RasterizerPipelineBase* pipeline,
                                          const HardwareBuffer& index_buffer,
                                          const HardwareBuffer& vertex_buffer,
                                          const DrawIndexedParams& params)
    {
        if (!validate_rasterizer_pipeline_record(index_buffer, vertex_buffer, params))
            return;

        HORIZON_PROFILE_SCOPE_N("RasterizerPipeline::record");

        RecordedDraw draw;
        draw.pipeline = pipeline;
        draw.index_buffer = index_buffer;
        draw.vertex_buffer = vertex_buffer;
        draw.params = normalize_draw_params(index_buffer, params);

        std::lock_guard lock(mutex_);
        draw.push_constant_data = push_constant_data_;
        // UBO 是批次公共数据，所有 draw 共享同一持久 buffer（gpu_buffer 句柄浅拷贝）。
        // 批次内写入直接原地替换 byte，无需隔离副本。
        draw.uniform_buffers = uniform_buffers_;

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
            .depth_target = depth_target_,
            .draws = draws_,
        };
    }

    CommandBatch VulkanRasterizerPipeline::command_batch() const
    {
        HORIZON_PROFILE_SCOPE_N("RasterizerPipeline::command_batch");

        Snapshot state = snapshot();
        CommandBatch batch;

        std::vector<const BoundImage*> color_outputs;
        for (const BoundImage& image : state.images)
        {
            if (!is_stage_output(image.bind_type) || !image.image)
                continue;
            color_outputs.push_back(&image);
        }
        std::sort(color_outputs.begin(), color_outputs.end(),
                  [](const BoundImage* a, const BoundImage* b) { return a->location < b->location; });

        const bool has_rendering_scope = !color_outputs.empty() && state.width != 0 && state.height != 0;
        if (has_rendering_scope)
        {
            const bool has_depth = static_cast<bool>(state.depth_target);
            RenderingDesc rendering_desc;
            rendering_desc.color = image_ref(color_outputs[0]->image);
            for (size_t i = 1; i < color_outputs.size() && i < 4; ++i)
                rendering_desc.extra_colors[i - 1] = image_ref(color_outputs[i]->image);
            rendering_desc.depth = has_depth ? image_ref(state.depth_target) : ImageRef {};
            rendering_desc.width = state.width;
            rendering_desc.height = state.height;
            rendering_desc.clear_color = state.desc.clear_color_target;
            rendering_desc.clear_depth = has_depth;
            batch << begin_rendering(rendering_desc);
        }

        for (const RecordedDraw& draw : state.draws)
        {
            DrawIndexedDesc draw_desc = to_draw_desc(draw.params);
            //ResourceBridge::set(draw_desc.pipeline, draw.pipeline.lock());
            draw_desc.pipeline = draw.pipeline;
            auto pipelineDesc = desc();
            if (auto object = pipelineDesc.pipelineObject; object)
            {
                draw_desc.vert_condition_info = object->vertex->getCurrentConditionInfo();
                draw_desc.frag_condition_info = object->fragment->getCurrentConditionInfo();
            }
            draw_desc.push_constant_data = draw.push_constant_data;
            draw_desc.uniform_buffers = draw.uniform_buffers;

            batch << draw_indexed(buffer_ref(draw.index_buffer),
                                  buffer_ref(draw.vertex_buffer),
                                  std::move(draw_desc));
        }

        if (has_rendering_scope)
            batch << end_rendering();

        return batch;
    }

    void VulkanRasterizerPipeline::record_consuming(CommandRecorder& recorder)
    {
        HORIZON_PROFILE_SCOPE_N("RasterizerPipeline::record_consuming");

        uint32_t width = 0;
        uint32_t height = 0;
        bool clear_color_target = true;
        std::vector<BoundImage> images;
        HardwareImage depth_target;
        std::vector<RecordedDraw> draws;
        {
            std::lock_guard lock(mutex_);
            width = width_;
            height = height_;
            clear_color_target = desc_.clear_color_target;
            images = bound_images_;
            depth_target = depth_target_;
            draws = std::move(draws_);
            draws_.clear();
        }

        std::vector<const BoundImage*> color_outputs;
        for (const BoundImage& image : images)
        {
            if (!is_stage_output(image.bind_type) || !image.image)
                continue;
            color_outputs.push_back(&image);
        }
        std::sort(color_outputs.begin(), color_outputs.end(),
                  [](const BoundImage* a, const BoundImage* b) { return a->location < b->location; });

        const bool has_rendering_scope = !color_outputs.empty() && width != 0 && height != 0;
        if (has_rendering_scope)
        {
            const bool has_depth = static_cast<bool>(depth_target);
            RenderingDesc rendering_desc;
            rendering_desc.color = image_ref(color_outputs[0]->image);
            for (size_t i = 1; i < color_outputs.size() && i < 4; ++i)
                rendering_desc.extra_colors[i - 1] = image_ref(color_outputs[i]->image);
            rendering_desc.depth = has_depth ? image_ref(depth_target) : ImageRef {};
            rendering_desc.width = width;
            rendering_desc.height = height;
            rendering_desc.clear_color = clear_color_target;
            rendering_desc.clear_depth = has_depth;
            recorder.begin_rendering(rendering_desc);
        }

        DrawIndexedBatchDesc batch;
        batch.draws.reserve(draws.size());
        for (RecordedDraw& draw : draws)
        {
            DrawIndexedDesc draw_desc = to_draw_desc(draw.params);
            draw_desc.debug_label = std::move(draw.params.debug_label);
            //ResourceBridge::set(draw_desc.pipeline, draw.pipeline.lock());
            draw_desc.pipeline = draw.pipeline;
            draw_desc.push_constant_data = std::move(draw.push_constant_data);
            draw_desc.uniform_buffers = std::move(draw.uniform_buffers);
            batch.draws.push_back({
                .index = buffer_ref(draw.index_buffer),
                .vertex = buffer_ref(draw.vertex_buffer),
                .draw = std::move(draw_desc),
            });
        }
        recorder.draw_indexed_batch(std::move(batch));

        if (has_rendering_scope)
            recorder.end_rendering();
    }
}
