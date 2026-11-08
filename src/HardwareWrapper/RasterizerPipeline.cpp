#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"

#include "Horizon_old.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include <stdexcept>

namespace {
RasterizerPipelineWrap* rasterFromId(uintptr_t id)
{
    if (!id)
        throw std::runtime_error("Empty RasterizerPipelineWrap handle");
    return reinterpret_cast<RasterizerPipelineWrap*>(id);
}

void retainRaster(uintptr_t id)
{
    if (id)
        ++rasterFromId(id)->refCount;
}

void releaseRaster(uintptr_t id)
{
    if (!id)
        return;
    auto* handle = rasterFromId(id);
    if (--handle->refCount == 0)
    {
        delete handle->impl;
        delete handle;
    }
}
} // namespace

RasterizerPipelineBase::RasterizerPipelineBase()
{
    auto handle = std::make_unique<RasterizerPipelineWrap>();
    handle->impl = new RasterizerPipelineVulkan();
    rasterizerPipelineID = reinterpret_cast<uintptr_t>(handle.release());
}

RasterizerPipelineBase::RasterizerPipelineBase(std::string vs, std::string fs, uint32_t multiviewCount, EmbeddedShader::ShaderLanguage vlang, EmbeddedShader::ShaderLanguage flang, const std::source_location &src)
{
    auto handle = std::make_unique<RasterizerPipelineWrap>();
    handle->impl = new RasterizerPipelineVulkan(vs, fs, multiviewCount, vlang, flang, src);
    rasterizerPipelineID = reinterpret_cast<uintptr_t>(handle.release());
}

RasterizerPipelineBase::RasterizerPipelineBase(const EmbeddedShader::ShaderCodeModule &vertexSpirV, const EmbeddedShader::ShaderCodeModule &fragmentSpirV, uint32_t multiviewCount, const std::source_location &src)
{
    auto handle = std::make_unique<RasterizerPipelineWrap>();
    handle->impl = new RasterizerPipelineVulkan(vertexSpirV, fragmentSpirV, multiviewCount, src);
    rasterizerPipelineID = reinterpret_cast<uintptr_t>(handle.release());
}

// 辅助函数：从已编译的 ShaderCodeCompiler 初始化 RasterizerPipeline
void rasterizerPipelineInitFromCompiler(std::uintptr_t &pipelineID,
                                         EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                                         EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                                         uint32_t multiviewCount,
                                         const std::source_location &src)
{
    auto handle = std::make_unique<RasterizerPipelineWrap>();
    handle->impl = new RasterizerPipelineVulkan(vertexCompiler, fragmentCompiler, multiviewCount, src);
    pipelineID = reinterpret_cast<uintptr_t>(handle.release());
}

RasterizerPipelineBase::RasterizerPipelineBase(const RasterizerPipelineBase& other)
    : rasterizerPipelineID(other.rasterizerPipelineID), autoBindEntries_(other.autoBindEntries_)
{
    retainRaster(rasterizerPipelineID);
}

RasterizerPipelineBase::RasterizerPipelineBase(RasterizerPipelineBase&& other) noexcept
    : rasterizerPipelineID(std::exchange(other.rasterizerPipelineID, 0)), autoBindEntries_(std::move(other.autoBindEntries_))
{
}

RasterizerPipelineBase::~RasterizerPipelineBase()
{
    releaseRaster(rasterizerPipelineID);
}

RasterizerPipelineBase& RasterizerPipelineBase::operator=(const RasterizerPipelineBase& other)
{
    if (rasterizerPipelineID == other.rasterizerPipelineID)
        return *this;

    auto entries = other.autoBindEntries_;
    retainRaster(other.rasterizerPipelineID);
    releaseRaster(rasterizerPipelineID);
    rasterizerPipelineID = other.rasterizerPipelineID;
    autoBindEntries_ = std::move(entries);
    return *this;
}

RasterizerPipelineBase& RasterizerPipelineBase::operator=(RasterizerPipelineBase&& other) noexcept
{
    if (this == &other)
        return *this;

    releaseRaster(rasterizerPipelineID);
    rasterizerPipelineID = std::exchange(other.rasterizerPipelineID, 0);
    autoBindEntries_ = std::move(other.autoBindEntries_);
    return *this;
}

void RasterizerPipelineBase::setDepthImage(HardwareImage &depthImage)
{
    auto handle = rasterFromId(rasterizerPipelineID);
    handle->impl->setDepthImage(depthImage);
}

void RasterizerPipelineBase::setDepthEnabled(bool enabled)
{
    auto handle = rasterFromId(rasterizerPipelineID);
    handle->impl->setDepthEnabled(enabled);
}

//void RasterizerPipeline::setDepthWriteEnabled(bool enabled)
//{
//    auto handle = rasterFromId(rasterizerPipelineID);
//    handle->impl->setDepthEnabled(enabled);
//}

HardwareImage RasterizerPipelineBase::getDepthImage()
{
    HardwareImage img;
    auto handle = rasterFromId(rasterizerPipelineID);
    img = handle->impl->getDepthImage();
    return img;
}

void RasterizerPipelineBase::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    auto handle = rasterFromId(rasterizerPipelineID);
    handle->impl->setPushConstantDirect(byteOffset, data, size, bindType);
}

void RasterizerPipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    auto handle = rasterFromId(rasterizerPipelineID);
    handle->impl->setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void RasterizerPipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location)
{
    auto handle = rasterFromId(rasterizerPipelineID);
    handle->impl->setResourceDirect(byteOffset, typeSize, image, bindType, location);
}

RasterizerPipelineBase &RasterizerPipelineBase::operator()(uint16_t width, uint16_t height)
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
                setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType, entry.location);
        }
    }
    auto handle = rasterFromId(rasterizerPipelineID);
    (*handle->impl)(width, height);
    return *this;
}

RasterizerPipelineBase &RasterizerPipelineBase::record(const HardwareBuffer &indexBuffer, const HardwareBuffer &vertexBuffer)
{
    DrawIndexedParams params;
    return record(indexBuffer, vertexBuffer, params);
}

RasterizerPipelineBase &RasterizerPipelineBase::record(const HardwareBuffer &indexBuffer,
                                               const HardwareBuffer &vertexBuffer,
                                               const DrawIndexedParams &params)
{
    auto handle = rasterFromId(rasterizerPipelineID);
    handle->impl->record(indexBuffer, vertexBuffer, params);
    return *this;
}
