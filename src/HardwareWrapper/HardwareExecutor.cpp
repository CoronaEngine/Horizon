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
    auto& pending = HardwareBackendAccess::state(*this).pending;
    auto entry = command.impl_->queueEntry(std::move(command.impl_));
    auto* gpu = std::get_if<RecordedCommand>(&entry);
    if (gpu && gpu->type == CommandRecordVulkan::ExecutorType::Invalid)
        return *this;

    using Draw = Command::Impl::Draw;
    auto* draw = gpu ? dynamic_cast<Draw*>(gpu->command) : nullptr;
    if (!draw) {
        pending.push_back(std::move(entry));
        return *this;
    }

    Draw* previous = nullptr;
    if (!draw->startsPass && !pending.empty()) {
        if (const auto* last = std::get_if<RecordedCommand>(&pending.back()))
            previous = dynamic_cast<Draw*>(last->command);
    }

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
    if (gpu->owner.use_count() > 1) {
        auto queued = std::make_shared<Draw>(*draw);
        draw = queued.get();
        gpu->command = draw;
        gpu->owner = std::move(queued);
    }
    pending.push_back(std::move(entry));
    if (continuesPass) {
        draw->pass = {.loadColor = true, .loadDepth = static_cast<bool>(draw->depthImage)};
        draw->groups.front().root.drawBase = previous->drawCount;
        draw->drawCount += previous->drawCount;
    }
    return *this;
}

HardwareExecutor& HardwareExecutor::commit()
{
    auto& state = HardwareBackendAccess::state(*this);
    auto& pending = state.pending;
    auto& executor = state.backend;
    while (!pending.empty()) {
        size_t count = 0;
        while (count < pending.size() && std::holds_alternative<RecordedCommand>(pending[count]))
            ++count;
        const bool hasDisplay = count < pending.size();
        auto& batch = executor.commandList;
        batch.reserve(count);
        for (size_t i = 0; i < count; ++i)
            batch.push_back(std::move(std::get<RecordedCommand>(pending[i])));

        try {
            if (hasDisplay)
                std::get<std::shared_ptr<const Command::Impl::Display>>(pending[count])->submit(executor);
            executor.commit(hasDisplay); // A skipped display still submits its work and dependencies.
        } catch (...) {
            // Successful submissions have already taken ownership of their records.
            // Restore only unsent work and leave the display and its suffix queued for retry.
            const size_t submitted = count - batch.size();
            for (size_t i = 0; i < batch.size(); ++i)
                std::get<RecordedCommand>(pending[submitted + i]) = std::move(batch[i]);
            batch.clear();
            pending.erase(pending.begin(), pending.begin() + submitted);
            throw;
        }
        pending.erase(pending.begin(), pending.begin() + count + hasDisplay);
    }
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
