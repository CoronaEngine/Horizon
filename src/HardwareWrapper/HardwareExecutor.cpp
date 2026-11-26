#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
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

HardwareExecutor& HardwareExecutor::operator<<(HardwareCommand command)
{
    if (!command.impl_) return *this;
    auto& executor = *execFromId(executorID);
    auto* record = command.impl_->getCommandRecord();
    using Draw = HardwareCommand::Impl::Draw;
    auto* draw = dynamic_cast<Draw*>(record);
    if (!draw) {
        executor.record(record, {}, std::move(command.impl_));
        return *this;
    }

    Draw* previous = nullptr;
    if (!draw->startsPass && !executor.commandList.empty())
        previous = dynamic_cast<Draw*>(executor.commandList.back().command);

    const bool continuesPass = previous && previous->sameAttachments(*draw);
    if (continuesPass) {
        if (draw->drawCount > UINT32_MAX - previous->drawCount)
            throw std::length_error("Raster pass exceeds the draw index range");
        if (previous->canMerge(*draw)) {
            previous->groups.push_back(draw->groups.front());
            previous->groups.back().root.drawBase = previous->drawCount;
            previous->drawCount += draw->drawCount;
            return *this;
        }
    }

    // A queued pass may acquire more draws. Keep any saved command independent.
    if (command.impl_.use_count() > 1) {
        auto queued = std::make_shared<Draw>(*draw);
        draw = queued.get();
        command.impl_ = std::move(queued);
    }
    executor.record(draw, {}, std::move(command.impl_));
    if (continuesPass) {
        draw->pass = {.loadColor = true, .loadDepth = true};
        draw->groups.front().root.drawBase = previous->drawCount;
        draw->drawCount += previous->drawCount;
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
