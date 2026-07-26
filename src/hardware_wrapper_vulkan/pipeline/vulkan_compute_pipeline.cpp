#include "vulkan_compute_pipeline.h"

#include "hardware_wrapper_vulkan/hardware_context.h"
#include "hardware_wrapper_vulkan/hardware/execution_profile.h"
#include "hardware_wrapper_vulkan/resource_pool.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
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

        [[nodiscard]] BufferStore::Read read_buffer(const ResourceHandle& handle)
        {
            return read<BufferStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] BufferStore::Write write_buffer(const ResourceHandle& handle)
        {
            return write<BufferStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] ImageStore::Write write_image(const ResourceHandle& handle)
        {
            return write<ImageStore>(ResourceBridge::token(handle));
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

        void diagnose_bindless_reflection(const EmbeddedShader::ShaderCodeModule& module, bool uses_bindless)
        {
            bool relevant = !uses_bindless;
            for (const auto& info : module.shaderResources.bindInfoPool)
            {
                if (info.variateName == "inputImageRGBA16" ||
                    (info.set == ResourceManager::bindless_storage_image_set && info.binding == 0))
                {
                    relevant = true;
                    break;
                }
            }

            if (!relevant)
                return;

            std::ostringstream out;
            out << "ComputePipeline reflection uses_bindless=" << (uses_bindless ? "true" : "false")
                << " bind_count=" << module.shaderResources.bindInfoPool.size();
            for (const auto& info : module.shaderResources.bindInfoPool)
            {
                out << "\n  name=" << info.variateName
                    << " type=" << info.typeName
                    << " set=" << info.set
                    << " binding=" << info.binding
                    << " location=" << info.location
                    << " bindType=" << static_cast<int32_t>(info.bindType)
                    << " elementCount=" << info.elementCount
                    << " typeSize=" << info.typeSize
                    << " byteOffset=" << info.byteOffset;
            }
            Diagnostics::write(Diagnostics::Level::Info, "HORIZON VALIDATION", out.str());
        }

        void add_resource_use(std::vector<ResourceUse>& uses, ResourceHandle handle, AccessKind access)
        {
            if (!handle)
                return;

            const auto id = ResourceBridge::token(handle) ? ResourceBridge::token(handle)->id() : 0;
            auto found = std::ranges::find_if(uses, [id](const ResourceUse& item) {
                auto token = ResourceBridge::token(item.handle);
                return token && token->id() == id;
            });

            if (found == uses.end())
            {
                uses.push_back({ std::move(handle), access, 0 });
                return;
            }

            if (found->access != access)
                found->access = AccessKind::ReadWrite;
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

        [[nodiscard]] std::vector<UniformBufferBindingData> reflected_uniform_buffers(const EmbeddedShader::ShaderCodeModule& module)
        {
            std::vector<UniformBufferBindingData> buffers;
            for (const auto& info : module.shaderResources.bindInfoPool)
            {
                if (info.bindType != BindType::uniformBuffers)
                    continue;

                const uint32_t size = info.typeSize != 0 ? info.typeSize : module.shaderResources.uniformBufferSize;
                add_uniform_buffer(buffers, info.set, info.binding, size);
            }

            if (buffers.empty() && module.shaderResources.uniformBufferSize != 0)
                add_uniform_buffer(buffers, 0, 0, module.shaderResources.uniformBufferSize);

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
                throw std::out_of_range("ComputePipeline uniform member does not match any reflected uniform buffer.");

            if (found->data.size() > std::numeric_limits<uint32_t>::max())
                throw std::overflow_error("ComputePipeline uniform buffer is too large.");

            write_bytes(found->data,
                        static_cast<uint32_t>(found->data.size()),
                        byte_offset,
                        data,
                        size,
                        "ComputePipeline uniform buffer");
        }

        [[nodiscard]] uint32_t store_storage_buffer_descriptor(const HardwareBuffer& buffer)
        {
            BufferStore::Write native = write_buffer(static_cast<const ResourceHandle&>(buffer));
            if (!native || native->buffer_handle == VK_NULL_HANDLE)
                throw std::logic_error("ComputePipeline bindless storage buffer requires a valid HardwareBuffer.");

            ResourceManager* manager = native->resource_manager != nullptr ? native->resource_manager : &resource_manager();
            return manager->store_descriptor(*native);
        }

        [[nodiscard]] uint32_t store_sampled_image_descriptor(const HardwareImage& image)
        {
            ImageStore::Write native = write_image(static_cast<const ResourceHandle&>(image));
            if (!native || native->image_view == VK_NULL_HANDLE)
                throw std::logic_error("ComputePipeline bindless sampled image requires a valid HardwareImage.");

            ResourceManager* manager = native->resource_manager != nullptr ? native->resource_manager : &resource_manager();
            return manager->store_sampled_descriptor(*native);
        }

        [[nodiscard]] uint32_t store_storage_image_descriptor(const HardwareImage& image)
        {
            ImageStore::Write native = write_image(static_cast<const ResourceHandle&>(image));
            if (!native || native->image_view == VK_NULL_HANDLE)
                throw std::logic_error("ComputePipeline bindless storage image requires a valid HardwareImage.");

            ResourceManager* manager = native->resource_manager != nullptr ? native->resource_manager : &resource_manager();
            return manager->store_storage_descriptor(*native);
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
                key.push_back({ binding.set, binding.binding, binding.kind });
            }

            std::ranges::sort(key, [](const auto& left, const auto& right) {
                if (left.set != right.set)
                    return left.set < right.set;
                if (left.binding != right.binding)
                    return left.binding < right.binding;
                return static_cast<int>(left.kind) < static_cast<int>(right.kind);
            });

            auto duplicate = std::ranges::unique(key, [](const auto& left, const auto& right) {
                return left.set == right.set && left.binding == right.binding && left.kind == right.kind;
            });
            key.erase(duplicate.begin(), duplicate.end());

            for (size_t index = 1; index < key.size(); ++index)
            {
                if (key[index - 1].set == key[index].set && key[index - 1].binding == key[index].binding)
                {
                    throw std::logic_error("ComputePipeline cannot bind different descriptor kinds to the same binding.");
                }
            }

            return key;
        }

    }

    VulkanComputePipeline::VulkanComputePipeline(ComputePipelineDesc desc,
                                                 std::source_location source_location)
        : auto_bind_entries_(std::move(desc.auto_bind_entries)),
          desc_(std::move(desc)),
          source_location_(source_location)
    {
        desc_.auto_bind_entries.clear();

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
        uniform_buffers_ = reflected_uniform_buffers(desc_.compute_shader.module);

        // 为每个 UBO binding 创建持久 GPU buffer（HOST_VISIBLE，之后只 write_bytes）
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
                1, BufferUsageFlags::Uniform, "ComputePipeline.ubo_persistent");
            ubo.gpu_buffer = buf;   // ResourceHandle 切片，用于 dispatch 路径
            ubo_buffers_.push_back(std::move(buf));
        }
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

    std::shared_ptr<EmbeddedShader::ComputePipelineObject> VulkanComputePipeline::pipeline_object() const
    {
        std::lock_guard lock(mutex_);
        return desc_.pipelineObject;
    }

    void VulkanComputePipeline::bind_auto_resources()
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
            images.reserve(auto_bind_entries_.size());
            values.reserve(auto_bind_entries_.size());
            for (const EmbeddedShader::AutoBindEntry& entry : auto_bind_entries_)
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
                        reflected_binding_coordinates(desc_.compute_shader.module, entry),
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

    void VulkanComputePipeline::bind_auto_image(EmbeddedShader::AutoBindEntry entry, const HardwareImage& image)
    {
        set_resource_direct(entry.byteOffset,
                            entry.typeSize,
                            image,
                            entry.bindType,
                            0,
                            entry.location);
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

            for (PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
            {
                // 销毁 pool 即隐式释放其中的 descriptor set。
                for (PipelineState::UniformDescriptorSet& uniform_set : set_layout.uniform_sets)
                {
                    if (uniform_set.pool != VK_NULL_HANDLE)
                        vkDestroyDescriptorPool(state.device, uniform_set.pool, nullptr);
                }
                set_layout.uniform_sets.clear();

                if (set_layout.layout != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(state.device, set_layout.layout, nullptr);
                    set_layout.layout = VK_NULL_HANDLE;
                }
            }

            for (VkDescriptorSetLayout layout : state.empty_descriptor_set_layouts)
            {
                if (layout != VK_NULL_HANDLE)
                    vkDestroyDescriptorSetLayout(state.device, layout, nullptr);
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
        state.uses_bindless = uses_bindless_descriptors(desc_.compute_shader.module);
        diagnose_bindless_reflection(desc_.compute_shader.module, state.uses_bindless);

        auto add_descriptor_binding = [&](uint32_t set, uint32_t binding, VkDescriptorType descriptor_type) {
            if (state.uses_bindless && set < ResourceManager::bindless_descriptor_set_count)
            {
                throw std::logic_error("ComputePipeline ordinary descriptors cannot use Horizon bindless reserved sets 0-2.");
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
                    throw std::logic_error("ComputePipeline reflection maps different descriptor types to the same set/binding.");
                return;
            }

            set_found->bindings.push_back({ set, binding, descriptor_type });
        };

        for (const UniformBufferBindingData& uniform_buffer : reflected_uniform_buffers(desc_.compute_shader.module))
        {
            add_descriptor_binding(uniform_buffer.set, uniform_buffer.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        }

        // 非 UBO 资源（storage buffer/image）强制走 bindless：运行时绑定恒落在
        // bindless 保留 set 0-2，此处全部跳过（其 descriptor 由 bindless 持久 set 承载）。
        // 任何落到保留 set 之外的普通 storage 绑定都违反 bindless 契约，直接拒绝。
        for (const BindingLayout& binding : state.bindings)
        {
            if (binding.set < ResourceManager::bindless_descriptor_set_count)
                continue;

            throw std::logic_error(
                "ComputePipeline storage buffer/image must be bindless (reserved set 0-2); "
                "transient storage descriptors are not supported.");
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

        try
        {
            for (PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
            {
                std::vector<VkDescriptorSetLayoutBinding> descriptor_bindings;
                descriptor_bindings.reserve(set_layout.bindings.size());
                for (const PipelineState::DescriptorBindingLayout& binding : set_layout.bindings)
                {
                    VkDescriptorSetLayoutBinding layout_binding {};
                    layout_binding.binding = binding.binding;
                    layout_binding.descriptorType = binding.descriptor_type;
                    layout_binding.descriptorCount = 1;
                    layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                    descriptor_bindings.push_back(layout_binding);
                }

                VkDescriptorSetLayoutCreateInfo descriptor_layout_info {};
                descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                // 该 set 仅承载 UBO（非 UBO 全走 bindless）。UBO 是批次共享的持久 buffer，
                // (binding, buffer, range) 在管线生命周期内恒定，值的变化走 mapped 内存
                // 写入，所以按 bindless set 0-2 的同一形式处理：分配一次、写入一次、只 bind。
                descriptor_layout_info.flags = 0;
                descriptor_layout_info.bindingCount = static_cast<uint32_t>(descriptor_bindings.size());
                descriptor_layout_info.pBindings = descriptor_bindings.data();

                VkResult result = vkCreateDescriptorSetLayout(device,
                                                              &descriptor_layout_info,
                                                              nullptr,
                                                              &set_layout.layout);
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
                    VkResult result = vkCreateDescriptorSetLayout(device, &empty_layout_info, nullptr, &empty_layout);
                    if (result != VK_SUCCESS)
                    {
                        throw std::runtime_error("vkCreateDescriptorSetLayout(empty) failed for ComputePipeline. VkResult=" +
                                                 std::to_string(static_cast<int>(result)));
                    }

                    state.empty_descriptor_set_layouts.push_back(empty_layout);
                    set_layout = empty_layout;
                }
            }

            layout_info.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
            layout_info.pSetLayouts = set_layouts.empty() ? nullptr : set_layouts.data();
            layout_info.pushConstantRangeCount = push_constant_bytes != 0 ? 1u : 0u;
            layout_info.pPushConstantRanges = push_constant_bytes != 0 ? &push_constant_range : nullptr;

            VkResult result = vkCreatePipelineLayout(device, &layout_info, nullptr, &state.layout);
            if (result != VK_SUCCESS)
            {
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   "vkCreatePipelineLayout failed for ComputePipeline. VkResult=" +
                                       std::to_string(static_cast<int>(result)));
                throw std::runtime_error("vkCreatePipelineLayout failed for ComputePipeline. VkResult=" +
                                         std::to_string(static_cast<int>(result)));
            }

            const std::string pipeline_name = desc_.debug_name.empty() ? "horizon.compute_pipeline" : desc_.debug_name;
            name_vulkan_object(device,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                               reinterpret_cast<uint64_t>(state.layout),
                               pipeline_name + ".layout");

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
                    Diagnostics::write(Diagnostics::Level::Error,
                                       "VK_ERROR",
                                       "vkCreateComputePipelines failed for ComputePipeline. VkResult=" +
                                           std::to_string(static_cast<int>(result)));
                    throw std::runtime_error("vkCreateComputePipelines failed for ComputePipeline. VkResult=" +
                                             std::to_string(static_cast<int>(result)));
                }

                name_vulkan_object(device,
                                   VK_OBJECT_TYPE_PIPELINE,
                                   reinterpret_cast<uint64_t>(state.pipeline),
                                   pipeline_name + ".pipeline");

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
            for (PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
            {
                if (set_layout.layout != VK_NULL_HANDLE)
                    vkDestroyDescriptorSetLayout(device, set_layout.layout, nullptr);
            }
            for (VkDescriptorSetLayout layout : state.empty_descriptor_set_layouts)
            {
                if (layout != VK_NULL_HANDLE)
                    vkDestroyDescriptorSetLayout(device, layout, nullptr);
            }
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

    void VulkanComputePipeline::set_debug_label(std::string label)
    {
        std::lock_guard lock(mutex_);
        dispatch_.debug_label = std::move(label);
    }

    void VulkanComputePipeline::set_push_constant_direct(uint64_t byte_offset,
                                                         const void* data,
                                                         size_t size,
                                                         int32_t bind_type,
                                                         uint32_t set,
                                                         uint32_t binding)
    {
        if (size == 0)
            return;

        std::lock_guard lock(mutex_);
        if (is_push_constant_member(bind_type))
        {
            write_bytes(push_constant_data_, push_constant_size(), byte_offset, data, size, "ComputePipeline push constant");
            return;
        }

        if (is_uniform_buffer_member(bind_type))
        {
            write_uniform_member(uniform_buffers_, set, binding, byte_offset, data, size);

            // 同步 flush 到持久 GPU buffer（HOST_VISIBLE，memcpy 级开销）
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

    void VulkanComputePipeline::set_resource_direct(uint64_t byte_offset,
                                                    uint32_t type_size,
                                                    const HardwareBuffer& buffer,
                                                    int32_t bind_type,
                                                    uint32_t set,
                                                    uint32_t binding)
    {
        if (!is_storage_buffer_bind(bind_type))
            return;

        if (uses_bindless_descriptors(desc_.compute_shader.module))
        {
            const uint32_t descriptor_index = store_storage_buffer_descriptor(buffer);
            std::lock_guard lock(mutex_);
            write_descriptor_handle(push_constant_data_,
                                    push_constant_size(),
                                    byte_offset,
                                    type_size,
                                    descriptor_index,
                                    "ComputePipeline bindless storage buffer");

            auto found = std::ranges::find_if(bound_buffers_, [set, binding](const BoundBuffer& item) {
                return item.set == set && item.binding == binding;
            });

            BoundBuffer value {
                .set = set,
                .binding = binding,
                .buffer = buffer,
                .access = AccessKind::ReadWrite,
            };

            if (found == bound_buffers_.end())
                bound_buffers_.push_back(std::move(value));
            else
                *found = std::move(value);
            return;
        }

        std::lock_guard lock(mutex_);
        auto found = std::ranges::find_if(bound_buffers_, [set, binding](const BoundBuffer& item) {
            return item.set == set && item.binding == binding;
        });

        BoundBuffer value {
            .set = set,
            .binding = binding,
            .buffer = buffer,
            .access = AccessKind::ReadWrite,
        };

        if (found == bound_buffers_.end())
            bound_buffers_.push_back(std::move(value));
        else
            *found = std::move(value);
    }

    void VulkanComputePipeline::set_resource_direct(uint64_t byte_offset,
                                                    uint32_t type_size,
                                                    const HardwareImage& image,
                                                    int32_t bind_type,
                                                    uint32_t set,
                                                    uint32_t binding)
    {
        if (!is_direct_resource_bind(bind_type))
            return;

        if (uses_bindless_descriptors(desc_.compute_shader.module))
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
                                    "ComputePipeline bindless image");

            auto found = std::ranges::find_if(bound_images_, [set, binding](const BoundImage& item) {
                return item.set == set && item.binding == binding;
            });

            BoundImage value {
                .set = set,
                .binding = binding,
                .image = image,
                .access = AccessKind::ReadWrite,
            };

            if (found == bound_images_.end())
                bound_images_.push_back(std::move(value));
            else
                *found = std::move(value);
            return;
        }

        if (!is_storage_image_bind(bind_type))
            return;

        std::lock_guard lock(mutex_);
        auto found = std::ranges::find_if(bound_images_, [set, binding](const BoundImage& item) {
            return item.set == set && item.binding == binding;
        });

        BoundImage value {
            .set = set,
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
        dispatch.uniform_buffers = uniform_buffers_;

        return {
            .dispatch = std::move(dispatch),
            .buffers = bound_buffers_,
            .images = bound_images_,
        };
    }

    CommandBatch VulkanComputePipeline::command_batch(ComputePipelineBase& pipeline) const
    {
        Snapshot state = snapshot();
        state.dispatch.pipeline = &pipeline;

        // 只需要 pipelineObject，不必深拷贝整个 ComputePipelineDesc（含 SPIR-V + 反射）。
        if (std::shared_ptr<EmbeddedShader::ComputePipelineObject> object = pipeline_object(); object)
        {
            state.dispatch.comp_condition_info = object->compute->getCurrentConditionInfo();
        }

        for (const BoundBuffer& buffer : state.buffers)
        {
            if (!buffer.buffer)
                continue;

            ResourceHandle handle = static_cast<const ResourceHandle&>(buffer.buffer);
            state.dispatch.bindings.push_back(
                {
                    .set = buffer.set,
                    .binding = buffer.binding,
                    .resource = handle,
                    .kind = DispatchBindingKind::StorageBuffer,
                    .access = buffer.access,
                });
            add_resource_use(state.dispatch.resource_uses, handle, buffer.access);
        }

        for (const BoundImage& image : state.images)
        {
            if (!image.image)
                continue;

            ResourceHandle handle = static_cast<const ResourceHandle&>(image.image);
            state.dispatch.bindings.push_back(
                {
                    .set = image.set,
                    .binding = image.binding,
                    .resource = handle,
                    .kind = DispatchBindingKind::StorageImage,
                    .access = image.access,
                });
            add_resource_use(state.dispatch.resource_uses, handle, image.access);
        }

        CommandBatch batch;
        batch << dispatch(std::move(state.dispatch));
        return batch;
    }

    VkDescriptorSet VulkanComputePipeline::uniform_descriptor_set_unlocked(
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
        // 命令缓冲可能仍绑定着它。稳态下每个 set layout 只会有一份（UBO buffer 在构造
        // 期建好后句柄恒定），上限仅作兜底。
        constexpr size_t max_uniform_sets_per_layout = 64;
        if (set_layout.uniform_sets.size() >= max_uniform_sets_per_layout)
        {
            throw std::runtime_error("ComputePipeline uniform descriptor set signatures exceeded " +
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
            throw std::runtime_error("vkCreateDescriptorPool failed for ComputePipeline uniform set. VkResult=" +
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
                throw std::runtime_error("vkAllocateDescriptorSets failed for ComputePipeline uniform set. VkResult=" +
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
            .uses_bindless = state.uses_bindless,
        };

        if (state.descriptor_set_layouts.empty())
        {
            return prepared;
        }

        // 非 bindless set 仅含 UBO（storage buffer/image 已全走 bindless，在建 layout 时
        // 被固化拒绝）。按 (binding, buffer, range) 签名取回持久 descriptor set：签名不变
        // 就是同一个句柄，execution 侧因而能像 bindless set 0-2 那样只 bind 一次。
        for (const PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
        {
            for (const PipelineState::DescriptorBindingLayout& binding : set_layout.bindings)
            {
                if (binding.descriptor_type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    throw std::logic_error(
                        "ComputePipeline non-bindless descriptor must be a uniform buffer; "
                        "storage buffer/image are required to be bindless.");
            }
        }

        const std::string debug_name = desc_.debug_name.empty() ? "horizon.compute_pipeline" : desc_.debug_name;
        std::vector<PipelineState::UniformDescriptorSet::Signature> signature;
        for (PipelineState::DescriptorSetLayout& set_layout : state.descriptor_set_layouts)
        {
            signature.clear();
            for (const PipelineState::DescriptorBindingLayout& binding_layout : set_layout.bindings)
            {
                auto uniform = std::ranges::find_if(dispatch.uniform_buffers, [&](const UniformBufferBindingData& item) {
                    return item.set == binding_layout.set && item.binding == binding_layout.binding;
                });
                if (uniform == dispatch.uniform_buffers.end() || !uniform->gpu_buffer)
                    throw std::logic_error("ComputePipeline uniform buffer descriptor is missing persistent GPU buffer.");

                // 直接使用管线初始化时创建的持久 buffer，跳过每次 dispatch 的 VkBuffer 分配
                BufferStore::Read buffer = read_buffer(uniform->gpu_buffer);
                if (!buffer || buffer->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("ComputePipeline uniform buffer descriptor requires a valid HardwareBuffer.");

                signature.push_back({
                    .binding = binding_layout.binding,
                    .buffer = buffer->buffer_handle,
                    .range = buffer->logical_size(),
                });
                // gpu_buffer 由 pipeline 对象持有，无需延长 lifetime
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
}
