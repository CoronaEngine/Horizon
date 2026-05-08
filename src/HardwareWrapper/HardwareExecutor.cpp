#include "Horizon.h"
#include "HardwareCommands.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"
#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include "corona/kernel/utils/storage.h"

static void incExec(uint32_t id, const Corona::Kernel::Utils::Storage<ExecutorWrap>::WriteHandle &handle)
{
    ++handle->refCount;
    // CFW_LOG_TRACE("HardwareExecutor ref++: id={}, count={}", id, handle->refCount);
}

static bool decExec(uint32_t id, const Corona::Kernel::Utils::Storage<ExecutorWrap>::WriteHandle &handle)
{
    int count = --handle->refCount;
    // CFW_LOG_TRACE("HardwareExecutor ref--: id={}, count={}", id, count);
    if (count == 0)
    {
        delete handle->impl;
        handle->impl = nullptr;
        // CFW_LOG_TRACE("HardwareExecutor destroyed: id={}", id);
        return true;
    }
    return false;
}

HardwareExecutor::HardwareExecutor() : executorID(gExecutorStorage.allocate())
{
    auto const self_id = executorID.load(std::memory_order_acquire);
    auto handle = gExecutorStorage.acquire_write(self_id);
    handle->impl = new HardwareExecutorVulkan();
    // CFW_LOG_TRACE("HardwareExecutor created: id={}", self_id);
}

HardwareExecutor::HardwareExecutor(const HardwareExecutor &other)
{
    std::lock_guard<std::mutex> lock(other.executorMutex);
    executorID.store(other.executorID.load(std::memory_order_acquire), std::memory_order_release);
    auto const self_id = executorID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        auto const handle = gExecutorStorage.acquire_write(self_id);
        incExec(self_id, handle);
    }
}

HardwareExecutor::HardwareExecutor(HardwareExecutor &&other) noexcept
{
    std::lock_guard<std::mutex> lock(other.executorMutex);
    executorID.store(other.executorID.load(std::memory_order_acquire), std::memory_order_release);
    other.executorID.store(0, std::memory_order_release);
}

HardwareExecutor::~HardwareExecutor()
{
    auto const self_id = executorID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        bool should_destroy_self = false;
        if (auto const handle = gExecutorStorage.acquire_write(self_id);
            decExec(self_id, handle))
        {
            should_destroy_self = true;
        }
        if (should_destroy_self)
        {
            gExecutorStorage.deallocate(self_id);
        }
        executorID.store(0, std::memory_order_release);
    }
}

HardwareExecutor &HardwareExecutor::operator=(const HardwareExecutor &other)
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(executorMutex, other.executorMutex);
    auto const self_id = executorID.load(std::memory_order_acquire);
    auto const other_id = other.executorID.load(std::memory_order_acquire);

    if (self_id == 0 && other_id == 0)
    {
        return *this;
    }
    if (self_id == other_id)
    {
        return *this;
    }

    bool should_destroy_self = false;
    if (other_id == 0)
    {
        if (auto const self_handle = gExecutorStorage.acquire_write(self_id);
            decExec(self_id, self_handle))
        {
            should_destroy_self = true;
        }
        if (should_destroy_self)
        {
            gExecutorStorage.deallocate(self_id);
        }
        executorID.store(0, std::memory_order_release);
        return *this;
    }

    if (self_id == 0)
    {
        executorID.store(other_id, std::memory_order_release);
        auto const other_handle = gExecutorStorage.acquire_write(other_id);
        incExec(other_id, other_handle);
        return *this;
    }

    if (self_id < other_id)
    {
        auto const self_handle = gExecutorStorage.acquire_write(self_id);
        auto const other_handle = gExecutorStorage.acquire_write(other_id);
        incExec(other_id, other_handle);
        if (decExec(self_id, self_handle))
        {
            should_destroy_self = true;
        }
    }
    else
    {
        auto const other_handle = gExecutorStorage.acquire_write(other_id);
        auto const self_handle = gExecutorStorage.acquire_write(self_id);
        incExec(other_id, other_handle);
        if (decExec(self_id, self_handle))
        {
            should_destroy_self = true;
        }
    }

    if (should_destroy_self)
    {
        gExecutorStorage.deallocate(self_id);
    }
    executorID.store(other_id, std::memory_order_release);
    return *this;
}

