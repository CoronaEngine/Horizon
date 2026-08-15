#include "hardware_wrapper_vulkan/pipeline/vulkan_compute_pipeline.h"
#include "hardware_wrapper_vulkan/resource_pool.h"
#include "horizon.h"

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Corona::Horizon
{
    namespace
    {
        using ComputePipelineStore = ResourceStore<ComputePipelineWrap, NoopReleaser>;

        // 同光栅侧：只读槽位里的 impl 指针，共享锁足够，不必独占。
        [[nodiscard]] std::shared_ptr<VulkanComputePipeline> pipeline_impl(const std::shared_ptr<IResourceRef>& token)
        {
            auto pipeline = read<ComputePipelineStore>(token);
            if (!pipeline || !pipeline->impl)
                throw std::logic_error("ComputePipeline does not reference a valid implementation.");

            auto impl = std::static_pointer_cast<VulkanComputePipeline>(pipeline->impl);
            if (!impl)
                throw std::logic_error("ComputePipeline implementation has an unexpected type.");

            return impl;
        }

        [[nodiscard]] std::shared_ptr<IResourceRef> make_pipeline_token(ComputePipelineDesc desc,
                                                                        ComputePipelineShaders shaders,
                                                                        const std::source_location& source_location)
        {
            auto handle = resource_pool().compute_pipelines.create(
                [desc = std::move(desc), shaders = std::move(shaders), source_location]() mutable {
                    ComputePipelineWrap wrap;
                    wrap.impl = std::make_shared<VulkanComputePipeline>(std::move(desc), std::move(shaders), source_location);
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

    ComputePipelineBase::ComputePipelineBase(ComputePipelineDesc desc,
                                             ComputePipelineShaders shaders,
                                             const std::source_location& source_location)
        : location_(source_location)
    {
        if (shaders.object)
        {
            const auto& info = shaders.object->compute->getCurrentConditionInfo();
            rebuild_pipeline(std::move(desc), std::move(shaders), info);
            return;
        }
        rebuild_pipeline(std::move(desc), std::move(shaders), {});
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

    void ComputePipelineBase::rebuild_pipeline(ComputePipelineDesc desc,
                                               ComputePipelineShaders shaders,
                                               const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& conditionInfo)
    {
        auto object = shaders.object;
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
        auto pipeline = make_pipeline_token(std::move(desc), std::move(shaders), location_);
        if (object)
        {
            pipeline_pool_.insert({ object->getCombinedKey(conditionInfo), pipeline });
        }
        ResourceBridge::set(*this, std::move(pipeline));
    }

    bool ComputePipelineBase::sync_shader_conditions(const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& conditionInfo)
    {
        // shaders() 深拷贝 SPIR-V + 反射,每 dispatch 一次在压测下是主要开销。条件比较不需要
        // shader 载荷,先比、只有真要 rebuild 时才付这份拷贝。
        if (conditionInfo == condition_info_)
            return false;

        std::shared_ptr<VulkanComputePipeline> impl = pipeline_impl(ResourceBridge::token(*this));
        ComputePipelineShaders shaders = impl->shaders();
        if (!shaders.object)
            return false;

        auto& cc = shaders.object->compute;
        const bool bindless = cc->getCompilerOption().enableBindless;
        shaders.compute = cc->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, bindless);
        shaders.object->updateAutoBind(bindless, conditionInfo);
        shaders.auto_bind_entries = shaders.object->autoBindEntries;
        rebuild_pipeline(impl->desc(), std::move(shaders), conditionInfo);
        return true;
    }

    ComputePipelineBase& ComputePipelineBase::groups(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
    {
        // set_dispatch 收 uint16_t；公共签名用 uint32_t（与 dispatch_extent 的像素
        // 尺寸同类型，避免调用方在两个入口间换整型），这里做一次范围检查再窄化。
        constexpr uint32_t max_groups = std::numeric_limits<uint16_t>::max();
        if (groups_x > max_groups || groups_y > max_groups || groups_z > max_groups)
            throw std::out_of_range("ComputePipeline dispatch group count exceeds 65535 per dimension.");

        std::shared_ptr<VulkanComputePipeline> impl = pipeline_impl(ResourceBridge::token(*this));
        bind_auto_resources(impl);
        impl->set_dispatch(static_cast<uint16_t>(groups_x),
                           static_cast<uint16_t>(groups_y),
                           static_cast<uint16_t>(groups_z));
        return *this;
    }

    ComputePipelineBase& ComputePipelineBase::dispatch_extent(uint32_t width, uint32_t height)
    {
        const ktm::uvec3 tgs = pipeline_impl(ResourceBridge::token(*this))->resolved_thread_group_size();
        if (tgs.x == 0 || tgs.y == 0)
            throw std::logic_error("ComputePipeline workgroup local size is zero; reflection failed and no override was provided.");

        return groups((width + tgs.x - 1u) / tgs.x,
                      (height + tgs.y - 1u) / tgs.y,
                      1u);
    }

    void ComputePipelineBase::record_into(CommandRecorder& recorder)
    {
        std::shared_ptr<VulkanComputePipeline> impl = pipeline_impl(ResourceBridge::token(*this));
        bind_auto_resources(impl);
        impl->record_into(*this, recorder);
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
