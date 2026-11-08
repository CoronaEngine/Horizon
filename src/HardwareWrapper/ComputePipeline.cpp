#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"

#include "Horizon_old.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include <stdexcept>

namespace {
ComputePipelineVulkan* computeFromId(uintptr_t id)
{
    if (!id)
        throw std::runtime_error("Empty ComputePipelineBase handle");
    return reinterpret_cast<ComputePipelineVulkan*>(id);
}
} // namespace

// 辅助函数：从已编译的 ShaderCodeCompiler 创建 ComputePipeline
void computePipelineInitFromCompiler(std::uintptr_t &pipelineID,
                                      EmbeddedShader::ShaderCodeCompiler &compiler,
                                      const std::source_location &src)
{
    pipelineID = reinterpret_cast<uintptr_t>(new ComputePipelineVulkan(compiler, src));
}

ComputePipelineBase::ComputePipelineBase()
{
    computePipelineID = reinterpret_cast<uintptr_t>(new ComputePipelineVulkan());
}

ComputePipelineBase::ComputePipelineBase(const std::string &shaderCode, EmbeddedShader::ShaderLanguage language, const std::source_location &src)
{
    computePipelineID = reinterpret_cast<uintptr_t>(new ComputePipelineVulkan(shaderCode, language, src));
}

ComputePipelineBase::ComputePipelineBase(const EmbeddedShader::ShaderCodeModule &spirV, const std::source_location &src)
{
    computePipelineID = reinterpret_cast<uintptr_t>(new ComputePipelineVulkan(spirV, src));
}

ComputePipelineBase::ComputePipelineBase(ComputePipelineBase&& other) noexcept
    : computePipelineID(std::exchange(other.computePipelineID, 0)), autoBindEntries_(std::move(other.autoBindEntries_))
{
}

ComputePipelineBase::~ComputePipelineBase()
{
    delete reinterpret_cast<ComputePipelineVulkan*>(computePipelineID);
}

ComputePipelineBase& ComputePipelineBase::operator=(ComputePipelineBase&& other) noexcept
{
    if (this == &other)
        return *this;

    delete reinterpret_cast<ComputePipelineVulkan*>(computePipelineID);
    computePipelineID = std::exchange(other.computePipelineID, 0);
    autoBindEntries_ = std::move(other.autoBindEntries_);
    return *this;
}

void ComputePipelineBase::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    auto const handle = computeFromId(computePipelineID);
    handle->setPushConstantDirect(byteOffset, data, size, bindType);
}

void ComputePipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    auto const handle = computeFromId(computePipelineID);
    handle->setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void ComputePipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType)
{
    auto const handle = computeFromId(computePipelineID);
    handle->setResourceDirect(byteOffset, typeSize, image, bindType);
}

ComputePipelineBase &ComputePipelineBase::operator()(uint16_t x, uint16_t y, uint16_t z)
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
    auto const handle = computeFromId(computePipelineID);
    (*handle)(x, y, z);
    return *this;
}
