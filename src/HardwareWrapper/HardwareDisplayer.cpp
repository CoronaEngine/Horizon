#include "Horizon_old.h"
#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"

HardwareDisplayer::HardwareDisplayer(void* surface)
    : displaySurfaceID(reinterpret_cast<uintptr_t>(new DisplayManager())), displaySurface(surface)
{
}

HardwareDisplayer::HardwareDisplayer(HardwareDisplayer&& other) noexcept
    : displaySurfaceID(std::exchange(other.displaySurfaceID, 0)),
      displaySurface(std::exchange(other.displaySurface, nullptr))
{
}

HardwareDisplayer::~HardwareDisplayer()
{
    delete reinterpret_cast<DisplayManager*>(displaySurfaceID);
}

HardwareDisplayer& HardwareDisplayer::operator=(HardwareDisplayer&& other) noexcept
{
    if (this == &other)
        return *this;

    delete reinterpret_cast<DisplayManager*>(displaySurfaceID);
    displaySurfaceID = std::exchange(other.displaySurfaceID, 0);
    displaySurface = std::exchange(other.displaySurface, nullptr);
    return *this;
}

HardwareDisplayer &HardwareDisplayer::wait(const HardwareExecutor &executor)
{
    const auto executorId = executor.getExecutorID();
    if (executorId && displaySurfaceID)
    {
        auto* display = reinterpret_cast<DisplayManager*>(displaySurfaceID);
        display->waitExecutor(*reinterpret_cast<HardwareExecutorVulkan*>(executorId));
    }
    return *this;
}

HardwareDisplayer &HardwareDisplayer::operator<<(const HardwareImage &image)
{
    if (displaySurfaceID && displaySurface)
        reinterpret_cast<DisplayManager*>(displaySurfaceID)->displayFrame(displaySurface, image);
    return *this;
}
