#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareContext.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include "corona/kernel/utils/storage.h"

static void incrementPushConstantRefCount(uint32_t id, const Corona::Kernel::Utils::Storage<PushConstantWrap>::WriteHandle &handle)
{
    ++handle->refCount;
    // CFW_LOG_TRACE("HardwarePushConstant ref++: id={}, count={}", id, handle->refCount);
}

static bool decrementPushConstantRefCount(uint32_t id, const Corona::Kernel::Utils::Storage<PushConstantWrap>::WriteHandle &handle)
{
    int count = --handle->refCount;
    // CFW_LOG_TRACE("HardwarePushConstant ref--: id={}, count={}", id, count);
    if (count == 0)
    {
        if (handle->data != nullptr && !handle->isSub)
        {
            std::free(handle->data);
            handle->data = nullptr;
        }
        // CFW_LOG_TRACE("HardwarePushConstant destroyed: id={}", id);
        return true;
    }
    return false;
}

HardwarePushConstant::HardwarePushConstant()
    : pushConstantID(globalPushConstantStorages.allocate())
{
    // CFW_LOG_TRACE("HardwarePushConstant created: id={}", pushConstantID.load(std::memory_order_acquire));
}

HardwarePushConstant::HardwarePushConstant(uint64_t size, uint64_t offset, HardwarePushConstant *whole)
{
    auto const self_id = globalPushConstantStorages.allocate();
    pushConstantID.store(self_id, std::memory_order_release);
    auto pushConstantHandle = globalPushConstantStorages.acquire_write(self_id);
    pushConstantHandle->size = size;
    pushConstantHandle->isSub = false;

    if (whole != nullptr)
    {
        auto wholeHandle = globalPushConstantStorages.acquire_read(whole->getPushConstantID());
        if (wholeHandle->data != nullptr)
        {
            pushConstantHandle->data = wholeHandle->data + offset;
            pushConstantHandle->isSub = true;
        }
    }

    if (!pushConstantHandle->isSub)
    {
        pushConstantHandle->data = static_cast<uint8_t *>(std::malloc(size));
    }
    // CFW_LOG_TRACE("HardwarePushConstant created: id={}", self_id);
}

HardwarePushConstant::HardwarePushConstant(const HardwarePushConstant &other)
{
    std::lock_guard<std::mutex> lock(other.pushConstantMutex);
    auto const other_id = other.pushConstantID.load(std::memory_order_acquire);
    pushConstantID.store(other_id, std::memory_order_release);
    if (other_id > 0)
    {
        auto const handle = globalPushConstantStorages.acquire_write(other_id);
        incrementPushConstantRefCount(other_id, handle);
    }
}

HardwarePushConstant::HardwarePushConstant(HardwarePushConstant &&other) noexcept
{
    std::lock_guard<std::mutex> lock(other.pushConstantMutex);
    auto const other_id = other.pushConstantID.load(std::memory_order_acquire);
    pushConstantID.store(other_id, std::memory_order_release);
    other.pushConstantID.store(0, std::memory_order_release);
}

HardwarePushConstant::~HardwarePushConstant()
{
    auto const self_id = pushConstantID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        bool destroy = false;
        if (auto const handle = globalPushConstantStorages.acquire_write(self_id); decrementPushConstantRefCount(self_id, handle))
        {
            destroy = true;
        }
        if (destroy)
        {
            globalPushConstantStorages.deallocate(self_id);
        }
        pushConstantID.store(0, std::memory_order_release);
    }
}

