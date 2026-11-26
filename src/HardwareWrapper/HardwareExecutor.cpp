#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/PipelineVulkan/RasterPipeline.h"
#include <stdexcept>

HardwareExecutor::HardwareExecutor()
    : impl_(std::make_unique<Impl>())
{
}

HardwareExecutor::HardwareExecutor(HardwareExecutor&&) noexcept = default;
HardwareExecutor::~HardwareExecutor() = default;
HardwareExecutor& HardwareExecutor::operator=(HardwareExecutor&&) noexcept = default;

HardwareExecutor& HardwareExecutor::operator<<(Command command)
{
    if (!command.impl_) return *this;
    auto& executor = HardwareBackendAccess::executor(*this);
    auto* record = command.impl_->getCommandRecord();
    if (!record) {
        if (auto display = std::dynamic_pointer_cast<Command::Impl::Display>(std::move(command.impl_))) {
            auto& pending = impl_->displays.emplace_back(std::move(display));
            pending.preceding.swap(executor.commandList);
        }
        return *this;
    }
    using Draw = Command::Impl::Draw;
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

HardwareExecutor& HardwareExecutor::commit()
{
    auto& executor = HardwareBackendAccess::executor(*this);
    while (!impl_->displays.empty()) {
        auto& pending = impl_->displays.front();
        pending.preceding.swap(executor.commandList);
        try {
            pending.display->submit(executor);
            executor.commit(true); // A skipped display still submits its work and dependencies.
        } catch (...) {
            pending.preceding.swap(executor.commandList);
            throw;
        }
        pending.preceding.swap(executor.commandList);
        impl_->displays.pop_front();
    }
    executor.commit();
    return *this;
}

HardwareExecutor& HardwareExecutor::wait(HardwareExecutor* other)
{
    if (!impl_) return *this;
    if (!other || other == this)
        impl_->backend.waitForAllDeferredResources();
    else if (other->impl_)
        impl_->backend.wait(other->impl_->backend);
    return *this;
}
