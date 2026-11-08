#include "Horizon_old.h"
#include "HardwareCommands_old.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"
#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include <stdexcept>

namespace {
ExecutorWrap* execFromId(uintptr_t id)
{
    if (!id)
        throw std::runtime_error("Empty ExecutorWrap handle");
    return reinterpret_cast<ExecutorWrap*>(id);
}

void retainExec(uintptr_t id)
{
    if (id)
        ++execFromId(id)->refCount;
}

void releaseExec(uintptr_t id)
{
    if (!id)
        return;
    auto* handle = execFromId(id);
    if (--handle->refCount == 0)
    {
        delete handle->impl;
        delete handle;
    }
}
} // namespace

HardwareExecutor::HardwareExecutor()
{
    auto handle = std::make_unique<ExecutorWrap>();
    handle->impl = new HardwareExecutorVulkan();
    executorID = reinterpret_cast<uintptr_t>(handle.release());
}

HardwareExecutor::HardwareExecutor(const HardwareExecutor& other)
    : executorID(other.executorID)
{
    retainExec(executorID);
}

HardwareExecutor::HardwareExecutor(HardwareExecutor&& other) noexcept
    : executorID(std::exchange(other.executorID, 0))
{
}

HardwareExecutor::~HardwareExecutor()
{
    releaseExec(executorID);
}

HardwareExecutor& HardwareExecutor::operator=(const HardwareExecutor& other)
{
    if (executorID == other.executorID)
        return *this;

    retainExec(other.executorID);
    releaseExec(executorID);
    executorID = other.executorID;
    return *this;
}

HardwareExecutor& HardwareExecutor::operator=(HardwareExecutor&& other) noexcept
{
    if (this == &other)
        return *this;

    releaseExec(executorID);
    executorID = std::exchange(other.executorID, 0);
    return *this;
}

HardwareExecutor &HardwareExecutor::operator<<(ComputePipelineBase &computePipeline)
{
    if (auto const executor_handle = execFromId(executorID);
        computePipeline.getComputePipelineID())
    {
        if (auto const pipeline_handle = reinterpret_cast<ComputePipelineWrap*>(computePipeline.getComputePipelineID());
            pipeline_handle != nullptr)
        {
            *executor_handle->impl << static_cast<CommandRecordVulkan *>(pipeline_handle->impl);
        }
    }
    return *this;
}

HardwareExecutor &HardwareExecutor::operator<<(RasterizerPipelineBase &rasterizerPipeline)
{
    if (auto const executor_handle = execFromId(executorID);
        rasterizerPipeline.getRasterizerPipelineID())
    {
        if (auto const raster_handle = reinterpret_cast<RasterizerPipelineWrap*>(rasterizerPipeline.getRasterizerPipelineID());
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

HardwareExecutor &HardwareExecutor::operator<<(const CopyCommand &cmd)
{
    if (!cmd.impl)
    {
        return *this;
    }
    auto const self_id = executorID;
    if (self_id == 0)
    {
        return *this;
    }
    auto executor_handle = execFromId(self_id);
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
    auto const self_id = executorID;
    auto const other_id = other.executorID;
    if (self_id == 0 || other_id == 0)
    {
        return *this;
    }
    if (self_id == other_id)
    {
        return *this;
    }

    auto* self = execFromId(self_id);
    auto* dependency = execFromId(other_id);
    if (self->impl && dependency->impl)
        self->impl->wait(*dependency->impl);
    return *this;
}

HardwareExecutor &HardwareExecutor::commit()
{
    auto handle = execFromId(executorID);
    handle->impl->commit();
    return *this;
}

// ========== 延迟释放相关接口实现 ==========

void HardwareExecutor::waitForDeferredResources()
{
    auto const self_id = executorID;
    if (self_id == 0)
    {
        return;
    }

    auto handle = execFromId(self_id);
    if (handle->impl)
    {
        handle->impl->waitForAllDeferredResources();
    }
}

void HardwareExecutor::cleanupDeferredResources()
{
    auto const self_id = executorID;
    if (self_id == 0)
    {
        return;
    }

    auto handle = execFromId(self_id);
    if (handle->impl)
    {
        handle->impl->cleanupCompletedResources();
    }
}
