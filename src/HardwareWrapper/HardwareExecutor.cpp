#include "Horizon_old.h"
#include "HardwareCommands_old.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/PipelineVulkan/ComputePipeline.h"
#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"
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

HardwareExecutor& HardwareExecutor::operator<<(ComputePipelineBase& computePipeline)
{
    auto* executor = execFromId(executorID);
    if (auto* pipeline = reinterpret_cast<ComputePipelineVulkan*>(computePipeline.getComputePipelineID()))
        *executor << static_cast<CommandRecordVulkan*>(pipeline);
    return *this;
}

HardwareExecutor& HardwareExecutor::operator<<(RasterizerPipelineBase& rasterizerPipeline)
{
    auto* executor = execFromId(executorID);
    if (auto* pipeline = reinterpret_cast<RasterizerPipelineVulkan*>(rasterizerPipeline.getRasterizerPipelineID()))
        *executor << static_cast<CommandRecordVulkan*>(pipeline);
    return *this;
}

HardwareExecutor& HardwareExecutor::operator<<(HardwareExecutor& other)
{
    return other;
}

HardwareExecutor& HardwareExecutor::operator<<(const CopyCommand& cmd)
{
    if (!cmd.impl || !executorID)
        return *this;

    auto* executor = execFromId(executorID);
    if (CommandRecordVulkan* record = cmd.impl->getCommandRecord())
    {
        *executor << record;
        executor->pendingResources.push_back(cmd.impl);
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
