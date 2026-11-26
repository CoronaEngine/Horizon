#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"

#include "Horizon.h"
#include <stdexcept>

template<class CS>
ComputePipeline<CS>::ComputePipeline()
    : computePipeline_(std::make_shared<ComputePipelineVulkan>()) {}

template<class CS>
ComputePipeline<CS>::ComputePipeline(const EmbeddedShader::ShaderCodeModule& module)
    : computePipeline_(std::make_shared<ComputePipelineVulkan>(module)) {}

template<class CS>
ComputePipeline<CS>::ComputePipeline(EmbeddedShader::ComputePipelineObject shader)
    : computePipeline_(std::make_shared<ComputePipelineVulkan>(
          shader.compute->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, shader.conditions))),
      autoBindEntries_(std::move(shader.autoBindEntries))
{
    if (!shader.conditions.empty())
        variants_ = std::make_unique<EmbeddedShader::ComputePipelineObject>(std::move(shader));
}

template<class CS>
ComputePipelineVulkan& ComputePipeline<CS>::backend() const
{
    if (!computePipeline_) throw std::runtime_error("Empty ComputePipeline<> handle");
    return *computePipeline_;
}

template<class CS>
void ComputePipeline<CS>::setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType)
{
    backend().setPushConstantDirect(byteOffset, data, size, bindType);
}

template<class CS>
void ComputePipeline<CS>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize,
                                           const HardwareBuffer& buffer, int32_t bindType)
{
    backend().setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

template<class CS>
void ComputePipeline<CS>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize,
                                           const HardwareImage& image, int32_t bindType, uint32_t)
{
    backend().setResourceDirect(byteOffset, typeSize, image, bindType);
}

template<class CS>
ComputePipeline<CS>& ComputePipeline<CS>::operator()(uint16_t x, uint16_t y, uint16_t z)
{
    if (variants_)
    {
        auto conditions = variants_->compute->getCurrentConditionInfo();
        if (conditions != variants_->conditions)
        {
            computePipeline_ = std::make_shared<ComputePipelineVulkan>(variants_->updateAutoBind(conditions));
            autoBindEntries_ = std::move(variants_->autoBindEntries);
            variants_->conditions = std::move(conditions);
        }
    }

    // Auto-bind: read current resource from each EDSL proxy's back-pointer
    for (const auto& entry : autoBindEntries_)
    {
        if (entry.boundValueRef)
        {
            setPushConstantDirect(entry.byteOffset, entry.boundValueRef, entry.boundValueSize, entry.bindType);
        }
        else if (entry.boundResourceRef && *entry.boundResourceRef)
        {
            void* res = *entry.boundResourceRef;
            if (entry.boundResourceIsBuffer)
                setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareBuffer*>(res), entry.bindType);
            else
                setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType);
        }
    }
    backend()(x, y, z);
    return *this;
}

template struct ComputePipeline<void>;
