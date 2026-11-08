#include "Horizon_old.h"
#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include <stdexcept>

namespace {
DisplayerHardwareWrap* displayFromId(uintptr_t id)
{
    if (!id)
        throw std::runtime_error("Empty DisplayerHardwareWrap handle");
    return reinterpret_cast<DisplayerHardwareWrap*>(id);
}

void retainDisplay(uintptr_t id)
{
    if (id)
        ++displayFromId(id)->refCount;
}

void releaseDisplay(uintptr_t id)
{
    if (!id)
        return;
    auto* handle = displayFromId(id);
    if (--handle->refCount == 0)
    {
        handle->displayManager.reset();
        delete handle;
    }
}
} // namespace

HardwareDisplayer::HardwareDisplayer(void* surface)
{
    auto handle = std::make_unique<DisplayerHardwareWrap>();
    handle->displaySurface = surface;
    handle->displayManager = std::make_shared<DisplayManager>();
    displaySurfaceID = reinterpret_cast<uintptr_t>(handle.release());
}

HardwareDisplayer::HardwareDisplayer(const HardwareDisplayer& other)
    : displaySurfaceID(other.displaySurfaceID)
{
    retainDisplay(displaySurfaceID);
}

HardwareDisplayer::HardwareDisplayer(HardwareDisplayer&& other) noexcept
    : displaySurfaceID(std::exchange(other.displaySurfaceID, 0))
{
}

HardwareDisplayer::~HardwareDisplayer()
{
    releaseDisplay(displaySurfaceID);
}

HardwareDisplayer& HardwareDisplayer::operator=(const HardwareDisplayer& other)
{
    if (displaySurfaceID == other.displaySurfaceID)
        return *this;

    retainDisplay(other.displaySurfaceID);
    releaseDisplay(displaySurfaceID);
    displaySurfaceID = other.displaySurfaceID;
    return *this;
}

HardwareDisplayer& HardwareDisplayer::operator=(HardwareDisplayer&& other) noexcept
{
    if (this == &other)
        return *this;

    releaseDisplay(displaySurfaceID);
    displaySurfaceID = std::exchange(other.displaySurfaceID, 0);
    return *this;
}

HardwareDisplayer &HardwareDisplayer::wait(const HardwareExecutor &executor)
{
    const auto executorId = executor.getExecutorID();
    if (executorId && displaySurfaceID)
    {
        auto* display = displayFromId(displaySurfaceID);
        if (display->displayManager)
            display->displayManager->waitExecutor(*reinterpret_cast<HardwareExecutorVulkan*>(executorId));
    }
    return *this;
}

HardwareDisplayer &HardwareDisplayer::operator<<(const HardwareImage &image)
{
    auto const self_id = displaySurfaceID;
    if (self_id > 0)
    {
        if (auto const handle = displayFromId(self_id); handle->displayManager && handle->displaySurface)
        {
            handle->displayManager->displayFrame(handle->displaySurface, image);
        }
    }
    return *this;
}
