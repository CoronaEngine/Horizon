#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"

#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include <stdexcept>

ComputePipelineVulkan& ComputePipeline<>::backend() const
{
    if (!computePipeline_)
        throw std::runtime_error("Empty ComputePipeline<> handle");
    return *computePipeline_;
}

// 辅助函数：从已编译的 ShaderCodeCompiler 创建 ComputePipeline
void ComputePipeline<>::initFromCompiler(EmbeddedShader::ShaderCodeCompiler& compiler, const std::source_location& src)
{
    computePipeline_ = std::make_shared<ComputePipelineVulkan>(compiler, src);
}

ComputePipeline<>::ComputePipeline()
{
    computePipeline_ = std::make_shared<ComputePipelineVulkan>();
}

ComputePipeline<>::ComputePipeline(const std::string& source, const std::source_location& location)
    : computePipeline_(std::make_shared<ComputePipelineVulkan>(source, location))
{
}

ComputePipeline<>::ComputePipeline(const EmbeddedShader::ShaderCodeModule &spirV, const std::source_location &src)
{
    computePipeline_ = std::make_shared<ComputePipelineVulkan>(spirV, src);
}

ComputePipeline<>::ComputePipeline(ComputePipeline<>&&) noexcept = default;
ComputePipeline<>::~ComputePipeline() = default;
ComputePipeline<>& ComputePipeline<>::operator=(ComputePipeline<>&&) noexcept = default;

void ComputePipeline<>::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    backend().setPushConstantDirect(byteOffset, data, size, bindType);
}

void ComputePipeline<>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    backend().setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void ComputePipeline<>::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType)
{
    backend().setResourceDirect(byteOffset, typeSize, image, bindType);
}

ComputePipeline<> &ComputePipeline<>::operator()(uint16_t x, uint16_t y, uint16_t z)
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
    return *this;
}
