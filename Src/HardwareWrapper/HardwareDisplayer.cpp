#include "Horizon.h"
#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include "corona/kernel/utils/storage.h"

static void incrementDisplayerRefCount(uint32_t id, const Corona::Kernel::Utils::Storage<DisplayerHardwareWrap>::WriteHandle &handle)
{
    ++handle->refCount;
    // CFW_LOG_TRACE("HardwareDisplayer ref++: id={}, count={}", id, handle->refCount);
}

static bool decrementDisplayerRefCount(uint32_t id, const Corona::Kernel::Utils::Storage<DisplayerHardwareWrap>::WriteHandle &handle)
{
    int count = --handle->refCount;
    // CFW_LOG_TRACE("HardwareDisplayer ref--: id={}, count={}", id, count);
    if (count == 0)
    {
        handle->displayManager.reset();
        handle->displaySurface = nullptr;
        // CFW_LOG_TRACE("HardwareDisplayer destroyed: id={}", id);
        return true;
    }

    return false;
}

HardwareDisplayer::HardwareDisplayer(void *surface)
{
    auto id = globalDisplayerStorages.allocate();
    displaySurfaceID.store(id, std::memory_order_release);
    auto const handle = globalDisplayerStorages.acquire_write(id);
    handle->displaySurface = surface;
    handle->displayManager = std::make_shared<DisplayManager>();
    // CFW_LOG_TRACE("HardwareDisplayer created: id={}", id);
}

HardwareDisplayer::HardwareDisplayer(const HardwareDisplayer &other)
{
    std::lock_guard<std::mutex> lock(other.displayerMutex);
    auto const other_id = other.displaySurfaceID.load(std::memory_order_acquire);
    displaySurfaceID.store(other_id, std::memory_order_release);
    if (other_id > 0)
    {
        auto const handle = globalDisplayerStorages.acquire_write(other_id);
        incrementDisplayerRefCount(other_id, handle);
    }
}

HardwareDisplayer::HardwareDisplayer(HardwareDisplayer &&other) noexcept
{
    std::lock_guard<std::mutex> lock(other.displayerMutex);
    auto const other_id = other.displaySurfaceID.load(std::memory_order_acquire);
    displaySurfaceID.store(other_id, std::memory_order_release);
    other.displaySurfaceID.store(0, std::memory_order_release);
}

HardwareDisplayer::~HardwareDisplayer()
{
    auto const self_id = displaySurfaceID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        bool destroy = false;
        if (auto const handle = globalDisplayerStorages.acquire_write(self_id); decrementDisplayerRefCount(self_id, handle))
        {
            destroy = true;
        }
        if (destroy)
        {
            globalDisplayerStorages.deallocate(self_id);
        }
        displaySurfaceID.store(0, std::memory_order_release);
    }
}

HardwareDisplayer &HardwareDisplayer::operator=(const HardwareDisplayer &other)
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(displayerMutex, other.displayerMutex);
    auto const self_id = displaySurfaceID.load(std::memory_order_acquire);
    auto const other_id = other.displaySurfaceID.load(std::memory_order_acquire);

    if (self_id == 0 && other_id == 0)
    {
        return *this;
    }
    if (self_id == other_id)
    {
        return *this;
    }

    if (other_id == 0)
    {
        bool should_destroy_self = false;
        if (auto const self_handle = globalDisplayerStorages.acquire_write(self_id);
            decrementDisplayerRefCount(self_id, self_handle))
        {
            should_destroy_self = true;
        }
        if (should_destroy_self)
        {
            globalDisplayerStorages.deallocate(self_id);
        }
        displaySurfaceID.store(0, std::memory_order_release);
        return *this;
    }

    if (self_id == 0)
    {
        displaySurfaceID.store(other_id, std::memory_order_release);
        auto const other_handle = globalDisplayerStorages.acquire_write(other_id);
        incrementDisplayerRefCount(other_id, other_handle);
        return *this;
    }

    bool should_destroy_self = false;
    if (self_id < other_id)
    {
        auto const self_handle = globalDisplayerStorages.acquire_write(self_id);
        auto const other_handle = globalDisplayerStorages.acquire_write(other_id);
        incrementDisplayerRefCount(other_id, other_handle);
        if (decrementDisplayerRefCount(self_id, self_handle))
        {
            should_destroy_self = true;
        }
    }
    else
    {
        auto const other_handle = globalDisplayerStorages.acquire_write(other_id);
        auto const self_handle = globalDisplayerStorages.acquire_write(self_id);
        incrementDisplayerRefCount(other_id, other_handle);
        if (decrementDisplayerRefCount(self_id, self_handle))
        {
            should_destroy_self = true;
        }
    }
    if (should_destroy_self)
    {
        globalDisplayerStorages.deallocate(self_id);
    }
    displaySurfaceID.store(other_id, std::memory_order_release);
    return *this;
}

HardwareDisplayer &HardwareDisplayer::operator=(HardwareDisplayer &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(displayerMutex, other.displayerMutex);
    auto const self_id = displaySurfaceID.load(std::memory_order_acquire);
    auto const other_id = other.displaySurfaceID.load(std::memory_order_acquire);

    if (self_id > 0)
    {
        bool should_destroy_self = false;
        if (auto const self_handle = globalDisplayerStorages.acquire_write(self_id);
            decrementDisplayerRefCount(self_id, self_handle))
        {
            should_destroy_self = true;
        }
        if (should_destroy_self)
        {
            globalDisplayerStorages.deallocate(self_id);
        }
    }
    displaySurfaceID.store(other_id, std::memory_order_release);
    other.displaySurfaceID.store(0, std::memory_order_release);
    return *this;
}

HardwareDisplayer &HardwareDisplayer::wait(const HardwareExecutor &executor)
{
    auto const executor_id = executor.getExecutorID();
    auto const self_id = displaySurfaceID.load(std::memory_order_acquire);
    if (executor_id > 0 && self_id > 0)
    {
        if (auto const executor_handle = gExecutorStorage.acquire_read(executor_id); executor_handle->impl)
        {
            if (auto const display_handle = globalDisplayerStorages.acquire_write(self_id); display_handle->displayManager)
            {
                display_handle->displayManager->waitExecutor(*executor_handle->impl);
            }
        }
    }
    return *this;
}

HardwareDisplayer &HardwareDisplayer::operator<<(const HardwareImage &image)
{
    auto const self_id = displaySurfaceID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        if (auto const handle = globalDisplayerStorages.acquire_read(self_id); handle->displayManager && handle->displaySurface)
        {
            handle->displayManager->displayFrame(handle->displaySurface, image);
        }
    }
    return *this;
}