HardwareExecutor &HardwareExecutor::operator=(HardwareExecutor &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(executorMutex, other.executorMutex);
    auto const self_id = executorID.load(std::memory_order_acquire);
    auto const other_id = other.executorID.load(std::memory_order_acquire);

    if (self_id > 0)
    {
        bool should_destroy_self = false;
        if (auto const self_handle = gExecutorStorage.acquire_write(self_id);
            decExec(self_id, self_handle))
        {
            should_destroy_self = true;
        }
        if (should_destroy_self)
        {
            gExecutorStorage.deallocate(self_id);
        }
    }
    executorID.store(other_id, std::memory_order_release);
    other.executorID.store(0, std::memory_order_release);
    return *this;
}

HardwareExecutor &HardwareExecutor::operator<<(ComputePipelineBase &computePipeline)
{
    if (auto const executor_handle = gExecutorStorage.acquire_write(executorID.load(std::memory_order_acquire));
        computePipeline.getComputePipelineID())
    {
        if (auto const pipeline_handle = gComputePipelineStorage.acquire_read(computePipeline.getComputePipelineID());
            pipeline_handle.valid())
        {
            *executor_handle->impl << static_cast<CommandRecordVulkan *>(pipeline_handle->impl);
        }
    }
    return *this;
}

HardwareExecutor &HardwareExecutor::operator<<(RasterizerPipelineBase &rasterizerPipeline)
{
    if (auto const executor_handle = gExecutorStorage.acquire_write(executorID.load(std::memory_order_acquire));
        rasterizerPipeline.getRasterizerPipelineID())
    {
        if (auto const raster_handle = gRasterizerPipelineStorage.acquire_read(rasterizerPipeline.getRasterizerPipelineID());
            raster_handle->impl)
        {
            *executor_handle->impl << static_cast<CommandRecordVulkan *>(raster_handle->impl);
        }
    }
    return *this;
}

HardwareExecutor &HardwareExecutor::operator<<(HardwareExecutor &other)
{
    return other;
}

// CopyCommandImpl 前向声明（定义在 HardwareCommands.cpp 中）
// CopyCommandImpl definition moved to HardwareExecutorVulkan.h
// struct CopyCommandImpl
// {
//     virtual ~CopyCommandImpl() = default;
//     virtual CommandRecordVulkan *getCommandRecord() = 0;
// };

HardwareExecutor &HardwareExecutor::operator<<(const CopyCommand &cmd)
{
    if (!cmd.impl)
    {
        return *this;
    }
    auto const self_id = executorID.load(std::memory_order_acquire);
    if (self_id == 0)
    {
        return *this;
    }
    auto executor_handle = gExecutorStorage.acquire_write(self_id);
    if (executor_handle->impl)
    {
        CommandRecordVulkan *record = cmd.impl->getCommandRecord();
        if (record)
        {
            *executor_handle->impl << record;

            // ===== 将资源添加到待释放列表 =====
            executor_handle->impl->pendingResources.push_back(cmd.impl);
        }
    }
    return *this;
}

HardwareExecutor &HardwareExecutor::wait(HardwareExecutor &other)
{
    auto const self_id = executorID.load(std::memory_order_acquire);
    auto const other_id = other.executorID.load(std::memory_order_acquire);
    if (self_id == 0 || other_id == 0)
    {
        return *this;
    }
    if (self_id == other_id)
    {
        return *this;
    }

    if (self_id < other_id)
    {
        auto const self_handle = gExecutorStorage.acquire_write(self_id);
        auto const other_handle = gExecutorStorage.acquire_read(other_id);
        if (other_handle->impl && self_handle->impl)
        {
            self_handle->impl->wait(*other_handle->impl);
        }
    }
    else
    {
        auto const other_handle = gExecutorStorage.acquire_read(other_id);
        auto const self_handle = gExecutorStorage.acquire_write(self_id);
        if (other_handle->impl && self_handle->impl)
        {
            self_handle->impl->wait(*other_handle->impl);
        }
    }
    return *this;
}

HardwareExecutor &HardwareExecutor::commit()
{
    auto handle = gExecutorStorage.acquire_write(executorID.load(std::memory_order_acquire));
    handle->impl->commit();
    return *this;
}

// ========== 延迟释放相关接口实现 ==========

void HardwareExecutor::waitForDeferredResources()
{
    auto const self_id = executorID.load(std::memory_order_acquire);
    if (self_id == 0)
    {
        return;
    }

    auto handle = gExecutorStorage.acquire_write(self_id);
    if (handle->impl)
    {
        handle->impl->waitForAllDeferredResources();
    }
}

void HardwareExecutor::cleanupDeferredResources()
{
    auto const self_id = executorID.load(std::memory_order_acquire);
    if (self_id == 0)
    {
        return;
    }

    auto handle = gExecutorStorage.acquire_write(self_id);
    if (handle->impl)
    {
        handle->impl->cleanupCompletedResources();
    }
}