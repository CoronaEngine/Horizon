#include "hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h"
#include "hardware_wrapper_vulkan/resource_pool.h"
#include "horizon.h"
#include "validation/hardware_validation.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace Corona::Horizon
{
    namespace
    {
        using RasterizerPipelineStore = ResourceStore<RasterizerPipelineWrap, NoopReleaser>;

        [[nodiscard]] std::shared_ptr<VulkanRasterizerPipeline> pipeline_impl(const std::shared_ptr<IResourceRef>& token)
        {
            auto pipeline = write<RasterizerPipelineStore>(token);
            if (!pipeline || !pipeline->impl)
                throw std::logic_error("RasterizerPipeline does not reference a valid implementation.");

            auto impl = std::static_pointer_cast<VulkanRasterizerPipeline>(pipeline->impl);
            if (!impl)
                throw std::logic_error("RasterizerPipeline implementation has an unexpected type.");

            return impl;
        }

        [[nodiscard]] std::shared_ptr<IResourceRef> make_pipeline_token(RasterizerPipelineDesc desc,
                                                                        const std::source_location& source_location)
        {
            auto handle = resource_pool().rasterizer_pipelines.create(
                [desc = std::move(desc), source_location]() mutable {
                    RasterizerPipelineWrap wrap;
                    wrap.impl = std::make_shared<VulkanRasterizerPipeline>(std::move(desc), source_location);
                    return wrap;
                });

            return make_token<RasterizerPipelineStore>(std::move(handle));
        }

        void bind_auto_resources(const std::shared_ptr<VulkanRasterizerPipeline>& impl)
        {
            std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries = impl->auto_bind_entries();

            for (const EmbeddedShader::AutoBindEntry& entry : auto_bind_entries)
            {
                if (entry.boundResourceRef == nullptr || *entry.boundResourceRef == nullptr)
                    continue;

                impl->set_resource_direct(entry.byteOffset,
                                          entry.typeSize,
                                          *static_cast<HardwareImage*>(*entry.boundResourceRef),
                                          entry.bindType,
                                          entry.location,
                                          0,
                                          entry.location);
            }
        }
    }

    RasterizerPipeline::RasterizerPipeline() = default;

    RasterizerPipeline::RasterizerPipeline(RasterizerPipelineDesc desc, const std::source_location& source_location)
    {
        if (!validate_rasterizer_pipeline_desc(desc))
            return;

        ResourceBridge::set(*this, make_pipeline_token(std::move(desc), source_location));
    }

    RasterizerPipeline::RasterizerPipeline(const RasterizerPipeline& other)
        : ResourceHandle(other)
    {
    }

    RasterizerPipeline::RasterizerPipeline(RasterizerPipeline&& other) noexcept
        : ResourceHandle(std::move(other))
    {
    }

    RasterizerPipeline::~RasterizerPipeline() = default;

    RasterizerPipeline& RasterizerPipeline::operator=(const RasterizerPipeline& other)
    {
        if (this == &other)
            return *this;

        ResourceHandle::operator=(other);
        return *this;
    }

    RasterizerPipeline& RasterizerPipeline::operator=(RasterizerPipeline&& other) noexcept
    {
        if (this == &other)
            return *this;

        ResourceHandle::operator=(std::move(other));
        return *this;
    }

    RasterizerPipeline::operator bool() const noexcept
    {
        return ResourceHandle::operator bool();
    }

    RasterizerPipeline& RasterizerPipeline::operator()(uint16_t width, uint16_t height)
    {
        std::shared_ptr<IResourceRef> token = ResourceBridge::token(*this);
        std::shared_ptr<VulkanRasterizerPipeline> impl = pipeline_impl(token);

        bind_auto_resources(impl);
        impl->set_extent(width, height);
        return *this;
    }

    RasterizerPipeline& RasterizerPipeline::record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer)
    {
        DrawIndexedParams params;
        return record(index_buffer, vertex_buffer, params);
    }

    RasterizerPipeline& RasterizerPipeline::record(const HardwareBuffer& index_buffer,
                                                   const HardwareBuffer& vertex_buffer,
                                                   const DrawIndexedParams& params)
    {
        if (!validate_rasterizer_pipeline_record(index_buffer, vertex_buffer, params))
            return *this;

        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        std::shared_ptr<VulkanRasterizerPipeline> impl = pipeline_impl(token);
        bind_auto_resources(impl);
        impl->record(*this, index_buffer, vertex_buffer, params);
        return *this;
    }

    RasterizerPipeline& RasterizerPipeline::clear_records()
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->clear_records();
        return *this;
    }

    RasterizerPipeline& RasterizerPipeline::bind_render_target(uint32_t location, HardwareImage& image)
    {
        set_resource_direct(0,
                            0,
                            image,
                            static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs),
                            location);
        return *this;
    }

    RasterizerPipeline& RasterizerPipeline::bind_depth_target(HardwareImage& image)
    {
        std::shared_ptr<IResourceRef> token = ResourceBridge::token(*this);

        pipeline_impl(token)->set_depth_target(image);
        return *this;
    }

    CommandBatch RasterizerPipeline::command_batch() const
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        return pipeline_impl(token)->command_batch();
    }

    void RasterizerPipeline::set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set, uint32_t binding)
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->set_push_constant_direct(byte_offset, data, size, bind_type, set, binding);
    }

    void RasterizerPipeline::set_resource_direct(uint64_t byte_offset,
                                                 uint32_t type_size,
                                                 const HardwareBuffer& buffer,
                                                 int32_t bind_type,
                                                 uint32_t set,
                                                 uint32_t binding)
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->set_resource_direct(byte_offset, type_size, buffer, bind_type, set, binding);
    }

    void RasterizerPipeline::set_resource_direct(uint64_t byte_offset,
                                                 uint32_t type_size,
                                                 const HardwareImage& image,
                                                 int32_t bind_type,
                                                 uint32_t location,
                                                 uint32_t set,
                                                 uint32_t binding)
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->set_resource_direct(byte_offset, type_size, image, bind_type, location, set, binding);
    }

    void RasterizerPipeline::add_auto_bind_entry(EmbeddedShader::AutoBindEntry entry)
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->add_auto_bind_entry(std::move(entry));
    }
}
