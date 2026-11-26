#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/PipelineVulkan/RasterizerPipeline.h"
#include <stdexcept>

HardwareExecutor::HardwareExecutor()
    : impl_(std::make_unique<Impl>())
{
}

HardwareExecutor::HardwareExecutor(HardwareExecutor&&) noexcept = default;
HardwareExecutor::~HardwareExecutor() = default;
HardwareExecutor& HardwareExecutor::operator=(HardwareExecutor&&) noexcept = default;

HardwareExecutor& HardwareExecutor::operator<<(HardwareCommand command)
{
    if (!command.impl_) return *this;
    auto& executor = HardwareBackendAccess::executor(*this);
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
    if (impl_ && other.impl_ && this != &other)
        impl_->backend.wait(other.impl_->backend);
    return *this;
}

HardwareExecutor& HardwareExecutor::commit()
{
    HardwareBackendAccess::executor(*this).commit();
    return *this;
}

void HardwareExecutor::waitForDeferredResources()
{
    if (impl_)
        impl_->backend.waitForAllDeferredResources();
}

void HardwareExecutor::cleanupDeferredResources()
{
    if (impl_)
        impl_->backend.cleanupCompletedResources();
}
