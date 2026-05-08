#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"

#include "Horizon.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include "corona/kernel/utils/storage.h"

static void incRaster(uint32_t id, const Corona::Kernel::Utils::Storage<RasterizerPipelineWrap>::WriteHandle &handle)
{
    ++handle->refCount;
    // CFW_LOG_TRACE("RasterizerPipeline ref++: id={}, count={}", id, handle->refCount);
}

static bool decRaster(uint32_t id, const Corona::Kernel::Utils::Storage<RasterizerPipelineWrap>::WriteHandle &handle)
{
    int count = --handle->refCount;
    // CFW_LOG_TRACE("RasterizerPipeline ref--: id={}, count={}", id, count);
    if (count == 0)
    {
        delete handle->impl;
        handle->impl = nullptr;
        // CFW_LOG_TRACE("RasterizerPipeline destroyed: id={}", id);
        return true;
    }
    return false;
}

RasterizerPipelineBase::RasterizerPipelineBase()
{
    auto id = gRasterizerPipelineStorage.allocate();
    rasterizerPipelineID.store(id, std::memory_order_release);
    auto handle = gRasterizerPipelineStorage.acquire_write(id);
    handle->impl = new RasterizerPipelineVulkan();
    // CFW_LOG_TRACE("RasterizerPipelineBase created: id={}", id);
}

RasterizerPipelineBase::RasterizerPipelineBase(std::string vs, std::string fs, uint32_t multiviewCount, EmbeddedShader::ShaderLanguage vlang, EmbeddedShader::ShaderLanguage flang, const std::source_location &src)
{
    auto id = gRasterizerPipelineStorage.allocate();
    rasterizerPipelineID.store(id, std::memory_order_release);
    auto handle = gRasterizerPipelineStorage.acquire_write(id);
    handle->impl = new RasterizerPipelineVulkan(vs, fs, multiviewCount, vlang, flang, src);
    // CFW_LOG_TRACE("RasterizerPipelineBase created: id={}", id);
}

RasterizerPipelineBase::RasterizerPipelineBase(const std::vector<uint32_t> &vertexSpirV, const std::vector<uint32_t> &fragmentSpirV, uint32_t multiviewCount, const std::source_location &src)
{
    auto id = gRasterizerPipelineStorage.allocate();
    rasterizerPipelineID.store(id, std::memory_order_release);
    auto handle = gRasterizerPipelineStorage.acquire_write(id);
    handle->impl = new RasterizerPipelineVulkan(vertexSpirV, fragmentSpirV, multiviewCount, src);
}

// 辅助函数：从已编译的 ShaderCodeCompiler 初始化 RasterizerPipeline
void rasterizerPipelineInitFromCompiler(std::atomic<std::uintptr_t> &pipelineID,
                                         const EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                                         const EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                                         uint32_t multiviewCount,
                                         const std::source_location &src)
{
    auto id = gRasterizerPipelineStorage.allocate();
    pipelineID.store(id, std::memory_order_release);
    auto handle = gRasterizerPipelineStorage.acquire_write(id);
    handle->impl = new RasterizerPipelineVulkan(vertexCompiler, fragmentCompiler, multiviewCount, src);
}

RasterizerPipelineBase::RasterizerPipelineBase(const RasterizerPipelineBase &other)
{
    std::lock_guard<std::mutex> lock(other.rasterizerPipelineMutex);
    auto const other_id = other.rasterizerPipelineID.load(std::memory_order_acquire);
    rasterizerPipelineID.store(other_id, std::memory_order_release);
    autoBindEntries_ = other.autoBindEntries_;
    if (other_id > 0)
    {
        auto const handle = gRasterizerPipelineStorage.acquire_write(other_id);
        incRaster(other_id, handle);
    }
}

RasterizerPipelineBase::RasterizerPipelineBase(RasterizerPipelineBase &&other) noexcept
{
    std::lock_guard<std::mutex> lock(other.rasterizerPipelineMutex);
    auto const other_id = other.rasterizerPipelineID.load(std::memory_order_acquire);
    rasterizerPipelineID.store(other_id, std::memory_order_release);
    other.rasterizerPipelineID.store(0, std::memory_order_release);
    autoBindEntries_ = std::move(other.autoBindEntries_);
}

RasterizerPipelineBase::~RasterizerPipelineBase()
{
    auto const self_id = rasterizerPipelineID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        bool destroy = false;
        if (auto const handle = gRasterizerPipelineStorage.acquire_write(self_id); decRaster(self_id, handle))
        {
            destroy = true;
        }
        if (destroy)
        {
            gRasterizerPipelineStorage.deallocate(self_id);
        }
        rasterizerPipelineID.store(0, std::memory_order_release);
    }
}

