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
            impl->bind_auto_resources();
        }
    }

    RasterizerPipelineBase::RasterizerPipelineBase() = default;

    RasterizerPipelineBase::RasterizerPipelineBase(RasterizerPipelineDesc desc, const std::source_location& source_location) : location_(source_location)
    {
        if (desc.pipelineObject)
        {
            const auto& vi = desc.pipelineObject->vertex->getCurrentConditionInfo();
            const auto& fi = desc.pipelineObject->fragment->getCurrentConditionInfo();
            rebuild_pipeline(std::move(desc),vi,fi);
            return;
        }
        rebuild_pipeline(std::move(desc), {}, {});
    }

    RasterizerPipelineBase::RasterizerPipelineBase(const RasterizerPipelineBase& other)
        : ResourceHandle(other)
    {
    }

    RasterizerPipelineBase::RasterizerPipelineBase(RasterizerPipelineBase&& other) noexcept
        : ResourceHandle(std::move(other))
    {
    }

    RasterizerPipelineBase::~RasterizerPipelineBase() = default;

    RasterizerPipelineBase& RasterizerPipelineBase::operator=(const RasterizerPipelineBase& other)
    {
        if (this == &other)
            return *this;

        ResourceHandle::operator=(other);
        return *this;
    }

    RasterizerPipelineBase& RasterizerPipelineBase::operator=(RasterizerPipelineBase&& other) noexcept
    {
        if (this == &other)
            return *this;

        ResourceHandle::operator=(std::move(other));
        return *this;
    }

    RasterizerPipelineBase::operator bool() const noexcept
    {
        return ResourceHandle::operator bool();
    }

    RasterizerPipelineBase& RasterizerPipelineBase::operator()(uint16_t width, uint16_t height)
    {
        std::shared_ptr<IResourceRef> token = ResourceBridge::token(*this);
        std::shared_ptr<VulkanRasterizerPipeline> impl = pipeline_impl(token);

        bind_auto_resources(impl);
        impl->set_extent(width, height);
        return *this;
    }

    RasterizerPipelineBase& RasterizerPipelineBase::record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer)
    {
        DrawIndexedParams params;
        return record(index_buffer, vertex_buffer, params);
    }

    RasterizerPipelineBase& RasterizerPipelineBase::record(const HardwareBuffer& index_buffer,
                                                           const HardwareBuffer& vertex_buffer,
                                                           const DrawIndexedParams& params)
    {
        if (!validate_rasterizer_pipeline_record(index_buffer, vertex_buffer, params))
            return *this;

        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        std::shared_ptr<VulkanRasterizerPipeline> impl = pipeline_impl(token);
        bind_auto_resources(impl);
        impl->record(this, index_buffer, vertex_buffer, params);
        return *this;
    }

    RasterizerPipelineBase& RasterizerPipelineBase::clear_records()
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->clear_records();
        return *this;
    }

    RasterizerPipelineBase& RasterizerPipelineBase::bind_render_target(uint32_t location, HardwareImage& image)
    {
        set_resource_direct(0,
                            0,
                            image,
                            static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs),
                            location);
        return *this;
    }

    RasterizerPipelineBase& RasterizerPipelineBase::bind_depth_target(HardwareImage& image)
    {
        std::shared_ptr<IResourceRef> token = ResourceBridge::token(*this);

        pipeline_impl(token)->set_depth_target(image);
        return *this;
    }

    RasterizerPipelineDesc RasterizerPipelineBase::desc() const
    {
        return pipeline_impl(ResourceBridge::token(*this))->desc();
    }

    CommandBatch RasterizerPipelineBase::command_batch() const
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        return pipeline_impl(token)->command_batch();
    }

    void RasterizerPipelineBase::rebuild_pipeline(RasterizerPipelineDesc desc, const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertConditionInfo, const EmbeddedShader::
                                                  ShaderCodeCompiler::ConditionInfo& fragConditionInfo)
    {
        if (!validate_rasterizer_pipeline_desc(desc))
            return;
        auto object = desc.pipelineObject;
        std::string key;
        if (object)
        {
            vert_condition_info_ = object->vertex->getCurrentConditionInfo();
            frag_condition_info_ = object->fragment->getCurrentConditionInfo();
            key = object->getCombinedKey(vertConditionInfo, fragConditionInfo);
            auto it = pipeline_pool_.find(key);
            if (it != pipeline_pool_.end())
            {
                ResourceBridge::set(*this, it->second);
                return;
            }
        }
        auto pipeline = make_pipeline_token(std::move(desc),location_);
        if (object)
        {
            pipeline_pool_.insert({ std::move(key), pipeline });
        }
        ResourceBridge::set(*this, std::move(pipeline));
    }

    const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& RasterizerPipelineBase::vertex_condition_info() const
    {
        return vert_condition_info_;
    }

    const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& RasterizerPipelineBase::fragment_condition_info() const
    {
        return frag_condition_info_;
    }

    void RasterizerPipelineBase::set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set, uint32_t binding)
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->set_push_constant_direct(byte_offset, data, size, bind_type, set, binding);
    }

    void RasterizerPipelineBase::set_resource_direct(uint64_t byte_offset,
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

    void RasterizerPipelineBase::set_resource_direct(uint64_t byte_offset,
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

    void RasterizerPipelineBase::add_auto_bind_entry(EmbeddedShader::AutoBindEntry entry)
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->add_auto_bind_entry(std::move(entry));
    }
}
