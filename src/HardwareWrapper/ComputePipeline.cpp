#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"

#include "Horizon.h"
#include <stdexcept>

template<class CS>
ComputePipeline<CS>::ComputePipeline()
    : impl_(std::make_unique<Impl>(std::make_shared<ComputePipelineVulkan>())) {}

template<class CS>
ComputePipeline<CS>::ComputePipeline(const EmbeddedShader::ShaderCodeModule& module)
    : impl_(std::make_unique<Impl>(std::make_shared<ComputePipelineVulkan>(module))) {}

template<class CS>
ComputePipeline<CS>::ComputePipeline(EmbeddedShader::ComputePipelineObject shader)
    : ComputePipeline(shader.compute->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, shader.conditions))
{
    impl_->autoBindEntries = std::move(shader.autoBindEntries);
    if (!shader.conditions.empty())
        impl_->variants = std::make_unique<EmbeddedShader::ComputePipelineObject>(std::move(shader));
}

template<class CS>
ComputePipeline<CS>::~ComputePipeline() = default;

template<class CS>
ComputePipeline<CS>::ComputePipeline(ComputePipeline&&) noexcept = default;

template<class CS>
ComputePipeline<CS>& ComputePipeline<CS>::operator=(ComputePipeline&&) noexcept = default;

template<class CS>
auto ComputePipeline<CS>::state() -> Impl&
{
    if (!impl_ || !impl_->backend) throw std::runtime_error("Empty ComputePipeline<> handle");
    return *impl_;
}

template<class CS>
void ComputePipeline<CS>::setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType)
{
    state().backend->setPushConstantDirect(byteOffset, data, size, bindType);
}

template<class CS>
void ComputePipeline<CS>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize,
                                           const HardwareBuffer& buffer, int32_t bindType)
{
    state().backend->setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

template<class CS>
void ComputePipeline<CS>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize,
                                           const HardwareImage& image, int32_t bindType, uint32_t)
{
    state().backend->setResourceDirect(byteOffset, typeSize, image, bindType);
}

template<class CS>
ComputePipeline<CS>& ComputePipeline<CS>::uses(std::span<const ResourceUse> resources)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->resourceUses.assign(resources.begin(), resources.end());
    return *this;
}

template<class CS>
HardwareExecutor::Command ComputePipeline<CS>::operator()(uint16_t x, uint16_t y, uint16_t z)
{
    auto& impl = state();
    if (auto& variants = impl.variants)
    {
        auto conditions = variants->compute->getCurrentConditionInfo();
        if (conditions != variants->conditions)
        {
            impl.backend = std::make_shared<ComputePipelineVulkan>(variants->updateAutoBind(conditions));
            impl.autoBindEntries = std::move(variants->autoBindEntries);
            variants->conditions = std::move(conditions);
        }
    }

    // Auto-bind: read current resource from each EDSL proxy's back-pointer
    auto& backend = *impl.backend;
    for (const auto& entry : impl.autoBindEntries)
    {
        if (entry.boundValueRef)
        {
            backend.setPushConstantDirect(entry.byteOffset, entry.boundValueRef, entry.boundValueSize, entry.bindType);
        }
        else if (entry.boundResourceRef && *entry.boundResourceRef)
        {
            void* res = *entry.boundResourceRef;
            if (entry.boundResourceIsBuffer)
                backend.setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareBuffer*>(res), entry.bindType);
            else
                backend.setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType);
        }
    }
    return HardwareExecutor::Command(std::make_shared<HardwareExecutor::Command::Impl::Dispatch>(
        impl.backend, ktm::uvec3{x, y, z}, impl.resourceUses));
}

template struct ComputePipeline<void>;
