#include "Horizon_old.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include "HardwareWrapperVulkan/ResourcePool.h"

namespace {
PushConstantWrap* pushConstantFromId(uintptr_t id) noexcept
{
    return reinterpret_cast<PushConstantWrap*>(id);
}

void retainPushConstant(uintptr_t id)
{
    if (auto* handle = pushConstantFromId(id))
        ++handle->refCount;
}

void releasePushConstant(uintptr_t id)
{
    auto* handle = pushConstantFromId(id);
    if (handle && --handle->refCount == 0)
    {
        if (!handle->isSub)
            std::free(handle->data);
        delete handle;
    }
}
} // namespace

HardwarePushConstant::HardwarePushConstant()
    : pushConstantID(reinterpret_cast<uintptr_t>(new PushConstantWrap{}))
{
}

HardwarePushConstant::HardwarePushConstant(uint64_t size, uint64_t offset, HardwarePushConstant* whole)
{
    auto handle = std::make_unique<PushConstantWrap>();
    handle->size = size;

    if (whole)
    {
        auto* parent = pushConstantFromId(whole->getPushConstantID());
        if (!parent)
            throw std::runtime_error("Empty PushConstantWrap handle");
        if (parent->data)
        {
            handle->data = parent->data + offset;
            handle->isSub = true;
        }
    }

    if (!handle->isSub)
        handle->data = static_cast<uint8_t*>(std::malloc(size));

    pushConstantID = reinterpret_cast<uintptr_t>(handle.release());
}

HardwarePushConstant::HardwarePushConstant(const HardwarePushConstant& other)
    : pushConstantID(other.pushConstantID)
{
    retainPushConstant(pushConstantID);
}

HardwarePushConstant::HardwarePushConstant(HardwarePushConstant&& other) noexcept
    : pushConstantID(std::exchange(other.pushConstantID, 0))
{
}

HardwarePushConstant::~HardwarePushConstant()
{
    releasePushConstant(pushConstantID);
}

HardwarePushConstant& HardwarePushConstant::operator=(const HardwarePushConstant& other)
{
    if (pushConstantID == other.pushConstantID)
        return *this;

    auto* self = pushConstantFromId(pushConstantID);
    auto* source = pushConstantFromId(other.pushConstantID);
    // Assignment into a subregion writes its parent's bytes; ordinary copies
    // continue to share storage. The parent must outlive its subregions.
    if (self && source && self->isSub)
    {
        if (self->data && source->data)
            std::memcpy(self->data, source->data, std::min(self->size, source->size));
        return *this;
    }

    retainPushConstant(other.pushConstantID);
    releasePushConstant(pushConstantID);
    pushConstantID = other.pushConstantID;
    return *this;
}

HardwarePushConstant& HardwarePushConstant::operator=(HardwarePushConstant&& other) noexcept
{
    if (this == &other)
        return *this;

    releasePushConstant(pushConstantID);
    pushConstantID = std::exchange(other.pushConstantID, 0);
    return *this;
}

uint8_t* HardwarePushConstant::getData() const
{
    auto* handle = pushConstantFromId(pushConstantID);
    return handle ? handle->data : nullptr;
}

uint64_t HardwarePushConstant::getSize() const
{
    auto* handle = pushConstantFromId(pushConstantID);
    return handle ? handle->size : 0;
}

void HardwarePushConstant::copyFromRaw(const void* src, uint64_t size)
{
    if (!src || !size)
        return;

    auto handle = std::make_unique<PushConstantWrap>();
    handle->size = size;
    handle->data = static_cast<uint8_t*>(std::malloc(size));
    if (handle->data)
        std::memcpy(handle->data, src, size);

    releasePushConstant(pushConstantID);
    pushConstantID = reinterpret_cast<uintptr_t>(handle.release());
}
