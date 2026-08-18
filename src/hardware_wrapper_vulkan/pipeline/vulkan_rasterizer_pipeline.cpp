#include "vulkan_rasterizer_pipeline.h"

#include "horizon_profiling.h"

#include "hardware_wrapper/validation/hardware_validation.h"
#include "hardware_wrapper_vulkan/frame_ring.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "hardware_wrapper_vulkan/hardware/execution_profile.h"
#include "hardware_wrapper_vulkan/resource_pool.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
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

        [[nodiscard]] BindingCoordinates reflected_binding_coordinates(const RasterizerPipelineShaders& shaders,
                                                                       const EmbeddedShader::AutoBindEntry& entry) noexcept
        {
            BindingCoordinates coordinates = reflected_binding_coordinates(shaders.vertex, entry);
            if (coordinates.set != 0 || coordinates.binding != 0)
                return coordinates;

            return reflected_binding_coordinates(shaders.fragment, entry);
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
            if ((mask & ColorWrite_R) == ColorWrite_R)
                flags |= VK_COLOR_COMPONENT_R_BIT;
            if ((mask & ColorWrite_G) == ColorWrite_G)
                flags |= VK_COLOR_COMPONENT_G_BIT;
            if ((mask & ColorWrite_B) == ColorWrite_B)
                flags |= VK_COLOR_COMPONENT_B_BIT;
            if ((mask & ColorWrite_A) == ColorWrite_A)
                flags |= VK_COLOR_COMPONENT_A_BIT;
            return flags;
        }

        [[nodiscard]] VkPipelineColorBlendAttachmentState to_vk_blend_attachment(const RasterizerPipelineDesc& desc) noexcept
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

        [[nodiscard]] bool uses_bindless_descriptors(const RasterizerPipelineShaders& shaders) noexcept
        {
            return uses_bindless_descriptors(shaders.vertex) ||
                   uses_bindless_descriptors(shaders.fragment);
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

        [[nodiscard]] std::vector<UniformBufferBindingData> reflected_uniform_buffers(const RasterizerPipelineShaders& shaders)
        {
            std::vector<UniformBufferBindingData> buffers;
            append_reflected_uniform_buffers(buffers, shaders.vertex);
            append_reflected_uniform_buffers(buffers, shaders.fragment);
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
            desc.instance_count = params.instance_count;
            desc.first_index = params.first_index;
            desc.vertex_offset = params.vertex_offset;
            desc.first_instance = params.first_instance;
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

    }

    VulkanRasterizerPipeline::VulkanRasterizerPipeline(RasterizerPipelineDesc desc,
                                                       RasterizerPipelineShaders shaders,
                                                       std::source_location source_location)
        : desc_(std::move(desc)),
          shaders_(std::move(shaders)),
          source_location_(source_location)
    {
        (void)validate_rasterizer_pipeline_desc(desc_, shaders_);

        const uint32_t constant_size = push_constant_size();
        if (constant_size != 0)
            push_constant_data_.resize(constant_size);
        uniform_buffers_ = reflected_uniform_buffers(shaders_);
        // UBO 环在首次 sync_ubo_slot_unlocked() 时惰性创建：构造这里交换链可能
        // 还没建好，frame_ring_size() 还是 1。
        ubo_rings_.resize(uniform_buffers_.size());
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

    RasterizerPipelineShaders VulkanRasterizerPipeline::shaders() const
    {
        std::lock_guard lock(mutex_);
        return shaders_;
    }

    uint32_t VulkanRasterizerPipeline::push_constant_size() const noexcept
    {
        return std::max(shaders_.vertex.shaderResources.pushConstantSize,
                        shaders_.fragment.shaderResources.pushConstantSize);
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
                    // 销毁 pool 即隐式释放其中的 descriptor set。
                    for (PipelineState::UniformDescriptorSet& uniform_set : set_layout.uniform_sets)
                    {
                        if (uniform_set.pool != VK_NULL_HANDLE)
                            vkDestroyDescriptorPool(state.key.device, uniform_set.pool, nullptr);
                    }
                    set_layout.uniform_sets.clear();

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

        VkShaderModule vertex_shader = create_shader_module(key.device, shaders_.vertex, "vertex");
        VkShaderModule fragment_shader = VK_NULL_HANDLE;

        try
        {
            fragment_shader = create_shader_module(key.device, shaders_.fragment, "fragment");

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
            for (const auto& input : shaders_.vertex.shaderResources.bindInfoPool)
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
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewport_state {};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterization {};
            rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterization.depthClampEnable = desc_.depth_clamp_enabled ? VK_TRUE : VK_FALSE;
            rasterization.rasterizerDiscardEnable = desc_.rasterizer_discard_enabled ? VK_TRUE : VK_FALSE;
            rasterization.polygonMode = VK_POLYGON_MODE_FILL;
            rasterization.cullMode = VK_CULL_MODE_NONE;
            rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterization.depthBiasEnable = VK_FALSE;
            rasterization.lineWidth = desc_.line_width;

            VkPipelineMultisampleStateCreateInfo multisample {};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = to_vk_sample_count(desc_.sample_count);
            multisample.sampleShadingEnable = desc_.sample_shading_enabled ? VK_TRUE : VK_FALSE;
            multisample.minSampleShading = desc_.min_sample_shading;

            VkPipelineDepthStencilStateCreateInfo depth_stencil {};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = desc_.depth_test_enabled && key.depth_format != VK_FORMAT_UNDEFINED ? VK_TRUE : VK_FALSE;
            depth_stencil.depthWriteEnable = desc_.depth_write_enabled && key.depth_format != VK_FORMAT_UNDEFINED ? VK_TRUE : VK_FALSE;
            depth_stencil.depthCompareOp = to_vk_compare_op(desc_.depth_compare_op);
            depth_stencil.depthBoundsTestEnable = VK_FALSE;
            // 模板恒关：rendering_info.stencilAttachmentFormat 恒为 UNDEFINED，
            // 开启模板测试而无模板附件是无意义的（且会触发验证层告警）。
            depth_stencil.stencilTestEnable = VK_FALSE;

            VkPipelineColorBlendAttachmentState blend_attachment = to_vk_blend_attachment(desc_);

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
            color_blend.logicOpEnable = desc_.logic_op_enabled ? VK_TRUE : VK_FALSE;
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
            state.uses_bindless = uses_bindless_descriptors(shaders_);

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

            for (const UniformBufferBindingData& uniform_buffer : reflected_uniform_buffers(shaders_))
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
                // 该 set 仅承载 UBO。UBO 是批次共享的持久 buffer，(binding, buffer, range)
                // 在管线生命周期内恒定，值的变化走 mapped 内存写入，所以按 bindless set
                // 0-2 的同一形式处理：分配一次普通 descriptor set、写入一次、之后只 bind。
                descriptor_layout_info.flags = 0;
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

    VkDescriptorSet VulkanRasterizerPipeline::uniform_descriptor_set_unlocked(
        VkDevice device,
        PipelineState::DescriptorSetLayout& set_layout,
        const std::vector<PipelineState::UniformDescriptorSet::Signature>& signature,
        const std::string& debug_name)
    {
        auto found = std::ranges::find_if(set_layout.uniform_sets, [&](const PipelineState::UniformDescriptorSet& candidate) {
            return candidate.signature == signature;
        });
        if (found != set_layout.uniform_sets.end())
            return found->descriptor_set;

        // 签名未见过：新分配一份并写入一次。已分配的 set 从不被重写，因为 in-flight
        // 命令缓冲可能仍绑定着它。稳态下每个 set layout 有 N 份（N = 帧环长 = 交换链
        // 图像数，每个 UBO 帧槽一个 buffer 句柄 = 一个签名），换交换链补齐环长后再多
        // 出几份；rebuild_pipeline 换实现时也会多出。上限仅作兜底。
        constexpr size_t max_uniform_sets_per_layout = 64;
        if (set_layout.uniform_sets.size() >= max_uniform_sets_per_layout)
        {
            throw std::runtime_error("RasterizerPipeline uniform descriptor set signatures exceeded " +
                                     std::to_string(max_uniform_sets_per_layout) + " for one set layout.");
        }

        PipelineState::UniformDescriptorSet allocated;
        allocated.signature = signature;

        VkDescriptorPoolSize pool_size {};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = static_cast<uint32_t>(signature.size());

        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;

        VkResult pool_result = vkCreateDescriptorPool(device, &pool_info, nullptr, &allocated.pool);
        if (pool_result != VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateDescriptorPool failed for RasterizerPipeline uniform set. VkResult=" +
                                     std::to_string(static_cast<int>(pool_result)));
        }

        try
        {
            VkDescriptorSetAllocateInfo alloc_info {};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool = allocated.pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts = &set_layout.layout;

            VkResult alloc_result = vkAllocateDescriptorSets(device, &alloc_info, &allocated.descriptor_set);
            if (alloc_result != VK_SUCCESS)
            {
                throw std::runtime_error("vkAllocateDescriptorSets failed for RasterizerPipeline uniform set. VkResult=" +
                                         std::to_string(static_cast<int>(alloc_result)));
            }

            std::vector<VkDescriptorBufferInfo> buffer_infos;
            std::vector<VkWriteDescriptorSet> writes;
            buffer_infos.reserve(signature.size());
            writes.reserve(signature.size());
            for (const PipelineState::UniformDescriptorSet::Signature& entry : signature)
            {
                buffer_infos.push_back(VkDescriptorBufferInfo {
                    .buffer = entry.buffer,
                    .offset = 0,
                    .range = entry.range,
                });
            }
            for (size_t i = 0; i < signature.size(); ++i)
            {
                VkWriteDescriptorSet write {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = allocated.descriptor_set;
                write.dstBinding = signature[i].binding;
                write.dstArrayElement = 0;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.pBufferInfo = &buffer_infos[i];
                writes.push_back(write);
            }

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

            name_vulkan_object(device,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET,
                               reinterpret_cast<uint64_t>(allocated.descriptor_set),
                               debug_name + ".uniform_set" + std::to_string(set_layout.set));
        }
        catch (...)
        {
            vkDestroyDescriptorPool(device, allocated.pool, nullptr);
            throw;
        }

        set_layout.uniform_sets.push_back(std::move(allocated));
        return set_layout.uniform_sets.back().descriptor_set;
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

        // 非 bindless set 仅含 UBO（纹理已全走 bindless）。按 (binding, buffer, range)
        // 签名取回持久 descriptor set：签名不变就是同一个句柄，execution 侧因而能像
        // bindless set 0-2 那样只在首次 / layout 变更时 bind 一次。
        const std::string debug_name = desc_.debug_name.empty() ? "horizon.rasterizer_pipeline" : desc_.debug_name;
        std::vector<PipelineState::UniformDescriptorSet::Signature> signature;
        for (PipelineState::DescriptorSetLayout& set_layout : found->descriptor_set_layouts)
        {
            signature.clear();
            for (const PipelineState::DescriptorBindingLayout& binding_layout : set_layout.bindings)
            {
                if (binding_layout.descriptor_type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    continue;

                const std::vector<UniformBufferBindingData>& draw_uniforms = draw.uniform_buffers();
                auto uniform = std::ranges::find_if(draw_uniforms, [&](const UniformBufferBindingData& item) {
                    return item.set == binding_layout.set && item.binding == binding_layout.binding;
                });
                if (uniform == draw_uniforms.end() || !uniform->gpu_buffer)
                    throw std::logic_error("RasterizerPipeline uniform buffer descriptor is missing persistent GPU buffer.");

                // 直接使用持久 buffer，跳过 from_bytes VkBuffer 分配
                BufferStore::Read buffer = read_buffer_resource(uniform->gpu_buffer);
                if (!buffer || buffer->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("RasterizerPipeline uniform buffer descriptor requires a valid HardwareBuffer.");

                signature.push_back({
                    .binding = binding_layout.binding,
                    .buffer = buffer->buffer_handle,
                    .range = buffer->logical_size(),
                });
                // gpu_buffer 由 pipeline / draw 持有，无需延长 lifetime
            }

            if (signature.empty())
                continue;

            prepared.uniform_sets.push_back({
                .set = set_layout.set,
                .descriptor_set = uniform_descriptor_set_unlocked(device, set_layout, signature, debug_name),
            });
        }

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
            push_constant_snapshot_.reset();
            return;
        }

        if (is_uniform_buffer_member(bind_type))
        {
            // 只写 CPU 影子并置脏。真正落到 GPU buffer 的时机是本帧构建批次时
            // （build_draw_plan），那里才知道该写哪个帧槽。
            write_uniform_member(uniform_buffers_, set, binding, byte_offset, data, size);
            ubo_dirty_ = true;
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
        if (uses_bindless_descriptors(shaders_) && is_storage_buffer_bind(bind_type))
        {
            const uint32_t descriptor_index = store_storage_buffer_descriptor(buffer);
            std::lock_guard lock(mutex_);
            write_descriptor_handle(push_constant_data_,
                                    push_constant_size(),
                                    byte_offset,
                                    type_size,
                                    descriptor_index,
                                    "RasterizerPipeline bindless storage buffer");
            push_constant_snapshot_.reset();

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
        if (uses_bindless_descriptors(shaders_) && is_direct_resource_bind(bind_type) && !is_stage_output(bind_type))
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
            push_constant_snapshot_.reset();

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

            // 绝大多数 draw 没有任何 entry 变脏。先扫一遍再决定是否建容器，
            // 否则每次 record() 都要为两个空 vector 各付一次堆分配。
            bool any_dirty = false;
            for (const EmbeddedShader::AutoBindEntry& entry : shaders_.auto_bind_entries)
            {
                if (*entry.dirtyVersion != entry.currentDirtyVersion)
                {
                    any_dirty = true;
                    break;
                }
            }
            if (!any_dirty)
                return;

            images.reserve(shaders_.auto_bind_entries.size());
            values.reserve(shaders_.auto_bind_entries.size());
            for (EmbeddedShader::AutoBindEntry& entry : shaders_.auto_bind_entries)
            {
                if (*entry.dirtyVersion != entry.currentDirtyVersion)
                {
                    if (entry.boundResourceRef != nullptr && *entry.boundResourceRef != nullptr)
                    {
                        images.push_back({ entry, *static_cast<HardwareImage*>(*entry.boundResourceRef) });
                    }
                    else if (entry.boundValueRef != nullptr && entry.boundValueSize != 0)
                    {
                        values.push_back({
                            entry,
                            reflected_binding_coordinates(shaders_, entry),
                        });
                    }
                    entry.currentDirtyVersion = *entry.dirtyVersion;
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
        return shaders_.auto_bind_entries;
    }

    std::shared_ptr<const std::vector<std::byte>> VulkanRasterizerPipeline::push_constant_snapshot_unlocked()
    {
        if (!push_constant_snapshot_)
            push_constant_snapshot_ = std::make_shared<const std::vector<std::byte>>(push_constant_data_);
        return push_constant_snapshot_;
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
        // 自上次 record 后没写过 push constant 时，这里连拷贝都不做。
        draw.push_constant_data = push_constant_snapshot_unlocked();
        // UBO 不再逐 draw 拷贝：它是批次公共数据，批次构建时统一取本帧槽的句柄。
        draws_.push_back(std::move(draw));
    }

    void VulkanRasterizerPipeline::record(const HardwareBuffer& index_buffer,
                                          const HardwareBuffer& vertex_buffer,
                                          const DrawIndexedParams& params)
    {
        record({}, index_buffer, vertex_buffer, params);
    }

    void VulkanRasterizerPipeline::record_indirect(RasterizerPipelineBase* pipeline,
                                                   const HardwareBuffer& index_buffer,
                                                   const HardwareBuffer& vertex_buffer,
                                                   const HardwareBuffer& indirect_buffer,
                                                   const DrawIndexedIndirectParams& params)
    {
        HORIZON_PROFILE_SCOPE_N("RasterizerPipeline::record_indirect");

        RecordedIndirectDraw draw;
        draw.pipeline = pipeline;
        draw.index_buffer = index_buffer;
        draw.vertex_buffer = vertex_buffer;
        draw.indirect_buffer = indirect_buffer;
        draw.params = params;

        std::lock_guard lock(mutex_);
        draw.push_constant_data = push_constant_snapshot_unlocked();
        indirect_draws_.push_back(std::move(draw));
    }

    void VulkanRasterizerPipeline::clear_records()
    {
        std::lock_guard lock(mutex_);
        draws_.clear();
        indirect_draws_.clear();
    }

    void VulkanRasterizerPipeline::sync_ubo_slot_unlocked() const
    {
        if (uniform_buffers_.empty())
            return;

        const uint32_t ring_size = std::max(1u, frame_ring_size());
        const int64_t slot = static_cast<int64_t>(frame_ring_slot() % ring_size);

        if (ubo_rings_.size() != uniform_buffers_.size())
            ubo_rings_.resize(uniform_buffers_.size());

        // 槽没换且影子没脏 —— gpu_buffer 已经指向正确的 buffer，不用重写。
        bool ring_ready = true;
        for (size_t i = 0; i < uniform_buffers_.size(); ++i)
        {
            if (uniform_buffers_[i].data.empty())
                continue;
            if (ubo_rings_[i].size() < ring_size)
            {
                ring_ready = false;
                break;
            }
        }
        if (ring_ready && !ubo_dirty_ && slot == ubo_slot_)
            return;

        for (size_t i = 0; i < uniform_buffers_.size(); ++i)
        {
            UniformBufferBindingData& ubo = uniform_buffers_[i];
            if (ubo.data.empty())
                continue;

            std::vector<HardwareBuffer>& ring = ubo_rings_[i];
            // 环只增不减：交换链重建后 N 变大时补齐，已有的 buffer 可能仍被在飞的
            // 命令缓冲引用，不能重建。
            while (ring.size() < ring_size)
                ring.push_back(HardwareBuffer::from_bytes(
                    std::span<const std::byte>(ubo.data),
                    1, BufferUsage_Uniform, "RasterizerPipeline.ubo_persistent"));

            HardwareBuffer& target = ring[static_cast<size_t>(slot)];
            // 换槽时必须整份写：该槽上一次被写的是第 i-N 帧的数据。
            (void)target.write_bytes(std::span<const std::byte>(ubo.data), 0);
            ubo.gpu_buffer = target;
        }

        ubo_slot_ = slot;
        ubo_dirty_ = false;
    }

    VulkanRasterizerPipeline::DrawPlan VulkanRasterizerPipeline::build_draw_plan() const
    {
        // 只在锁内取真正需要的状态。原先走 snapshot()，它会把 draws_ 整份深拷贝
        // 一遍（每 draw 两个 vector），压测下是纯浪费；这里直接在锁内构建批次。
        uint32_t width = 0;
        uint32_t height = 0;
        bool clear_color_target = true;
        bool clear_depth_target = true;
        std::vector<BoundImage> images;
        HardwareImage depth_target;
        DrawPlan plan;
        {
            std::lock_guard lock(mutex_);
            width = width_;
            height = height_;
            clear_color_target = desc_.clear_color_target;
            clear_depth_target = desc_.clear_depth_target;
            images = bound_images_;
            depth_target = depth_target_;

            // 本帧槽的 UBO 落盘一次，之后所有 draw 共享同一批句柄。
            sync_ubo_slot_unlocked();
            // 整批共享一份公共载荷（条件信息 + 本帧 UBO 句柄）。原先逐 draw 各拷
            // 三个 vector；现在整批建一次，各 draw 只持一个 shared_ptr。
            auto shared_payload = std::make_shared<DrawSharedPayload>();
            shared_payload->uniform_buffers.reserve(uniform_buffers_.size());
            for (const UniformBufferBindingData& ubo : uniform_buffers_)
            {
                if (ubo.data.empty())
                    continue;
                shared_payload->uniform_buffers.push_back(
                    { .set = ubo.set, .binding = ubo.binding, .data = {}, .gpu_buffer = ubo.gpu_buffer });
            }

            if (const auto& object = shaders_.object; object)
            {
                shared_payload->vert_condition_info = object->vertex->getCurrentConditionInfo();
                shared_payload->frag_condition_info = object->fragment->getCurrentConditionInfo();
            }

            std::shared_ptr<const DrawSharedPayload> shared = std::move(shared_payload);

            plan.batch.draws.reserve(draws_.size());
            for (const RecordedDraw& draw : draws_)
            {
                DrawIndexedDesc draw_desc = to_draw_desc(draw.params);
                draw_desc.pipeline = draw.pipeline;
                draw_desc.shared = shared;
                draw_desc.push_constants = draw.push_constant_data;

                plan.batch.draws.push_back({
                    .index = buffer_ref(draw.index_buffer),
                    .vertex = buffer_ref(draw.vertex_buffer),
                    .draw = std::move(draw_desc),
                });
            }

            plan.indirect_draws.reserve(indirect_draws_.size());
            for (const RecordedIndirectDraw& draw : indirect_draws_)
            {
                DrawIndexedIndirectDesc indirect_desc;
                indirect_desc.pipeline = draw.pipeline;
                indirect_desc.shared = shared;
                indirect_desc.indirect_offset = draw.params.indirect_offset;
                indirect_desc.draw_count = draw.params.draw_count;
                indirect_desc.stride = draw.params.stride;
                indirect_desc.index_type = draw.params.index_type;
                indirect_desc.enable_scissor = draw.params.enable_scissor;
                indirect_desc.scissor = draw.params.scissor;
                indirect_desc.push_constants = draw.push_constant_data;
                indirect_desc.debug_label = draw.params.debug_label;

                plan.indirect_draws.emplace_back(buffer_ref(draw.index_buffer),
                                                 buffer_ref(draw.vertex_buffer),
                                                 buffer_ref(draw.indirect_buffer),
                                                 std::move(indirect_desc));
            }
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

        plan.has_rendering_scope = !color_outputs.empty() && width != 0 && height != 0;
        if (plan.has_rendering_scope)
        {
            const bool has_depth = static_cast<bool>(depth_target);
            plan.rendering.color = image_ref(color_outputs[0]->image);
            for (size_t i = 1; i < color_outputs.size() && i < 4; ++i)
                plan.rendering.extra_colors[i - 1] = image_ref(color_outputs[i]->image);
            plan.rendering.depth = has_depth ? image_ref(depth_target) : ImageRef {};
            plan.rendering.width = width;
            plan.rendering.height = height;
            plan.rendering.clear_color = clear_color_target;
            plan.rendering.clear_depth = has_depth && clear_depth_target;
        }

        return plan;
    }

    void VulkanRasterizerPipeline::record_into(CommandRecorder& recorder) const
    {
        HORIZON_PROFILE_SCOPE_N("RasterizerPipeline::record_into");

        DrawPlan plan = build_draw_plan();

        if (plan.has_rendering_scope)
            recorder.begin_rendering(plan.rendering);

        if (!plan.batch.draws.empty())
            recorder.draw_indexed_batch(std::move(plan.batch));
        for (auto& [index, vertex, indirect, draw] : plan.indirect_draws)
            recorder.draw_indexed_indirect(index, vertex, indirect, std::move(draw));

        if (plan.has_rendering_scope)
            recorder.end_rendering();
    }

}
