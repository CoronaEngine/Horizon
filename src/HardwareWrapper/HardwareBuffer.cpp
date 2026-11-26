#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareContext.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"

#include <stdexcept>

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
        vkUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        break;
    default:
        break;
    }

    return vkUsage;
}

namespace {
ResourceManager::BufferHardwareWrap& requireBuffer(ResourceManager::BufferHardwareWrap* buffer)
{
    if (!buffer) throw std::runtime_error("Empty HardwareBuffer handle");
    return *buffer;
}

void releaseBuffer(ResourceManager::BufferHardwareWrap* buffer)
{
    if (buffer && --buffer->refCount == 0)
    {
        buffer->resourceManager->destroyBuffer(*buffer);
        delete buffer;
    }
}
}

HardwareBuffer::HardwareBuffer() = default;

HardwareBuffer HardwareBuffer::rasterUpload(uint32_t bytes)
{
    HardwareBuffer buffer;
    auto& manager = getHardwareContext().getMainDevice()->resourceManager;
    constexpr auto usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer.impl_ = new Impl(manager.createBuffer(1, bytes, usage, true, true, true));
    return buffer;
}

HardwareExecutor::Command HardwareBuffer::createDeviceLocal(BufferUsage usage, const void* data,
                                                            size_t elementCount, size_t elementSize)
{
    if (elementSize == 0)
        throw std::invalid_argument("Device-local buffer element size must be nonzero");
    if (usage == BufferUsage::VertexBuffer && elementSize % 4 != 0)
        throw std::invalid_argument("Raster vertex stride must be divisible by four");
    if (usage == BufferUsage::IndexBuffer && elementSize != 2 && elementSize != 4)
        throw std::invalid_argument("Raster indices must be 2 or 4 bytes wide");
    if (elementCount && !data)
        throw std::invalid_argument("A nonempty device-local buffer requires initial data");

    if (elementCount > UINT32_MAX || elementSize > UINT32_MAX)
        throw std::length_error("Device-local buffer dimensions exceed the supported range");

    const auto count = static_cast<uint32_t>(elementCount);
    const auto stride = static_cast<uint32_t>(elementSize);
    HardwareBuffer destination;
    auto& manager = getHardwareContext().getMainDevice()->resourceManager;
    destination.impl_ = new Impl(manager.createBuffer(count, stride, convertBufferUsage(usage), false, true));
    HardwareExecutor::Command upload(nullptr);
    if (count) {
        HardwareBuffer staging(count, stride, BufferUsage::StorageBuffer, data);
        upload = staging.copyTo(destination);
    }
    *this = std::move(destination);
    return upload;
}

HardwareBuffer::HardwareBuffer(uint32_t bufferSize, uint32_t elementSize, BufferUsage usage, const void* data, bool useDedicated)
{
    HardwareBuffer buffer;
    auto& manager = getHardwareContext().getMainDevice()->resourceManager;
    buffer.impl_ = new Impl(manager.createBuffer(bufferSize, elementSize, convertBufferUsage(usage), true, useDedicated));
    if (data && buffer.impl_->bufferAllocInfo.pMappedData)
        manager.writeMapped(*buffer.impl_, data, static_cast<size_t>(bufferSize) * elementSize);
    *this = std::move(buffer);
}

HardwareBuffer::HardwareBuffer(const ExternalHandle& memHandle, uint32_t bufferSize, uint32_t elementSize, uint32_t allocSize, BufferUsage usage)
{
    ResourceManager::ExternalMemoryHandle memoryHandle;
#if _WIN32 || _WIN64
    memoryHandle.handle = memHandle.handle;
#else
    memoryHandle.fd = memHandle.fd;
#endif
    auto& manager = getHardwareContext().getMainDevice()->resourceManager;
    impl_ = new Impl(manager.importBufferMemory(memoryHandle, bufferSize, elementSize, allocSize, convertBufferUsage(usage)));
}

HardwareBuffer::HardwareBuffer(const HardwareBuffer& other) : impl_(other.impl_)
{
    if (impl_) ++impl_->refCount;
}

HardwareBuffer::HardwareBuffer(HardwareBuffer&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}

HardwareBuffer::~HardwareBuffer()
{
    releaseBuffer(impl_);
}

HardwareBuffer& HardwareBuffer::operator=(const HardwareBuffer& other)
{
    if (impl_ == other.impl_) return *this;
    if (other.impl_) ++other.impl_->refCount;
    releaseBuffer(impl_);
    impl_ = other.impl_;
    return *this;
}

HardwareBuffer& HardwareBuffer::operator=(HardwareBuffer&& other) noexcept
{
    if (this == &other) return *this;
    releaseBuffer(impl_);
    impl_ = std::exchange(other.impl_, nullptr);
    return *this;
}

HardwareBuffer::operator bool() const
{
    return impl_ && impl_->bufferHandle != VK_NULL_HANDLE;
}

uint32_t HardwareBuffer::storeDescriptor() const
{
    if (!*this) throw std::invalid_argument("Cannot bind an empty buffer");
    return impl_->resourceManager->storeDescriptor(*impl_);
}

bool HardwareBuffer::copyFromData(const void *inputData, const uint64_t size) const
{
    if (inputData == nullptr || size == 0)
    {
        return false;
    }
    if (!impl_)
    {
        CFW_LOG_WARNING("Cannot copy data to an uninitialized HardwareBuffer.");
        return false;
    }
    if (!impl_->bufferAllocInfo.pMappedData || size > uint64_t(impl_->elementCount) * impl_->elementSize) return false;
    impl_->resourceManager->writeMapped(*impl_, inputData, size);
    return true;
}

bool HardwareBuffer::copyToData(void *outputData, const uint64_t size) const
{
    if (outputData == nullptr || size == 0)
    {
        return false;
    }
    if (!impl_)
    {
        CFW_LOG_WARNING("Cannot copy uninitialized HardwareBuffer to out data.");
        return false;
    }
    if (!impl_->bufferAllocInfo.pMappedData || size > uint64_t(impl_->elementCount) * impl_->elementSize) return false;
    impl_->resourceManager->copyBufferToHost(*impl_, outputData, size);
    return true;
}

void *HardwareBuffer::getMappedData() const
{
    return requireBuffer(impl_).bufferAllocInfo.pMappedData;
}

void HardwareBuffer::flushMapped() const
{
    auto& buffer = requireBuffer(impl_);
    if (!buffer.bufferAllocInfo.pMappedData)
        throw std::logic_error("Cannot flush an unmapped buffer");
    buffer.resourceManager->flushMapped(buffer, size_t(buffer.elementCount) * buffer.elementSize);
}

uint64_t HardwareBuffer::getElementCount() const
{
    return requireBuffer(impl_).elementCount;
}

uint64_t HardwareBuffer::getElementSize() const
{
    return requireBuffer(impl_).elementSize;
}

ExternalHandle HardwareBuffer::exportBufferMemory()
{
    ExternalHandle winHandle{};
    auto& buffer = requireBuffer(impl_);
    ResourceManager::ExternalMemoryHandle memory_handle = buffer.resourceManager->exportBufferMemory(buffer);
#if _WIN32 || _WIN64
    winHandle.handle = memory_handle.handle;
#else
    winHandle.fd = memory_handle.fd;
#endif

    return winHandle;
}
