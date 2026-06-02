#include "vulkan_compute_pipeline.h"

#include "hardware_wrapper_vulkan/resource_pool.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace Corona::Horizon
{
    namespace
    {
        using BindType = EmbeddedShader::ShaderCodeModule::ShaderResources::BindType;
        using BufferStore = ResourceStore<BufferWrap, BufferReleaser>;
        using ImageStore = ResourceStore<ImageWrap, ImageReleaser>;

        [[nodiscard]] bool is_push_constant_member(int32_t bind_type) noexcept
        {
            return bind_type == static_cast<int32_t>(BindType::pushConstantMembers);
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

        [[nodiscard]] bool add_overflows(uint64_t lhs, size_t rhs) noexcept
        {
            if constexpr (sizeof(size_t) > sizeof(uint64_t))
            {
                if (rhs > std::numeric_limits<uint64_t>::max())
                    return true;
            }

            return lhs > std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(rhs);
        }

        [[nodiscard]] VkDescriptorType descriptor_type(DispatchBindingKind kind) noexcept
        {
            switch (kind)
            {
            case DispatchBindingKind::StorageImage:
                return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case DispatchBindingKind::StorageBuffer:
            default:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }
        }

        [[nodiscard]] BufferStore::Read read_buffer(const ResourceHandle& handle)
        {
            return read<BufferStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] ImageStore::Read read_image(const ResourceHandle& handle)
        {
            return read<ImageStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] VkShaderModule create_shader_module(VkDevice device, const EmbeddedShader::ShaderCodeModule& module)
        {
            if (!std::holds_alternative<std::vector<uint32_t>>(module.shaderCode))
            {
                throw std::logic_error("ComputePipeline shader must contain SPIR-V for Vulkan pipeline creation.");
            }

            const std::vector<uint32_t>& code = std::get<std::vector<uint32_t>>(module.shaderCode);
            if (code.empty())
            {
                throw std::logic_error("ComputePipeline shader SPIR-V is empty.");
            }

            VkShaderModuleCreateInfo create_info {};
            create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            create_info.codeSize = code.size() * sizeof(uint32_t);
            create_info.pCode = code.data();

            VkShaderModule shader = VK_NULL_HANDLE;
            VkResult result = vkCreateShaderModule(device, &create_info, nullptr, &shader);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("vkCreateShaderModule failed for ComputePipeline. VkResult=" +
                                         std::to_string(static_cast<int>(result)));
            }

            return shader;
        }

        [[nodiscard]] std::vector<VulkanComputePipeline::BindingLayout> binding_layout_key(const DispatchDesc& dispatch)
        {
            std::vector<VulkanComputePipeline::BindingLayout> key;
            key.reserve(dispatch.bindings.size());
            for (const DispatchResourceBinding& binding : dispatch.bindings)
            {
                key.push_back({ binding.binding, binding.kind });
            }

            std::ranges::sort(key, [](const auto& left, const auto& right) {
                if (left.binding != right.binding)
                    return left.binding < right.binding;
                return static_cast<int>(left.kind) < static_cast<int>(right.kind);
            });

            auto duplicate = std::ranges::unique(key, [](const auto& left, const auto& right) {
                return left.binding == right.binding && left.kind == right.kind;
            });
            key.erase(duplicate.begin(), duplicate.end());

            for (size_t index = 1; index < key.size(); ++index)
            {
                if (key[index - 1].binding == key[index].binding)
                {
                    throw std::logic_error("ComputePipeline cannot bind different descriptor kinds to the same binding.");
                }
            }

            return key;
        }

        struct TransientDescriptorSet
        {
            VkDevice device { VK_NULL_HANDLE };
            VkDescriptorPool pool { VK_NULL_HANDLE };

            ~TransientDescriptorSet()
            {
                if (device != VK_NULL_HANDLE && pool != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(device, pool, nullptr);
                }
            }
        };
    }

    VulkanComputePipeline::VulkanComputePipeline(ComputePipelineDesc desc,
                                                 std::source_location source_location)
        : desc_(std::move(desc)),
          source_location_(source_location)
    {
        if (desc_.compute_shader.stage != PipelineShaderStage::Compute)
        {
            throw std::invalid_argument("VulkanComputePipeline requires a compute shader.");
        }

        dispatch_.groups_x = 1;
        dispatch_.groups_y = 1;
        dispatch_.groups_z = 1;

        const uint32_t constant_size = push_constant_size();
        if (constant_size != 0)
            push_constant_data_.resize(constant_size);
    }

    VulkanComputePipeline::~VulkanComputePipeline()
    {
        std::lock_guard lock(mutex_);
        destroy_pipeline_cache_unlocked();
    }

    ComputePipelineDesc VulkanComputePipeline::desc() const
    {
        std::lock_guard lock(mutex_);
        return desc_;
    }

    uint32_t VulkanComputePipeline::push_constant_size() const noexcept
    {
        return desc_.compute_shader.module.shaderResources.pushConstantSize;
    }

    void VulkanComputePipeline::destroy_pipeline_cache_unlocked() noexcept
    {
        for (PipelineState& state : pipeline_cache_)
        {
            if (state.device == VK_NULL_HANDLE)
            {
                continue;
            }

            if (state.pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(state.device, state.pipeline, nullptr);
                state.pipeline = VK_NULL_HANDLE;
            }

            if (state.layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(state.device, state.layout, nullptr);
                state.layout = VK_NULL_HANDLE;
            }

            if (state.descriptor_set_layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(state.device, state.descriptor_set_layout, nullptr);
                state.descriptor_set_layout = VK_NULL_HANDLE;
            }
        }

        pipeline_cache_.clear();
    }

    VulkanComputePipeline::PipelineState VulkanComputePipeline::create_pipeline_state_unlocked(VkDevice device,
                                                                                               std::vector<BindingLayout> bindings) const
    {
        if (device == VK_NULL_HANDLE)
            throw std::logic_error("ComputePipeline creation requires a valid VkDevice.");

        PipelineState state;
        state.device = device;
        state.bindings = std::move(bindings);

        std::vector<VkDescriptorSetLayoutBinding> descriptor_bindings;
        descriptor_bindings.reserve(state.bindings.size());
        for (const BindingLayout& binding : state.bindings)
        {
            VkDescriptorSetLayoutBinding layout_binding {};
            layout_binding.binding = binding.binding;
            layout_binding.descriptorType = descriptor_type(binding.kind);
            layout_binding.descriptorCount = 1;
            layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            descriptor_bindings.push_back(layout_binding);
        }

        try
        {
            if (!descriptor_bindings.empty())
            {
                VkDescriptorSetLayoutCreateInfo descriptor_layout_info {};
                descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                descriptor_layout_info.bindingCount = static_cast<uint32_t>(descriptor_bindings.size());
                descriptor_layout_info.pBindings = descriptor_bindings.data();

                VkResult result = vkCreateDescriptorSetLayout(device,
                                                              &descriptor_layout_info,
                                                              nullptr,
                                                              &state.descriptor_set_layout);
                if (result != VK_SUCCESS)
                {
                    throw std::runtime_error("vkCreateDescriptorSetLayout failed for ComputePipeline. VkResult=" +
                                             std::to_string(static_cast<int>(result)));
                }
            }

            VkPushConstantRange push_constant_range {};
            const uint32_t push_constant_bytes = push_constant_size();
            if (push_constant_bytes != 0)
            {
                push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                push_constant_range.offset = 0;
                push_constant_range.size = push_constant_bytes;
            }

            VkPipelineLayoutCreateInfo layout_info {};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = state.descriptor_set_layout == VK_NULL_HANDLE ? 0u : 1u;
            layout_info.pSetLayouts = state.descriptor_set_layout == VK_NULL_HANDLE ? nullptr : &state.descriptor_set_layout;
            layout_info.pushConstantRangeCount = push_constant_bytes != 0 ? 1u : 0u;
            layout_info.pPushConstantRanges = push_constant_bytes != 0 ? &push_constant_range : nullptr;

            VkResult result = vkCreatePipelineLayout(device, &layout_info, nullptr, &state.layout);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("vkCreatePipelineLayout failed for ComputePipeline. VkResult=" +
                                         std::to_string(static_cast<int>(result)));
            }

            VkShaderModule shader = create_shader_module(device, desc_.compute_shader.module);
            try
            {
                VkPipelineShaderStageCreateInfo shader_stage {};
                shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                shader_stage.module = shader;
                shader_stage.pName = "main";

                VkComputePipelineCreateInfo pipeline_info {};
                pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                pipeline_info.stage = shader_stage;
                pipeline_info.layout = state.layout;

                result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &state.pipeline);
                if (result != VK_SUCCESS)
                {
                    throw std::runtime_error("vkCreateComputePipelines failed for ComputePipeline. VkResult=" +
                                             std::to_string(static_cast<int>(result)));
                }

                vkDestroyShaderModule(device, shader, nullptr);
            }
            catch (...)
            {
                vkDestroyShaderModule(device, shader, nullptr);
                throw;
            }
        }
        catch (...)
        {
            if (state.pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, state.pipeline, nullptr);
            if (state.layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(device, state.layout, nullptr);
            if (state.descriptor_set_layout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device, state.descriptor_set_layout, nullptr);
            throw;
        }

        return state;
    }

    VulkanComputePipeline::PipelineState& VulkanComputePipeline::pipeline_state_unlocked(VkDevice device,
                                                                                        const DispatchDesc& dispatch)
    {
        std::vector<BindingLayout> key = binding_layout_key(dispatch);
        auto found = std::ranges::find_if(pipeline_cache_, [&](const PipelineState& state) {
            return state.device == device && state.bindings == key;
        });

        if (found == pipeline_cache_.end())
        {
            pipeline_cache_.push_back(create_pipeline_state_unlocked(device, std::move(key)));
            found = std::prev(pipeline_cache_.end());
        }

        return *found;
    }

    void VulkanComputePipeline::set_dispatch(uint16_t groups_x, uint16_t groups_y, uint16_t groups_z)
    {
        std::lock_guard lock(mutex_);
        dispatch_.groups_x = std::max<uint32_t>(1u, groups_x);
        dispatch_.groups_y = std::max<uint32_t>(1u, groups_y);
        dispatch_.groups_z = std::max<uint32_t>(1u, groups_z);
    }

    void VulkanComputePipeline::set_push_constant_direct(uint64_t byte_offset,
                                                         const void* data,
                                                         size_t size,
                                                         int32_t bind_type)
    {
        if (!is_push_constant_member(bind_type) || size == 0)
            return;

        if (data == nullptr)
            throw std::invalid_argument("ComputePipeline push constant data must not be null.");

        if (add_overflows(byte_offset, size))
            throw std::overflow_error("ComputePipeline push constant range overflow.");

        const uint64_t end = byte_offset + static_cast<uint64_t>(size);
        if (end > std::numeric_limits<size_t>::max())
            throw std::overflow_error("ComputePipeline push constant range is too large for host memory.");

        std::lock_guard lock(mutex_);
        const uint32_t declared_size = push_constant_size();
        if (declared_size != 0 && end > declared_size)
            throw std::out_of_range("ComputePipeline push constant write exceeds reflected push constant size.");

        if (push_constant_data_.size() < static_cast<size_t>(end))
            push_constant_data_.resize(static_cast<size_t>(end));

        std::memcpy(push_constant_data_.data() + static_cast<size_t>(byte_offset), data, size);
    }

    void VulkanComputePipeline::set_resource_direct(uint64_t, uint32_t, const HardwareBuffer& buffer, int32_t bind_type)
    {
        if (is_storage_buffer_bind(bind_type))
        {
            bind_storage_buffer(0, buffer);
        }
    }

    void VulkanComputePipeline::set_resource_direct(uint64_t, uint32_t, const HardwareImage& image, int32_t bind_type)
    {
        if (is_storage_image_bind(bind_type))
        {
            bind_storage_image(0, image);
        }
    }

    void VulkanComputePipeline::bind_storage_buffer(uint32_t binding, const HardwareBuffer& buffer)
    {
        std::lock_guard lock(mutex_);
        auto found = std::ranges::find_if(bound_buffers_, [binding](const BoundBuffer& item) {
            return item.binding == binding;
        });

        BoundBuffer value {
            .binding = binding,
            .buffer = buffer,
            .access = AccessKind::ReadWrite,
        };

        if (found == bound_buffers_.end())
            bound_buffers_.push_back(std::move(value));
        else
            *found = std::move(value);
    }

    void VulkanComputePipeline::bind_storage_image(uint32_t binding, const HardwareImage& image)
    {
        std::lock_guard lock(mutex_);
        auto found = std::ranges::find_if(bound_images_, [binding](const BoundImage& item) {
            return item.binding == binding;
        });

        BoundImage value {
            .binding = binding,
            .image = image,
            .access = AccessKind::ReadWrite,
        };

        if (found == bound_images_.end())
            bound_images_.push_back(std::move(value));
        else
            *found = std::move(value);
    }

    VulkanComputePipeline::Snapshot VulkanComputePipeline::snapshot() const
    {
        std::lock_guard lock(mutex_);
        DispatchDesc dispatch = dispatch_;
        dispatch.push_constant_data = push_constant_data_;

        return {
            .desc = desc_,
            .dispatch = std::move(dispatch),
            .buffers = bound_buffers_,
            .images = bound_images_,
        };
    }

    CommandBatch VulkanComputePipeline::command_batch(const ResourceHandle& pipeline) const
    {
        Snapshot state = snapshot();

        for (const BoundBuffer& buffer : state.buffers)
        {
            if (!buffer.buffer)
                continue;

            ResourceHandle handle = static_cast<const ResourceHandle&>(buffer.buffer);
            state.dispatch.bindings.push_back(
                {
                    .binding = buffer.binding,
                    .resource = handle,
                    .kind = DispatchBindingKind::StorageBuffer,
                    .access = buffer.access,
                });
            state.dispatch.resource_uses.push_back({ handle, buffer.access, 0 });
        }

        for (const BoundImage& image : state.images)
        {
            if (!image.image)
                continue;

            ResourceHandle handle = static_cast<const ResourceHandle&>(image.image);
            state.dispatch.bindings.push_back(
                {
                    .binding = image.binding,
                    .resource = handle,
                    .kind = DispatchBindingKind::StorageImage,
                    .access = image.access,
                });
            state.dispatch.resource_uses.push_back({ handle, image.access, 0 });
        }

        CommandBatch batch;
        batch << dispatch({ pipeline }, std::move(state.dispatch));
        return batch;
    }

    VulkanComputePipeline::PreparedDispatch VulkanComputePipeline::prepare_dispatch(VkDevice device,
                                                                                    const DispatchDesc& dispatch)
    {
        std::lock_guard lock(mutex_);
        PipelineState& state = pipeline_state_unlocked(device, dispatch);
        if (state.pipeline == VK_NULL_HANDLE || state.layout == VK_NULL_HANDLE)
        {
            throw std::logic_error("ComputePipeline resolved an invalid Vulkan pipeline.");
        }

        PreparedDispatch prepared {
            .layout = state.layout,
            .pipeline = state.pipeline,
        };

        if (dispatch.bindings.empty())
        {
            return prepared;
        }

        auto descriptor_owner = std::make_shared<TransientDescriptorSet>();
        descriptor_owner->device = device;

        uint32_t storage_buffer_count = 0;
        uint32_t storage_image_count = 0;
        for (const DispatchResourceBinding& binding : dispatch.bindings)
        {
            if (binding.kind == DispatchBindingKind::StorageImage)
                ++storage_image_count;
            else
                ++storage_buffer_count;
        }

        std::vector<VkDescriptorPoolSize> pool_sizes;
        if (storage_buffer_count != 0)
            pool_sizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storage_buffer_count });
        if (storage_image_count != 0)
            pool_sizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storage_image_count });

        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        VkResult result = vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_owner->pool);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkCreateDescriptorPool failed for ComputePipeline dispatch. VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }

        VkDescriptorSetAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_owner->pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &state.descriptor_set_layout;

        result = vkAllocateDescriptorSets(device, &alloc_info, &prepared.descriptor_set);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkAllocateDescriptorSets failed for ComputePipeline dispatch. VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }

        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkDescriptorImageInfo> image_infos;
        std::vector<VkWriteDescriptorSet> writes;
        buffer_infos.reserve(storage_buffer_count);
        image_infos.reserve(storage_image_count);
        writes.reserve(dispatch.bindings.size());

        for (const DispatchResourceBinding& binding : dispatch.bindings)
        {
            VkWriteDescriptorSet write {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = prepared.descriptor_set;
            write.dstBinding = binding.binding;
            write.dstArrayElement = 0;
            write.descriptorCount = 1;
            write.descriptorType = descriptor_type(binding.kind);

            if (binding.kind == DispatchBindingKind::StorageImage)
            {
                ImageStore::Read image = read_image(binding.resource);
                if (!image || image->image_view == VK_NULL_HANDLE)
                {
                    throw std::logic_error("ComputePipeline storage image binding requires a valid HardwareImage.");
                }

                VkDescriptorImageInfo info {};
                info.imageView = image->image_view;
                info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                image_infos.push_back(info);
                write.pImageInfo = &image_infos.back();
            }
            else
            {
                BufferStore::Read buffer = read_buffer(binding.resource);
                if (!buffer || buffer->buffer_handle == VK_NULL_HANDLE)
                {
                    throw std::logic_error("ComputePipeline storage buffer binding requires a valid HardwareBuffer.");
                }

                VkDescriptorBufferInfo info {};
                info.buffer = buffer->buffer_handle;
                info.offset = 0;
                info.range = buffer->logical_size();
                buffer_infos.push_back(info);
                write.pBufferInfo = &buffer_infos.back();
            }

            writes.push_back(write);
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        prepared.descriptor_set_lifetime = std::move(descriptor_owner);
        return prepared;
    }
}
