#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"

#include "Horizon_old.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include <stdexcept>

namespace {
ComputePipelineWrap* computeFromId(uintptr_t id)
{
    if (!id)
        throw std::runtime_error("Empty ComputePipelineWrap handle");
    return reinterpret_cast<ComputePipelineWrap*>(id);
}

void retainCompute(uintptr_t id)
{
    if (id)
        ++computeFromId(id)->refCount;
}

void releaseCompute(uintptr_t id)
{
    if (!id)
        return;
    auto* handle = computeFromId(id);
    if (--handle->refCount == 0)
    {
        delete handle->impl;
        delete handle;
    }
}
} // namespace

// 辅助函数：从已编译的 ShaderCodeCompiler 创建 ComputePipeline
void computePipelineInitFromCompiler(std::uintptr_t &pipelineID,
                                      EmbeddedShader::ShaderCodeCompiler &compiler,
                                      const std::source_location &src)
{
    auto handle = std::make_unique<ComputePipelineWrap>();
    handle->impl = new ComputePipelineVulkan(compiler, src);
    pipelineID = reinterpret_cast<uintptr_t>(handle.release());
}

ComputePipelineBase::ComputePipelineBase()
{
    auto handle = std::make_unique<ComputePipelineWrap>();
    handle->impl = new ComputePipelineVulkan();
    computePipelineID = reinterpret_cast<uintptr_t>(handle.release());
}

ComputePipelineBase::ComputePipelineBase(const std::string &shaderCode, EmbeddedShader::ShaderLanguage language, const std::source_location &src)
{
    auto handle = std::make_unique<ComputePipelineWrap>();
    handle->impl = new ComputePipelineVulkan(shaderCode, language, src);
    computePipelineID = reinterpret_cast<uintptr_t>(handle.release());
}

ComputePipelineBase::ComputePipelineBase(const EmbeddedShader::ShaderCodeModule &spirV, const std::source_location &src)
{
    auto handle = std::make_unique<ComputePipelineWrap>();
    handle->impl = new ComputePipelineVulkan(spirV, src);
    computePipelineID = reinterpret_cast<uintptr_t>(handle.release());
}

ComputePipelineBase::ComputePipelineBase(const ComputePipelineBase& other)
    : computePipelineID(other.computePipelineID), autoBindEntries_(other.autoBindEntries_)
{
    retainCompute(computePipelineID);
}

ComputePipelineBase::ComputePipelineBase(ComputePipelineBase&& other) noexcept
    : computePipelineID(std::exchange(other.computePipelineID, 0)), autoBindEntries_(std::move(other.autoBindEntries_))
{
}

ComputePipelineBase::~ComputePipelineBase()
{
    releaseCompute(computePipelineID);
}

ComputePipelineBase& ComputePipelineBase::operator=(const ComputePipelineBase& other)
{
    if (computePipelineID == other.computePipelineID)
        return *this;

    auto entries = other.autoBindEntries_;
    retainCompute(other.computePipelineID);
    releaseCompute(computePipelineID);
    computePipelineID = other.computePipelineID;
    autoBindEntries_ = std::move(entries);
    return *this;
}

ComputePipelineBase& ComputePipelineBase::operator=(ComputePipelineBase&& other) noexcept
{
    if (this == &other)
        return *this;

    releaseCompute(computePipelineID);
    computePipelineID = std::exchange(other.computePipelineID, 0);
    autoBindEntries_ = std::move(other.autoBindEntries_);
    return *this;
}

void ComputePipelineBase::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    auto const handle = computeFromId(computePipelineID);
    handle->impl->setPushConstantDirect(byteOffset, data, size, bindType);
}

void ComputePipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    auto const handle = computeFromId(computePipelineID);
    handle->impl->setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void ComputePipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType)
{
    auto const handle = computeFromId(computePipelineID);
    handle->impl->setResourceDirect(byteOffset, typeSize, image, bindType);
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
    (*handle->impl)(x, y, z);
    return *this;
}
