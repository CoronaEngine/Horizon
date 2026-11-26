#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"

#include "Horizon.h"
#include <stdexcept>

namespace horizon::detail {

ComputePipelineVulkan& ComputePipelineState::backend() const
{
    if (!computePipeline_)
        throw std::runtime_error("Empty ComputePipeline<> handle");
    return *computePipeline_;
}

ComputePipelineState::ComputePipelineState(EmbeddedShader::ComputePipelineObject shader, const std::source_location& location)
    : computePipeline_(std::make_shared<ComputePipelineVulkan>(*shader.compute, location)),
      autoBindEntries_(std::move(shader.autoBindEntries))
{
}

ComputePipelineState::ComputePipelineState()
    : computePipeline_(std::make_shared<ComputePipelineVulkan>())
{
}

ComputePipelineState::ComputePipelineState(const std::string& source, const std::source_location& location)
    : computePipeline_(std::make_shared<ComputePipelineVulkan>(source, location))
{
}

ComputePipelineState::ComputePipelineState(const EmbeddedShader::ShaderCodeModule& module, const std::source_location& location)
    : computePipeline_(std::make_shared<ComputePipelineVulkan>(module, location))
{
}

void ComputePipelineState::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    backend().setPushConstantDirect(byteOffset, data, size, bindType);
}

void ComputePipelineState::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    backend().setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void ComputePipelineState::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t)
{
    backend().setResourceDirect(byteOffset, typeSize, image, bindType);
}

void ComputePipelineState::dispatch(uint16_t x, uint16_t y, uint16_t z)
{
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
}

} // namespace horizon::detail
