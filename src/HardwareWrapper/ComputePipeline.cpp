#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"

#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include "corona/kernel/utils/storage.h"

static void incCompute(uint32_t id, const Corona::Kernel::Utils::Storage<ComputePipelineWrap>::WriteHandle &write_handle)
{
    ++write_handle->refCount;
    // CFW_LOG_TRACE("ComputePipeline ref++: id={}, count={}", id, write_handle->refCount);
}

static bool decCompute(uint32_t id, const Corona::Kernel::Utils::Storage<ComputePipelineWrap>::WriteHandle &write_handle)
{
    int count = --write_handle->refCount;
    // CFW_LOG_TRACE("ComputePipeline ref--: id={}, count={}", id, count);
    if (count == 0)
    {
        delete write_handle->impl;
        write_handle->impl = nullptr;
        // CFW_LOG_TRACE("ComputePipeline destroyed: id={}", id);
        return true;
    }
    return false;
}

// 辅助函数：从已编译的 ShaderCodeCompiler 创建 ComputePipeline
void computePipelineInitFromCompiler(std::atomic<std::uintptr_t> &pipelineID,
                                      const EmbeddedShader::ShaderCodeCompiler &compiler,
                                      const std::source_location &src)
{
    auto const id = gComputePipelineStorage.allocate();
    pipelineID.store(id, std::memory_order_release);
    auto const handle = gComputePipelineStorage.acquire_write(id);
    handle->impl = new ComputePipelineVulkan(compiler, src);
}

ComputePipelineBase::ComputePipelineBase()
{
    auto const id = gComputePipelineStorage.allocate();
    computePipelineID.store(id, std::memory_order_release);
    auto const handle = gComputePipelineStorage.acquire_write(id);
    handle->impl = new ComputePipelineVulkan();
    // CFW_LOG_TRACE("ComputePipelineBase created: id={}", id);
}

ComputePipelineBase::ComputePipelineBase(const std::string &shaderCode, EmbeddedShader::ShaderLanguage language, const std::source_location &src)
{
    auto const id = gComputePipelineStorage.allocate();
    computePipelineID.store(id, std::memory_order_release);
    auto const handle = gComputePipelineStorage.acquire_write(id);
    handle->impl = new ComputePipelineVulkan(shaderCode, language, src);
    // CFW_LOG_TRACE("ComputePipelineBase created: id={}", id);
}

ComputePipelineBase::ComputePipelineBase(const std::vector<uint32_t> &spirV, const std::source_location &src)
{
    auto const id = gComputePipelineStorage.allocate();
    computePipelineID.store(id, std::memory_order_release);
    auto const handle = gComputePipelineStorage.acquire_write(id);
    handle->impl = new ComputePipelineVulkan(spirV, src);
}

ComputePipelineBase::ComputePipelineBase(const ComputePipelineBase &other)
{
    std::lock_guard<std::mutex> lock(other.computePipelineMutex);
    auto const other_id = other.computePipelineID.load(std::memory_order_acquire);
    computePipelineID.store(other_id, std::memory_order_release);
    autoBindEntries_ = other.autoBindEntries_;
    if (other_id > 0)
    {
        auto const write_handle = gComputePipelineStorage.acquire_write(other_id);
        incCompute(other_id, write_handle);
    }
}

ComputePipelineBase::ComputePipelineBase(ComputePipelineBase &&other) noexcept
{
    std::lock_guard<std::mutex> lock(other.computePipelineMutex);
    auto const other_id = other.computePipelineID.load(std::memory_order_acquire);
    computePipelineID.store(other_id, std::memory_order_release);
    other.computePipelineID.store(0, std::memory_order_release);
    autoBindEntries_ = std::move(other.autoBindEntries_);
}

ComputePipelineBase::~ComputePipelineBase()
{
    auto const self_id = computePipelineID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        bool destroy = false;
        if (auto const write_handle = gComputePipelineStorage.acquire_write(self_id); decCompute(self_id, write_handle))
        {
            destroy = true;
        }

        if (destroy)
        {
            gComputePipelineStorage.deallocate(self_id);
        }
        computePipelineID.store(0, std::memory_order_release);
    }
}