RasterizerPipelineBase &RasterizerPipelineBase::operator=(const RasterizerPipelineBase &other)
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(rasterizerPipelineMutex, other.rasterizerPipelineMutex);
    auto const self_id = rasterizerPipelineID.load(std::memory_order_acquire);
    auto const other_id = other.rasterizerPipelineID.load(std::memory_order_acquire);

    if (self_id == 0 && other_id == 0)
    {
        return *this;
    }
    if (self_id == other_id)
    {
        return *this;
    }

    if (other_id == 0)
    {
        bool should_destroy_self = false;
        if (auto const self_handle = gRasterizerPipelineStorage.acquire_write(self_id);
            decRaster(self_id, self_handle))
        {
            should_destroy_self = true;
        }
        if (should_destroy_self)
        {
            gRasterizerPipelineStorage.deallocate(self_id);
        }
        rasterizerPipelineID.store(0, std::memory_order_release);
        autoBindEntries_.clear();
        return *this;
    }

    if (self_id == 0)
    {
        rasterizerPipelineID.store(other_id, std::memory_order_release);
        autoBindEntries_ = other.autoBindEntries_;
        auto const other_handle = gRasterizerPipelineStorage.acquire_write(other_id);
        incRaster(other_id, other_handle);
        return *this;
    }

    bool should_destroy_self = false;
    if (self_id < other_id)
    {
        auto const self_handle = gRasterizerPipelineStorage.acquire_write(self_id);
        auto const other_handle = gRasterizerPipelineStorage.acquire_write(other_id);
        incRaster(other_id, other_handle);
        if (decRaster(self_id, self_handle))
        {
            should_destroy_self = true;
        }
    }
    else
    {
        auto const other_handle = gRasterizerPipelineStorage.acquire_write(other_id);
        auto const self_handle = gRasterizerPipelineStorage.acquire_write(self_id);
        incRaster(other_id, other_handle);
        if (decRaster(self_id, self_handle))
        {
            should_destroy_self = true;
        }
    }
    if (should_destroy_self)
    {
        gRasterizerPipelineStorage.deallocate(self_id);
    }
    rasterizerPipelineID.store(other_id, std::memory_order_release);
    autoBindEntries_ = other.autoBindEntries_;
    return *this;
}

RasterizerPipelineBase &RasterizerPipelineBase::operator=(RasterizerPipelineBase &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(rasterizerPipelineMutex, other.rasterizerPipelineMutex);
    auto const self_id = rasterizerPipelineID.load(std::memory_order_acquire);
    auto const other_id = other.rasterizerPipelineID.load(std::memory_order_acquire);

    if (self_id > 0)
    {
        bool should_destroy_self = false;
        if (auto const self_handle = gRasterizerPipelineStorage.acquire_write(self_id);
            decRaster(self_id, self_handle))
        {
            should_destroy_self = true;
        }

        if (should_destroy_self)
        {
            gRasterizerPipelineStorage.deallocate(self_id);
        }
    }
    rasterizerPipelineID.store(other_id, std::memory_order_release);
    other.rasterizerPipelineID.store(0, std::memory_order_release);
    autoBindEntries_ = std::move(other.autoBindEntries_);
    return *this;
}

void RasterizerPipelineBase::setDepthImage(HardwareImage &depthImage)
{
    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
    handle->impl->setDepthImage(depthImage);
}

void RasterizerPipelineBase::setDepthEnabled(bool enabled)
{
    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
    handle->impl->setDepthEnabled(enabled);
}

//void RasterizerPipeline::setDepthWriteEnabled(bool enabled)
//{
//    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
//    handle->impl->setDepthEnabled(enabled);
//}

HardwareImage RasterizerPipelineBase::getDepthImage()
{
    HardwareImage img;
    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
    img = handle->impl->getDepthImage();
    return img;
}

void RasterizerPipelineBase::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
    handle->impl->setPushConstantDirect(byteOffset, data, size, bindType);
}

void RasterizerPipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
    handle->impl->setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void RasterizerPipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location)
{
    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
    handle->impl->setResourceDirect(byteOffset, typeSize, image, bindType, location);
}

RasterizerPipelineBase &RasterizerPipelineBase::operator()(uint16_t width, uint16_t height)
{
    // Auto-bind: read current resource from each EDSL proxy's back-pointer
    for (const auto& entry : autoBindEntries_)
    {
        if (void* res = *entry.boundResourceRef)
        {
            setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType, entry.location);
        }
    }
    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
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
    auto handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipelineID.load(std::memory_order_acquire));
    handle->impl->record(indexBuffer, vertexBuffer, params);
    return *this;
}