HardwarePushConstant &HardwarePushConstant::operator=(const HardwarePushConstant &other)
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(pushConstantMutex, other.pushConstantMutex);
    auto const self_id = pushConstantID.load(std::memory_order_acquire);
    auto const other_id = other.pushConstantID.load(std::memory_order_acquire);

    bool isSub = false;
    if (self_id > 0 && other_id > 0)
    {
        auto thisPc = globalPushConstantStorages.acquire_write(self_id);
        auto otherPc = globalPushConstantStorages.acquire_read(other_id);

        if (thisPc->isSub)
        {
            if (thisPc->data != nullptr && otherPc->data != nullptr)
            {
                std::memcpy(thisPc->data, otherPc->data, std::min(thisPc->size, otherPc->size));
            }
            isSub = true;
        }
    }

    if (!isSub)
    {
        if (self_id == other_id)
        {
            return *this;
        }

        if (other_id == 0)
        {
            bool should_destroy_self = false;
            if (auto const self_handle = globalPushConstantStorages.acquire_write(self_id);
                decrementPushConstantRefCount(self_id, self_handle))
            {
                should_destroy_self = true;
            }
            if (should_destroy_self)
            {
                globalPushConstantStorages.deallocate(self_id);
            }
            pushConstantID.store(0, std::memory_order_release);
            return *this;
        }

        if (self_id == 0)
        {
            pushConstantID.store(other_id, std::memory_order_release);
            auto const other_handle = globalPushConstantStorages.acquire_write(other_id);
            incrementPushConstantRefCount(other_id, other_handle);
            return *this;
        }

        bool should_destroy_self = false;
        if (self_id < other_id)
        {
            auto const self_handle = globalPushConstantStorages.acquire_write(self_id);
            auto const other_handle = globalPushConstantStorages.acquire_write(other_id);
            incrementPushConstantRefCount(other_id, other_handle);
            if (decrementPushConstantRefCount(self_id, self_handle))
            {
                should_destroy_self = true;
            }
        }
        else
        {
            auto const other_handle = globalPushConstantStorages.acquire_write(other_id);
            auto const self_handle = globalPushConstantStorages.acquire_write(self_id);
            incrementPushConstantRefCount(other_id, other_handle);
            if (decrementPushConstantRefCount(self_id, self_handle))
            {
                should_destroy_self = true;
            }
        }
        if (should_destroy_self)
        {
            globalPushConstantStorages.deallocate(self_id);
        }
        pushConstantID.store(other_id, std::memory_order_release);
    }
    return *this;
}

HardwarePushConstant &HardwarePushConstant::operator=(HardwarePushConstant &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    std::scoped_lock lock(pushConstantMutex, other.pushConstantMutex);
    auto const self_id = pushConstantID.load(std::memory_order_acquire);
    auto const other_id = other.pushConstantID.load(std::memory_order_acquire);

    if (self_id > 0)
    {
        bool should_destroy_self = false;
        if (auto const self_handle = globalPushConstantStorages.acquire_write(self_id);
            decrementPushConstantRefCount(self_id, self_handle))
        {
            should_destroy_self = true;
        }
        if (should_destroy_self)
        {
            globalPushConstantStorages.deallocate(self_id);
        }
    }
    pushConstantID.store(other_id, std::memory_order_release);
    other.pushConstantID.store(0, std::memory_order_release);
    return *this;
}

uint8_t *HardwarePushConstant::getData() const
{
    auto const self_id = pushConstantID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        return globalPushConstantStorages.acquire_write(self_id)->data;
    }
    return nullptr;
}

uint64_t HardwarePushConstant::getSize() const
{
    auto const self_id = pushConstantID.load(std::memory_order_acquire);
    if (self_id > 0)
    {
        return globalPushConstantStorages.acquire_read(self_id)->size;
    }
    return 0;
}

void HardwarePushConstant::copyFromRaw(const void *src, uint64_t size)
{
    if (src != nullptr || size != 0)
    {
        auto const self_id = pushConstantID.load(std::memory_order_acquire);
        if (self_id > 0)
        {
            bool destroy = false;
            if (auto const handle = globalPushConstantStorages.acquire_write(self_id); decrementPushConstantRefCount(self_id, handle))
            {
                destroy = true;
            }
            if (destroy)
            {
                globalPushConstantStorages.deallocate(self_id);
            }
        }

        auto const new_id = globalPushConstantStorages.allocate();
        pushConstantID.store(new_id, std::memory_order_release);

        auto handle = globalPushConstantStorages.acquire_write(new_id);

        handle->size = size;
        handle->isSub = false;
        handle->data = static_cast<uint8_t *>(std::malloc(size));

        if (handle->data != nullptr)
        {
            std::memcpy(handle->data, src, size);
        }
    }
}