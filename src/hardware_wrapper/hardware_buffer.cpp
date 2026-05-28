#include "horizon_refac.h"
#include "validation/hardware_validation.h"
#include "hardware_wrapper_vulkan/resource_pool.h"

namespace Corona::Horizon
{
	HardwareBuffer::HardwareBuffer() : buffer_id(0) {}

	HardwareBuffer::HardwareBuffer(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data)
	{
        buffer_id.store(0, std::memory_order_relaxed);

        if (!validate_buffer_desc(desc, upload_data))
            return;

        auto const buffer_id = globalBufferStorages.allocate();
        bufferID.store(buffer_id, std::memory_order_release);
        auto const handle = globalBufferStorages.acquire_write(buffer_id);
        *handle = globalHardwareContext.getMainDevice()->resourceManager.createBuffer(bufferSize, elementSize, convertBufferUsage(usage), true, useDedicated);

        if (data != nullptr && handle->bufferAllocInfo.pMappedData != nullptr)
        {
            std::memcpy(handle->bufferAllocInfo.pMappedData, data, static_cast<size_t>(bufferSize) * elementSize);
        }
	}

	HardwareBuffer::HardwareBuffer(const HardwareBuffer& other)
	{
        std::lock_guard<std::mutex> lock(other.bufferMutex);
        auto const other_buffer_id = other.bufferID.load(std::memory_order_acquire);
        bufferID.store(other_buffer_id, std::memory_order_release);
        if (other_buffer_id > 0)
        {
            auto const handle = globalBufferStorages.acquire_write(other_buffer_id);
            incrementBufferRefCount(other_buffer_id, handle);
        }
	}

	HardwareBuffer::HardwareBuffer(HardwareBuffer&& other) noexcept
	{
        std::lock_guard<std::mutex> lock(other.bufferMutex);
        auto const other_buffer_id = other.bufferID.load(std::memory_order_relaxed);
        other.bufferID.store(0, std::memory_order_release);
        bufferID.store(other_buffer_id, std::memory_order_release);
	}

	HardwareBuffer::~HardwareBuffer()
	{
        // NOTE: 不要修改写法，避免死锁
        auto const self_buffer_id = bufferID.load(std::memory_order_acquire);
        if (self_buffer_id > 0)
        {
            bool destroy = false;
            if (auto const handle = globalBufferStorages.acquire_write(self_buffer_id);
                decrementBufferRefCount(self_buffer_id, handle))
            {
                destroy = true;
            }
            if (destroy)
            {
                globalBufferStorages.deallocate(self_buffer_id);
            }
        }
	}

	HardwareBuffer& HardwareBuffer::operator=(const HardwareBuffer& other)
	{
        if (this == &other)
        {
            return *this;
        }
        std::scoped_lock lock(bufferMutex, other.bufferMutex);
        auto const self_buffer_id = bufferID.load(std::memory_order_acquire);
        auto const other_buffer_id = other.bufferID.load(std::memory_order_acquire);

        if (self_buffer_id == 0 && other_buffer_id == 0)
        {
            return *this;
        }

        if (self_buffer_id == other_buffer_id)
        {
            return *this;
        }

        if (other_buffer_id == 0)
        {
            bool should_destroy_self = false;
            if (auto const self_handle = globalBufferStorages.acquire_write(self_buffer_id);
                decrementBufferRefCount(self_buffer_id, self_handle))
            {
                should_destroy_self = true;
            }
            if (should_destroy_self)
            {
                globalBufferStorages.deallocate(self_buffer_id);
            }
            bufferID.store(0, std::memory_order_release);
            return *this;
        }

        if (self_buffer_id == 0)
        {
            bufferID.store(other_buffer_id, std::memory_order_release);
            auto const other_handle = globalBufferStorages.acquire_write(other_buffer_id);
            incrementBufferRefCount(other_buffer_id, other_handle);
            return *this;
        }

        bool should_destroy_self = false;
        if (self_buffer_id < other_buffer_id)
        {
            auto const self_handle = globalBufferStorages.acquire_write(self_buffer_id);
            auto const other_handle = globalBufferStorages.acquire_write(other_buffer_id);
            incrementBufferRefCount(other_buffer_id, other_handle);
            if (decrementBufferRefCount(self_buffer_id, self_handle))
            {
                should_destroy_self = true;
            }
        }
        else
        {
            auto const other_handle = globalBufferStorages.acquire_write(other_buffer_id);
            auto const self_handle = globalBufferStorages.acquire_write(self_buffer_id);
            incrementBufferRefCount(other_buffer_id, other_handle);
            if (decrementBufferRefCount(self_buffer_id, self_handle))
            {
                should_destroy_self = true;
            }
        }
        if (should_destroy_self)
        {
            globalBufferStorages.deallocate(self_buffer_id);
        }
        bufferID.store(other_buffer_id, std::memory_order_release);
        return *this;
	}

	HardwareBuffer& HardwareBuffer::operator=(HardwareBuffer&& other) noexcept
	{
        if (this == &other)
        {
            return *this;
        }
        std::scoped_lock lock(bufferMutex, other.bufferMutex);
        auto const self_id = bufferID.load(std::memory_order_acquire);
        auto const other_id = other.bufferID.load(std::memory_order_acquire);

        if (self_id > 0)
        {
            bool should_destroy_self = false;
            if (auto const handle = globalBufferStorages.acquire_write(self_id);
                decrementBufferRefCount(self_id, handle))
            {
                should_destroy_self = true;
            }
            if (should_destroy_self)
            {
                globalBufferStorages.deallocate(self_id);
            }
        }
        bufferID.store(other_id, std::memory_order_release);
        other.bufferID.store(0, std::memory_order_release);
        return *this;
	}

