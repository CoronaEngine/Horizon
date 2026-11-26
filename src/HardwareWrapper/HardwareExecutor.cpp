#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"
#include <stdexcept>

namespace {
HardwareExecutorVulkan* execFromId(uintptr_t id)
{
    if (!id)
        throw std::runtime_error("Empty HardwareExecutor handle");
    return reinterpret_cast<HardwareExecutorVulkan*>(id);
}
} // namespace

HardwareExecutor::HardwareExecutor()
    : executorID(reinterpret_cast<uintptr_t>(new HardwareExecutorVulkan()))
{
}

HardwareExecutor::HardwareExecutor(HardwareExecutor&& other) noexcept
    : executorID(std::exchange(other.executorID, 0))
{
}

HardwareExecutor::~HardwareExecutor()
{
    delete reinterpret_cast<HardwareExecutorVulkan*>(executorID);
}

HardwareExecutor& HardwareExecutor::operator=(HardwareExecutor&& other) noexcept
{
    if (this == &other)
        return *this;

    delete reinterpret_cast<HardwareExecutorVulkan*>(executorID);
    executorID = std::exchange(other.executorID, 0);
    return *this;
}

HardwareExecutor& HardwareExecutor::enqueue(const ComputePipeline<>& pipeline)
{
    auto* executor = execFromId(executorID);
    if (const auto& impl = pipeline.impl_; impl && impl->backend)
        executor->recordOwned(impl->backend->capture(), impl->resourceUses);
    return *this;
}

HardwareExecutor& HardwareExecutor::operator<<(RasterBatch&& batch)
{
    if (!batch.record_) throw std::logic_error("Raster batch already submitted");
    execFromId(executorID)->recordOwned(batch.record_, batch.uses_);
    batch.record_.reset();
    return *this;
}

HardwareExecutor& HardwareExecutor::operator<<(const CopyCommand& cmd)
{
    if (!cmd.impl_ || !executorID)
        return *this;

    auto* executor = execFromId(executorID);
    if (CommandRecordVulkan* record = cmd.impl_->getCommandRecord())
    {
        executor->record(record, {}, cmd.impl_);
    }
    return *this;
}

HardwareExecutor& HardwareExecutor::wait(HardwareExecutor& other)
{
    if (executorID && other.executorID && executorID != other.executorID)
        execFromId(executorID)->wait(*execFromId(other.executorID));
    return *this;
}

HardwareExecutor& HardwareExecutor::commit()
{
    execFromId(executorID)->commit();
    return *this;
}

void HardwareExecutor::waitForDeferredResources()
{
    if (executorID)
        execFromId(executorID)->waitForAllDeferredResources();
}

void HardwareExecutor::cleanupDeferredResources()
{
    if (executorID)
        execFromId(executorID)->cleanupCompletedResources();
}
