#include "Horizon_old.h"
#include "HardwareCommands_old.h"
#include "HardwareWrapperVulkan/HardwareContext.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"

VkBufferUsageFlags convertBufferUsage(BufferUsage const usage)
{
    VkBufferUsageFlags vkUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    switch (usage)
    {
    case BufferUsage::VertexBuffer:
    case BufferUsage::IndexBuffer:
        vkUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        break;
    case BufferUsage::RasterIndirect:
        vkUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    case BufferUsage::UniformBuffer:
        vkUsage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        break;
    case BufferUsage::StorageBuffer:
        vkUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    default:
        break;
    }

    return vkUsage;
}

namespace {
void retainBuffer(uintptr_t id)
{
    if (id) ++legacyBufferFromId(id)->refCount;
}

void releaseBuffer(uintptr_t id)
{
    if (!id) return;
    auto* resource = legacyBufferFromId(id);
    if (--resource->refCount == 0)
    {
        resource->resourceManager->destroyBuffer(*resource);
        delete resource;
    }
}
}

HardwareBuffer::HardwareBuffer() = default;

HardwareBuffer HardwareBuffer::uploadDeviceLocal(size_t count, size_t stride, BufferUsage usage, const void* data)
{
    if (count > UINT32_MAX || stride > UINT32_MAX)
        throw std::length_error("Device-local buffer dimensions exceed the supported range");
    const auto elementCount = static_cast<uint32_t>(count);
    const auto elementSize = static_cast<uint32_t>(stride);
    HardwareBuffer destination;
    auto resource = std::make_unique<ResourceManager::BufferHardwareWrap>();
    *resource = getHardwareContext().getMainDevice()->resourceManager.createBuffer(
        elementCount, elementSize, convertBufferUsage(usage), false, true);
    destination.bufferID = reinterpret_cast<uintptr_t>(resource.release());
    if (elementCount) {
        HardwareBuffer staging(elementCount, elementSize, BufferUsage::StorageBuffer, data);
        HardwareExecutor upload;
        upload << staging.copyTo(destination);
        upload.commit().waitForDeferredResources();
    }
    return destination;
}

HardwareBuffer::HardwareBuffer(uint32_t bufferSize, uint32_t elementSize, BufferUsage usage, const void* data, bool useDedicated)
{
    auto resource = std::make_unique<ResourceManager::BufferHardwareWrap>();
    *resource = getHardwareContext().getMainDevice()->resourceManager.createBuffer(
        bufferSize, elementSize, convertBufferUsage(usage), true, useDedicated);
    if (data && resource->bufferAllocInfo.pMappedData)
        resource->resourceManager->writeMapped(*resource, data, static_cast<size_t>(bufferSize) * elementSize);
    bufferID = reinterpret_cast<uintptr_t>(resource.release());
}

HardwareBuffer::HardwareBuffer(const ExternalHandle& memHandle, uint32_t bufferSize, uint32_t elementSize, uint32_t allocSize, BufferUsage usage)
{
    ResourceManager::ExternalMemoryHandle memoryHandle;
#if _WIN32 || _WIN64
    memoryHandle.handle = memHandle.handle;
#else
    memoryHandle.fd = memHandle.fd;
#endif
    auto resource = std::make_unique<ResourceManager::BufferHardwareWrap>();
    *resource = getHardwareContext().getMainDevice()->resourceManager.importBufferMemory(
        memoryHandle, bufferSize, elementSize, allocSize, convertBufferUsage(usage));
    bufferID = reinterpret_cast<uintptr_t>(resource.release());
}

HardwareBuffer::HardwareBuffer(const HardwareBuffer& other) : bufferID(other.bufferID)
{
    retainBuffer(bufferID);
}

HardwareBuffer::HardwareBuffer(HardwareBuffer&& other) noexcept : bufferID(std::exchange(other.bufferID, 0)) {}

HardwareBuffer::~HardwareBuffer()
{
    releaseBuffer(bufferID);
}

HardwareBuffer& HardwareBuffer::operator=(const HardwareBuffer& other)
{
    if (bufferID == other.bufferID) return *this;
    retainBuffer(other.bufferID);
    releaseBuffer(bufferID);
    bufferID = other.bufferID;
    return *this;
}

HardwareBuffer& HardwareBuffer::operator=(HardwareBuffer&& other) noexcept
{
    if (this == &other) return *this;
    releaseBuffer(bufferID);
    bufferID = std::exchange(other.bufferID, 0);
    return *this;
}

HardwareBuffer::operator bool() const
{
    return bufferID && legacyBufferFromId(bufferID)->bufferHandle != VK_NULL_HANDLE;
}

BufferCopyCommand HardwareBuffer::copyTo(const HardwareBuffer &dst,
                                         uint64_t srcOffset,
                                         uint64_t dstOffset,
                                         uint64_t size) const
{
    return BufferCopyCommand(*this, dst, srcOffset, dstOffset, size);
}

BufferToImageCommand HardwareBuffer::copyTo(const HardwareImage &dst,
                                            uint64_t bufferOffset,
                                            uint32_t imageLayer,
                                            uint32_t imageMip) const
{
    return BufferToImageCommand(*this, dst, bufferOffset, imageLayer, imageMip);
}

uint32_t HardwareBuffer::storeDescriptor() const
{
    auto bufferHandle = legacyBufferFromId(bufferID);
    return bufferHandle->resourceManager->storeDescriptor(*bufferHandle);
}

bool HardwareBuffer::copyFromData(const void *inputData, const uint64_t size) const
{
    if (inputData == nullptr || size == 0)
    {
        return false;
    }
    auto const self_buffer_id = bufferID;
    if (self_buffer_id == 0)
    {
        CFW_LOG_WARNING("Cannot copy data to an uninitialized HardwareBuffer.");
        return false;
    }
    if (const auto handle = legacyBufferFromId(self_buffer_id);
        handle->bufferAllocInfo.pMappedData != nullptr)
    {
        if (size > uint64_t(handle->elementCount) * handle->elementSize) return false;
        handle->resourceManager->writeMapped(*handle, inputData, size);
        return true;
    }
    return false;
}

bool HardwareBuffer::copyToData(void *outputData, const uint64_t size) const
{
    if (outputData == nullptr || size == 0)
    {
        return false;
    }
    auto const self_buffer_id = bufferID;
    if (self_buffer_id == 0)
    {
        CFW_LOG_WARNING("Cannot copy uninitialized HardwareBuffer to out data.");
        return false;
    }
    if (const auto handle = legacyBufferFromId(self_buffer_id);
        handle->bufferAllocInfo.pMappedData != nullptr)
    {
        handle->resourceManager->copyBufferToHost(*handle, outputData, size);
        return true;
    }
    return false;
}

void *HardwareBuffer::getMappedData() const
{
    return legacyBufferFromId(bufferID)->bufferAllocInfo.pMappedData;
}

uint64_t HardwareBuffer::getElementCount() const
{
    return legacyBufferFromId(bufferID)->elementCount;
}

uint64_t HardwareBuffer::getElementSize() const
{
    return legacyBufferFromId(bufferID)->elementSize;
}

ExternalHandle HardwareBuffer::exportBufferMemory()
{
    ExternalHandle winHandle{};
    const auto bufferHandle = legacyBufferFromId(bufferID);

    ResourceManager::ExternalMemoryHandle memory_handle = bufferHandle->resourceManager->exportBufferMemory(*bufferHandle);
#if _WIN32 || _WIN64
    winHandle.handle = memory_handle.handle;
#else
    winHandle.fd = memory_handle.fd;
#endif

    return winHandle;
}