    HardwareBuffer::operator bool() const
    {
        auto const self_buffer_id = bufferID.load(std::memory_order_acquire);
        return self_buffer_id > 0 && globalBufferStorages.acquire_read(self_buffer_id)->bufferHandle != VK_NULL_HANDLE;
    }

    HardwareBuffer HardwareBuffer::from_bytes(std::span<const std::byte> data, uint32_t element_size, BufferUsageFlags usage, std::string name, HardwareBufferOptions options)
    {
        validate_buffer_source_data(data, element_size);

        HardwareBufferDesc desc;
        desc.element_count = uint64_t(data.size_bytes() / element_size);
        desc.element_size = element_size;
        desc.usage = usage;
        desc.debug_name = std::move(name);
        desc.apply(options);
        return HardwareBuffer(desc, data);
    }

    uint64_t HardwareBuffer::get_element_size() const
    {
        return globalBufferStorages.acquire_read(bufferID.load(std::memory_order_acquire))->elementSize;
    }

	uint64_t HardwareBuffer::get_element_count() const
    {
        return globalBufferStorages.acquire_read(bufferID.load(std::memory_order_acquire))->elementCount;
    }

	void* HardwareBuffer::get_mapped_data() const
    {
        return globalBufferStorages.acquire_read(bufferID.load(std::memory_order_acquire))->bufferAllocInfo.pMappedData;
    }

	bool HardwareBuffer::write_bytes(std::span<const std::byte> data, uint64_t offset) const
	{
        if (outputData == nullptr || size == 0)
        {
            return false;
        }
        auto const self_buffer_id = bufferID.load(std::memory_order_acquire);
        if (self_buffer_id == 0)
        {
            CFW_LOG_WARNING("Cannot copy uninitialized HardwareBuffer to out data.");
            return false;
        }
        if (const auto handle = globalBufferStorages.acquire_write(self_buffer_id);
            handle->bufferAllocInfo.pMappedData != nullptr)
        {
            globalHardwareContext.getMainDevice()->resourceManager.copyBufferToHost(*handle, outputData, size);
            return true;
        }
        return false;
	}

	bool HardwareBuffer::read_bytes(std::span<std::byte> output, uint64_t offset) const
	{
        if (inputData == nullptr || size == 0)
        {
            return false;
        }
        auto const self_buffer_id = bufferID.load(std::memory_order_acquire);
        if (self_buffer_id == 0)
        {
            CFW_LOG_WARNING("Cannot copy data to an uninitialized HardwareBuffer.");
            return false;
        }
        if (const auto handle = globalBufferStorages.acquire_write(self_buffer_id);
            handle->bufferAllocInfo.pMappedData != nullptr)
        {
            std::memcpy(handle->bufferAllocInfo.pMappedData, inputData, size);
            return true;
        }
        return false;
	}

	BufferCopyCommand HardwareBuffer::copy_to(const HardwareBuffer& dst, BufferRange src, uint64_t dst_offset) const
	{
        return BufferCopyCommand(*this, dst, srcOffset, dstOffset, size);
	}

	BufferToImageCommand HardwareBuffer::copy_to(const HardwareImage& dst, uint64_t buffer_offset, uint32_t image_layer, uint32_t image_mip) const
	{
        return BufferToImageCommand(*this, dst, bufferOffset, imageLayer, imageMip);
	}

    uint32_t HardwareBuffer::store_descriptor() const
	{
        auto bufferHandle = globalBufferStorages.acquire_write(bufferID);
        return globalHardwareContext.getMainDevice()->resourceManager.storeDescriptor(bufferHandle);
	}

	HardwareBuffer HardwareBuffer::import_external(const ExternalMemoryHandle& handle, const HardwareBufferDesc& desc)
	{
        if (!validate_buffer_desc(desc))
            return {};

        ResourceManager::ExternalMemoryHandle memory_handle;
#if _WIN32 || _WIN64
        memory_handle.handle = memHandle.handle;
#else
        memory_handle.fd = memHandle.fd;
#endif

        const VkBufferUsageFlags vkUsage = convertBufferUsage(usage);
        auto const buffer_id = globalBufferStorages.allocate();
        bufferID.store(buffer_id, std::memory_order_release);
        auto const bufferHandle = globalBufferStorages.acquire_write(buffer_id);
        *bufferHandle = globalHardwareContext.getMainDevice()->resourceManager.importBufferMemory(memory_handle, bufferSize, elementSize, allocSize, vkUsage);
	}

	ExternalMemoryHandle HardwareBuffer::export_external() const
	{
        ExternalHandle winHandle{};
        const auto bufferHandle = globalBufferStorages.acquire_write(bufferID.load(std::memory_order_acquire));

        ResourceManager::ExternalMemoryHandle memory_handle = globalHardwareContext.getMainDevice()->resourceManager.exportBufferMemory(*bufferHandle);
#if _WIN32 || _WIN64
        winHandle.handle = memory_handle.handle;
#else
        winHandle.fd = memory_handle.fd;
#endif

        return winHandle;
	}
}
