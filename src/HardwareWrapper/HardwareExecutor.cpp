#include "Horizon.h"
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

HardwareExecutor& HardwareExecutor::operator<<(const ComputeCommand& dispatch)
{
    execFromId(executorID)->recordOwned(dispatch.impl_);
    return *this;
}

HardwareExecutor& HardwareExecutor::operator<<(RasterCommand&& draw)
{
    if (!draw.impl_) return *this;
    auto& executor = *execFromId(executorID);
    auto& current = *draw.impl_;
    RasterCommand::Impl* previous = nullptr;
    if (!current.startsPass && !executor.commandList.empty())
        previous = dynamic_cast<RasterCommand::Impl*>(executor.commandList.back().command);

    if (previous && previous->sameAttachments(current)) {
        const uint32_t base = previous->drawCount;
        if (current.drawCount > UINT32_MAX - base) throw std::length_error("Raster pass exceeds the draw index range");
        if (previous->canMerge(current)) {
            previous->groups.push_back(std::move(current.groups.front()));
            previous->groups.back().root.drawBase = base;
            previous->drawCount += current.drawCount;
        } else {
            // A physical split preserves the attachments and the shader-visible draw index.
            executor.recordOwned(draw.impl_);
            current.pass = {.loadColor = true, .loadDepth = true};
            current.groups.front().root.drawBase = base;
            current.drawCount += base;
        }
    } else {
        executor.recordOwned(draw.impl_);
    }
    draw.impl_.reset();
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
