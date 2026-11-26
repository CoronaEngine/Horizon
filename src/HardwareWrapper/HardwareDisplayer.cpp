#include "Horizon.h"
#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include <mutex>

struct HardwareDisplayer::Impl
{
    void* surface;
    DisplayManager manager{};
    std::mutex mutex;
};

HardwareDisplayer::HardwareDisplayer(void* surface)
    : impl_(std::make_shared<Impl>(surface))
{
}

HardwareDisplayer::HardwareDisplayer(HardwareDisplayer&&) noexcept = default;
HardwareDisplayer::~HardwareDisplayer() = default;
HardwareDisplayer& HardwareDisplayer::operator=(HardwareDisplayer&&) noexcept = default;

HardwareExecutor::Command HardwareDisplayer::operator()(const HardwareImage& image) const
{
    return HardwareExecutor::Command(std::make_shared<HardwareExecutor::Command::Impl::Display>(impl_, image));
}

void HardwareExecutor::Command::Impl::Display::submit(HardwareExecutorVulkan& executor) const
{
    if (!target || !target->surface || !image) return;
    std::lock_guard lock(target->mutex);
    target->manager.displayFrame(target->surface, image, executor);
}
