#include "hardware_wrapper_vulkan/pipeline/vulkan_compute_pipeline.h"
#include "hardware_wrapper_vulkan/resource_pool.h"
#include "horizon.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace Corona::Horizon
{
    namespace
    {
        using ComputePipelineStore = ResourceStore<ComputePipelineWrap, NoopReleaser>;

        [[nodiscard]] std::shared_ptr<VulkanComputePipeline> pipeline_impl(const std::shared_ptr<IResourceRef>& token)
        {
            auto pipeline = write<ComputePipelineStore>(token);
            if (!pipeline || !pipeline->impl)
                throw std::logic_error("ComputePipeline does not reference a valid implementation.");

            auto impl = std::static_pointer_cast<VulkanComputePipeline>(pipeline->impl);
            if (!impl)
                throw std::logic_error("ComputePipeline implementation has an unexpected type.");

            return impl;
        }

        [[nodiscard]] std::shared_ptr<IResourceRef> make_pipeline_token(ComputePipelineDesc desc,
                                                                        const std::source_location& source_location)
        {
            auto handle = resource_pool().compute_pipelines.create(
                [desc = std::move(desc), source_location]() mutable {
                    ComputePipelineWrap wrap;
                    wrap.impl = std::make_shared<VulkanComputePipeline>(std::move(desc), source_location);
                    return wrap;
                });

            return make_token<ComputePipelineStore>(std::move(handle));
        }

        void bind_auto_resources(const std::shared_ptr<VulkanComputePipeline>& impl)
        {
            impl->bind_auto_resources();
        }
    }

    ComputePipelineBase::ComputePipelineBase() = default;

    ComputePipelineBase::ComputePipelineBase(ComputePipelineDesc desc, const std::source_location& source_location) : location_(source_location)
    {
        if (desc.pipelineObject)
        {
            const auto& info = desc.pipelineObject->compute->getCurrentConditionInfo();
            rebuild_pipeline(std::move(desc), info);
            return;
        }
        rebuild_pipeline(std::move(desc), {});
    }

    ComputePipelineBase::ComputePipelineBase(const ComputePipelineBase& other)
        : ResourceHandle(other)
    {
    }

    ComputePipelineBase::ComputePipelineBase(ComputePipelineBase&& other) noexcept
        : ResourceHandle(std::move(other))
    {
    }

    ComputePipelineBase::~ComputePipelineBase() = default;

    ComputePipelineBase& ComputePipelineBase::operator=(const ComputePipelineBase& other)
    {
        if (this == &other)
            return *this;

        ResourceHandle::operator=(other);
        return *this;
    }

    ComputePipelineBase& ComputePipelineBase::operator=(ComputePipelineBase&& other) noexcept
    {
        if (this == &other)
            return *this;

        ResourceHandle::operator=(std::move(other));
        return *this;
    }

    ComputePipelineBase::operator bool() const noexcept
    {
        return ResourceHandle::operator bool();
    }

    void ComputePipelineBase::rebuild_pipeline(ComputePipelineDesc desc, const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& conditionInfo)
    {
        auto object = desc.pipelineObject;
        if (object)
        {
            condition_info_ = conditionInfo;
            auto it = pipeline_pool_.find(object->getCombinedKey(conditionInfo));
            if (it != pipeline_pool_.end())
            {
                ResourceBridge::set(*this, it->second);
                return;
            }
        }
        auto pipeline = make_pipeline_token(std::move(desc),location_);
        if (object)
        {
            pipeline_pool_.insert({ object->getCombinedKey(conditionInfo), pipeline });
        }
        ResourceBridge::set(*this, std::move(pipeline));
    }

    const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& ComputePipelineBase::compute_condition_info()
    {
        return condition_info_;
    }

    ComputePipelineBase& ComputePipelineBase::operator()(uint16_t x, uint16_t y, uint16_t z)
    {
        std::shared_ptr<VulkanComputePipeline> impl = pipeline_impl(ResourceBridge::token(*this));
        bind_auto_resources(impl);
        impl->set_dispatch(x, y, z);
        return *this;
    }

    ComputePipelineBase& ComputePipelineBase::set_debug_label(std::string label)
    {
        pipeline_impl(ResourceBridge::token(*this))->set_debug_label(std::move(label));
        return *this;
    }

    ComputePipelineBase& ComputePipelineBase::bind_storage_buffer(uint32_t binding, const HardwareBuffer& buffer)
    {
        pipeline_impl(ResourceBridge::token(*this))->bind_storage_buffer(binding, buffer);
        return *this;
    }

    ComputePipelineBase& ComputePipelineBase::bind_storage_image(uint32_t binding, const HardwareImage& image)
    {
        pipeline_impl(ResourceBridge::token(*this))->bind_storage_image(binding, image);
        return *this;
    }

    ComputePipelineDesc ComputePipelineBase::desc() const
    {
        return pipeline_impl(ResourceBridge::token(*this))->desc();
    }

    CommandBatch ComputePipelineBase::command_batch()
    {
        std::shared_ptr<VulkanComputePipeline> impl = pipeline_impl(ResourceBridge::token(*this));
        bind_auto_resources(impl);
        return impl->command_batch(*this);
    }

    void ComputePipelineBase::set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set, uint32_t binding)
    {
        pipeline_impl(ResourceBridge::token(*this))->set_push_constant_direct(byte_offset, data, size, bind_type, set, binding);
    }

    void ComputePipelineBase::set_resource_direct(uint64_t byte_offset,
                                                  uint32_t type_size,
                                                  const HardwareBuffer& buffer,
                                                  int32_t bind_type,
                                                  uint32_t set,
                                                  uint32_t binding)
    {
        pipeline_impl(ResourceBridge::token(*this))->set_resource_direct(byte_offset, type_size, buffer, bind_type, set, binding);
    }

    void ComputePipelineBase::set_resource_direct(uint64_t byte_offset,
                                                  uint32_t type_size,
                                                  const HardwareImage& image,
                                                  int32_t bind_type,
                                                  uint32_t set,
                                                  uint32_t binding)
    {
        pipeline_impl(ResourceBridge::token(*this))->set_resource_direct(byte_offset, type_size, image, bind_type, set, binding);
    }
}
