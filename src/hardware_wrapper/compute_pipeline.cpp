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
    }

    ComputePipeline::ComputePipeline() = default;

    ComputePipeline::ComputePipeline(ComputePipelineDesc desc, const std::source_location& source_location)
    {
        ResourceBridge::set(*this, make_pipeline_token(std::move(desc), source_location));
    }

    ComputePipeline::ComputePipeline(const ComputePipeline& other)
        : ResourceHandle(other)
    {
    }

    ComputePipeline::ComputePipeline(ComputePipeline&& other) noexcept
        : ResourceHandle(std::move(other))
    {
    }

    ComputePipeline::~ComputePipeline() = default;

    ComputePipeline& ComputePipeline::operator=(const ComputePipeline& other)
    {
        if (this == &other)
            return *this;

        ResourceHandle::operator=(other);
        return *this;
    }

    ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept
    {
        if (this == &other)
            return *this;

        ResourceHandle::operator=(std::move(other));
        return *this;
    }

    ComputePipeline::operator bool() const noexcept
    {
        return ResourceHandle::operator bool();
    }

    ComputePipeline& ComputePipeline::operator()(uint16_t x, uint16_t y, uint16_t z)
    {
        pipeline_impl(ResourceBridge::token(*this))->set_dispatch(x, y, z);
        return *this;
    }

    ComputePipeline& ComputePipeline::bind_storage_buffer(uint32_t binding, const HardwareBuffer& buffer)
    {
        pipeline_impl(ResourceBridge::token(*this))->bind_storage_buffer(binding, buffer);
        return *this;
    }

    ComputePipeline& ComputePipeline::bind_storage_image(uint32_t binding, const HardwareImage& image)
    {
        pipeline_impl(ResourceBridge::token(*this))->bind_storage_image(binding, image);
        return *this;
    }

    CommandBatch ComputePipeline::command_batch() const
    {
        return pipeline_impl(ResourceBridge::token(*this))->command_batch(*this);
    }

    void ComputePipeline::set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set, uint32_t binding)
    {
        pipeline_impl(ResourceBridge::token(*this))->set_push_constant_direct(byte_offset, data, size, bind_type, set, binding);
    }

    void ComputePipeline::set_resource_direct(uint64_t byte_offset,
                                              uint32_t type_size,
                                              const HardwareBuffer& buffer,
                                              int32_t bind_type,
                                              uint32_t set,
                                              uint32_t binding)
    {
        pipeline_impl(ResourceBridge::token(*this))->set_resource_direct(byte_offset, type_size, buffer, bind_type, set, binding);
    }

    void ComputePipeline::set_resource_direct(uint64_t byte_offset,
                                              uint32_t type_size,
                                              const HardwareImage& image,
                                              int32_t bind_type,
                                              uint32_t set,
                                              uint32_t binding)
    {
        pipeline_impl(ResourceBridge::token(*this))->set_resource_direct(byte_offset, type_size, image, bind_type, set, binding);
    }
}