ComputePipelineBase &ComputePipelineBase::operator=(const ComputePipelineBase &other)
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(computePipelineMutex, other.computePipelineMutex);
    auto const self_id = computePipelineID.load(std::memory_order_acquire);
    auto const other_id = other.computePipelineID.load(std::memory_order_acquire);

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
        if (auto const self_handle = gComputePipelineStorage.acquire_write(self_id);
            decCompute(self_id, self_handle))
        {
            should_destroy_self = true;
        }

        if (should_destroy_self)
        {
            gComputePipelineStorage.deallocate(self_id);
        }
        computePipelineID.store(0, std::memory_order_release);
        autoBindEntries_.clear();
        return *this;
    }

    if (self_id == 0)
    {
        computePipelineID.store(other_id, std::memory_order_release);
        autoBindEntries_ = other.autoBindEntries_;
        auto const other_handle = gComputePipelineStorage.acquire_write(other_id);
        incCompute(other_id, other_handle);
        return *this;
    }

    bool should_destroy_self = false;
    if (self_id < other_id)
    {
        auto const self_handle = gComputePipelineStorage.acquire_write(self_id);
        auto const other_handle = gComputePipelineStorage.acquire_write(other_id);
        incCompute(other_id, other_handle);
        if (decCompute(self_id, self_handle))
        {
            should_destroy_self = true;
        }
    }
    else
    {
        auto const other_handle = gComputePipelineStorage.acquire_write(other_id);
        auto const self_handle = gComputePipelineStorage.acquire_write(self_id);
        incCompute(other_id, other_handle);
        if (decCompute(self_id, self_handle))
        {
            should_destroy_self = true;
        }
    }

    if (should_destroy_self)
    {
        gComputePipelineStorage.deallocate(self_id);
    }
    computePipelineID.store(other_id, std::memory_order_release);
    autoBindEntries_ = other.autoBindEntries_;
    return *this;
}

ComputePipelineBase &ComputePipelineBase::operator=(ComputePipelineBase &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(computePipelineMutex, other.computePipelineMutex);
    auto const self_id = computePipelineID.load(std::memory_order_acquire);
    auto const other_id = other.computePipelineID.load(std::memory_order_acquire);

    if (self_id > 0)
    {
        bool should_destroy_self = false;
        if (auto const self_handle = gComputePipelineStorage.acquire_write(self_id);
            decCompute(self_id, self_handle))
        {
            should_destroy_self = true;
        }
        if (should_destroy_self)
        {
            gComputePipelineStorage.deallocate(self_id);
        }
    }
    computePipelineID.store(other_id, std::memory_order_release);
    other.computePipelineID.store(0, std::memory_order_release);
    autoBindEntries_ = std::move(other.autoBindEntries_);
    return *this;
}

void ComputePipelineBase::setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType)
{
    auto const handle = gComputePipelineStorage.acquire_read(computePipelineID.load(std::memory_order_acquire));
    handle->impl->setPushConstantDirect(byteOffset, data, size, bindType);
}

void ComputePipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType)
{
    auto const handle = gComputePipelineStorage.acquire_read(computePipelineID.load(std::memory_order_acquire));
    handle->impl->setResourceDirect(byteOffset, typeSize, buffer, bindType);
}

void ComputePipelineBase::setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType)
{
    auto const handle = gComputePipelineStorage.acquire_read(computePipelineID.load(std::memory_order_acquire));
    handle->impl->setResourceDirect(byteOffset, typeSize, image, bindType);
}

ComputePipelineBase &ComputePipelineBase::operator()(uint16_t x, uint16_t y, uint16_t z)
{
    // Auto-bind: read current resource from each EDSL proxy's back-pointer
    for (const auto& entry : autoBindEntries_)
    {
        if (void* res = *entry.boundResourceRef)
        {
            setResourceDirect(entry.byteOffset, entry.typeSize, *static_cast<HardwareImage*>(res), entry.bindType);
        }
    }
    auto const handle = gComputePipelineStorage.acquire_read(computePipelineID.load(std::memory_order_acquire));
    (*handle->impl)(x, y, z);
    return *this;
}