#include "hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h"
#include "hardware_wrapper_vulkan/resource_pool.h"
#include "horizon.h"
#include "horizon_resource_bridge.h"
#include "validation/hardware_validation.h"

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Corona::Horizon
{
    namespace
    {
        using RasterizerPipelineStore = ResourceStore<RasterizerPipelineWrap, NoopReleaser>;

        // 只读槽位里的 impl 指针（VulkanRasterizerPipeline 自身有内部锁负责状态变更），
        // 因此取共享锁即可。原先用 write<> 拿独占锁：每次 record / 每次 reflected
        // binding 赋值都要独占一次槽位，既是纯开销也让并发录制互相串行化。
        [[nodiscard]] std::shared_ptr<VulkanRasterizerPipeline> pipeline_impl(const std::shared_ptr<IResourceRef>& token)
        {
            auto pipeline = read<RasterizerPipelineStore>(token);
            if (!pipeline || !pipeline->impl)
                throw std::logic_error("RasterizerPipeline does not reference a valid implementation.");

            auto impl = std::static_pointer_cast<VulkanRasterizerPipeline>(pipeline->impl);
            if (!impl)
                throw std::logic_error("RasterizerPipeline implementation has an unexpected type.");

            return impl;
        }

        [[nodiscard]] std::shared_ptr<IResourceRef> make_pipeline_token(RasterizerPipelineDesc desc,
                                                                        RasterizerPipelineShaders shaders,
                                                                        const std::source_location& source_location)
        {
            auto handle = resource_pool().rasterizer_pipelines.create(
                [desc = std::move(desc), shaders = std::move(shaders), source_location]() mutable {
                    RasterizerPipelineWrap wrap;
                    wrap.impl = std::make_shared<VulkanRasterizerPipeline>(std::move(desc), std::move(shaders), source_location);
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

    RasterizerPipelineBase::RasterizerPipelineBase(RasterizerPipelineDesc desc,
                                                   RasterizerPipelineShaders shaders,
                                                   const std::source_location& source_location)
        : location_(source_location)
    {
        if (shaders.object)
        {
            const auto& vi = shaders.object->vertex->getCurrentConditionInfo();
            const auto& fi = shaders.object->fragment->getCurrentConditionInfo();
            rebuild_pipeline(std::move(desc), std::move(shaders), vi, fi);
            return;
        }
        rebuild_pipeline(std::move(desc), std::move(shaders), {}, {});
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

    RasterizerPipelineBase& RasterizerPipelineBase::extent(uint32_t width, uint32_t height)
    {
        // set_extent 收 uint16_t；公共签名统一 uint32_t（与 HardwareImageDesc 的
        // extent 同类型），这里检查后窄化。
        constexpr uint32_t max_extent = std::numeric_limits<uint16_t>::max();
        if (width > max_extent || height > max_extent)
            throw std::out_of_range("RasterizerPipeline render extent exceeds 65535 per dimension.");

        std::shared_ptr<IResourceRef> token = ResourceBridge::token(*this);
        std::shared_ptr<VulkanRasterizerPipeline> impl = pipeline_impl(token);

        bind_auto_resources(impl);
        impl->set_extent(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
        return *this;
    }

    RasterizerPipelineBase& RasterizerPipelineBase::record_indirect(const HardwareBuffer& index_buffer,
                                                                   const HardwareBuffer& vertex_buffer,
                                                                   const HardwareBuffer& indirect_buffer,
                                                                   const DrawIndexedIndirectParams& params)
    {
        if (!index_buffer || !vertex_buffer || !indirect_buffer || params.draw_count == 0)
            return *this;

        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        std::shared_ptr<VulkanRasterizerPipeline> impl = pipeline_impl(token);
        bind_auto_resources(impl);
        impl->record_indirect(this, index_buffer, vertex_buffer, indirect_buffer, params);
        return *this;
    }

    RasterizerPipelineBase& RasterizerPipelineBase::clear_records()
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->clear_records();
        return *this;
    }

    RasterizerPipelineBase& RasterizerPipelineBase::bind_depth_target(HardwareImage& image)
    {
        std::shared_ptr<IResourceRef> token = ResourceBridge::token(*this);

        pipeline_impl(token)->set_depth_target(image);
        return *this;
    }

    void RasterizerPipelineBase::record_into(CommandRecorder& recorder) const
    {
        std::shared_ptr<IResourceRef> token;
        token = ResourceBridge::token(*this);

        pipeline_impl(token)->record_into(recorder);
    }

    void RasterizerPipelineBase::rebuild_pipeline(RasterizerPipelineDesc desc,
                                                  RasterizerPipelineShaders shaders,
                                                  const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertConditionInfo,
                                                  const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& fragConditionInfo)
    {
        if (!validate_rasterizer_pipeline_desc(desc, shaders))
            return;
        auto object = shaders.object;
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
        auto pipeline = make_pipeline_token(std::move(desc), std::move(shaders), location_);
        if (object)
        {
            pipeline_pool_.insert({ std::move(key), pipeline });
        }
        ResourceBridge::set(*this, std::move(pipeline));
    }

    bool RasterizerPipelineBase::sync_shader_conditions(const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertConditionInfo,
                                                        const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& fragConditionInfo)
    {
        // shaders() 深拷贝两份 SPIR-V + 反射表,每 draw 一次在压测下是主要开销。条件比较不需要
        // shader 载荷,先比、只有真要 rebuild 时才付这份拷贝。
        // 条件相同(或无 pipelineObject,此时两边都是默认值)时本就不该 rebuild。
        const bool vertex_changed = vert_condition_info_ != vertConditionInfo;
        const bool fragment_changed = frag_condition_info_ != fragConditionInfo;
        if (!vertex_changed && !fragment_changed)
            return false;

        std::shared_ptr<VulkanRasterizerPipeline> impl = pipeline_impl(ResourceBridge::token(*this));
        RasterizerPipelineShaders shaders = impl->shaders();
        if (!shaders.object)
            return false;

        auto& vc = shaders.object->vertex;
        auto& fc = shaders.object->fragment;
        const bool bindless = vc->getCompilerOption().enableBindless;
        if (vertex_changed)
            shaders.vertex = vc->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, bindless);
        if (fragment_changed)
            shaders.fragment = fc->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, fc->getCompilerOption().enableBindless);

        shaders.object->updateAutoBind(bindless, vertConditionInfo, fragConditionInfo);
        shaders.auto_bind_entries = shaders.object->autoBindEntries;
        rebuild_pipeline(impl->desc(), std::move(shaders), vertConditionInfo, fragConditionInfo);
        return true;
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
}
