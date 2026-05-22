

namespace Corona::Horizon
{
    VkBufferUsageFlags to_vk_buffer_usage(BufferUsageFlags usage)
    {
        VkBufferUsageFlags result = 0;

        if (hasFlag(usage, BufferUsageFlags::TransferSrc))
            result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        if (hasFlag(usage, BufferUsageFlags::TransferDst))
            result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        if (hasFlag(usage, BufferUsageFlags::Vertex))
            result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        if (hasFlag(usage, BufferUsageFlags::Index))
            result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        if (hasFlag(usage, BufferUsageFlags::Uniform))
            result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        if (hasFlag(usage, BufferUsageFlags::Storage))
            result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        return result;
    }

    uint32_t checked_element_count(const HardwareBufferDesc& desc)
    {
        if (desc.byte_size == 0)
            throw std::runtime_error("HardwareBufferDesc.byte_size must be greater than 0.");

        if (desc.element_stride == 0)
            throw std::runtime_error("HardwareBufferDesc.element_stride must be greater than 0.");

        if (desc.byte_size % desc.element_stride != 0)
            throw std::runtime_error("HardwareBufferDesc.byte_size must be aligned to element_stride.");

        const uint64_t count = desc.byte_size / desc.element_stride;
        if (count > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("HardwareBuffer element count exceeds uint32_t.");

        return static_cast<uint32_t>(count);
    }

    uint32_t checked_allocation_size(uint64_t size)
    {
        if (size > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("External buffer allocation size exceeds uint32_t.");

        return static_cast<uint32_t>(size);
    }

    bool requires_host_mapping(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data)
    {
        return !upload_data.empty() || desc.cpu_access != CpuAccessMode::None;
    }

    bool validate_upload_data(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data)
    {
        if (upload_data.empty())
            return true;

        if (!upload_data.data())
            return Validation::error("HardwareBuffer upload_data must not be null.");

        if (upload_data.size_bytes() > desc.byte_size)
            return Validation::error("HardwareBuffer upload_data exceeds buffer byte_size.");

        return true;
    }

    HardwareBuffer::HardwareBuffer(const HardwareBufferDesc& desc) : HardwareBuffer(desc, {}) {}

    HardwareBuffer::HardwareBuffer(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data)
    {
        if (!Validation::validateBufferDesc(desc, upload_data.empty() ? nullptr : upload_data.data()))
            return;

        if (!validate_upload_data(desc, upload_data))
            return;

        const Id id = global_buffer_storages.allocate();

        try
        {
            {
                auto handle = global_buffer_storages.acquire_write(id);

                const bool host_visible_mapped = requires_host_mapping(desc, upload_data);
                const bool dedicated = desc.dedicated || desc.exportable;

                *handle = globalHardwareContext.getMainDevice()->resource_manager.create_buffer(checked_element_count(desc),
                                                                                                desc.element_stride,
                                                                                                to_vk_buffer_usage(desc.usage),
                                                                                                host_visible_mapped,
                                                                                                dedicated);

                handle->ref_count = 1;

                if (handle->bufferHandle == VK_NULL_HANDLE)
                    throw std::runtime_error("Failed to create HardwareBuffer.");

                if (!upload_data.empty())
                {
                    if (!handle->bufferAllocInfo.pMappedData)
                       throw std::runtime_error("HardwareBuffer upload_data requires mapped buffer memory.");

                    std::memcpy(handle->bufferAllocInfo.pMappedData, upload_data.data(), upload_data.size_bytes());
                }
            }

            buffer_id = id;
        }
        catch (...)
        {
            release_buffer(id);
            throw;
        }
    }

    HardwareBuffer HardwareBuffer::import_external_memory(const ExternalHandle& handle, const HardwareBufferDesc& desc, uint64_t allocation_size)
    {
        if (!Validation::validateBufferDesc(desc, nullptr))
            return {};

        const uint64_t native_allocation_size = allocation_size != 0 ? allocation_size : desc.byte_size;

        if (native_allocation_size < desc.byte_size)
        {
            Validation::error("External buffer allocation_size must not be smaller than desc.byte_size.");
            return {};
        }

        ResourceManager::ExternalMemoryHandle native_handle{};

#if _WIN32 || _WIN64
        native_handle.handle = handle.handle;
#else
        native_handle.fd = handle.fd;
#endif
        const Id id = globalBufferStorages.allocate();

        try
        {
            {
                auto storage_handle = globalBufferStorages.acquire_write(id);

                *storage_handle = globalHardwareContext.getMainDevice()->resourceManager.importBufferMemory(native_handle,
                                                                                                            checked_element_count(desc),
                                                                                                            desc.element_stride,
                                                                                                            checked_allocation_size(native_allocation_size),
                                                                                                            to_vk_buffer_usage(desc.usage));

                storage_handle->refCount = 1;

                if (storage_handle->bufferHandle == VK_NULL_HANDLE)
                    throw std::runtime_error("Failed to import external HardwareBuffer.");
            }

            return HardwareBuffer(id);
        }
        catch (...)
        {
            releaseBuffer(id);
            throw;
        }
    }

    HardwareBuffer::HardwareBuffer(const HardwareBuffer& other)
    {
        const Id id = other.buffer_id;
        retainBuffer(id);
        buffer_id = id;
    }

    HardwareBuffer::HardwareBuffer(HardwareBuffer&& other) noexcept : buffer_id(std::exchange(other.buffer_id, 0)) {}

    HardwareBuffer::~HardwareBuffer()
    {
        reset();
    }

    HardwareBuffer& HardwareBuffer::operator=(const HardwareBuffer& other)
    {
        if (this == &other)
            return *this;

        const Id new_id = other.buffer_id;
        retainBuffer(new_id);

        const Id old_id = std::exchange(buffer_id, new_id);
        releaseBuffer(old_id);

        return *this;
    }

    HardwareBuffer& HardwareBuffer::operator=(HardwareBuffer&& other) noexcept
    {
        if (this == &other)
            return *this;

        const Id old_id = std::exchange(buffer_id, std::exchange(other.buffer_id, 0));
        releaseBuffer(old_id);

        return *this;
    }

    void HardwareBuffer::reset() noexcept
    {
        const Id old_id = std::exchange(buffer_id, 0);
        releaseBuffer(old_id);
    }

    void HardwareBuffer::swap(HardwareBuffer& other) noexcept
    {
        std::swap(buffer_id, other.buffer_id);
    }

    bool HardwareBuffer::valid() const noexcept
    {
        return isBufferAlive(buffer_id);
    }

    uint64_t HardwareBuffer::get_byte_size() const
    {
        if (buffer_id == 0)
            return 0;

        auto handle = globalBufferStorages.try_acquire_read(buffer_id);
        if (!handle)
            return 0;

        return uint64_t(handle->elementCount) * uint64_t(handle->elementSize);
    }

    uint64_t HardwareBuffer::get_element_stride() const
    {
        if (buffer_id == 0)
            return 0;

        auto handle = globalBufferStorages.try_acquire_read(buffer_id);
        return handle ? handle->elementSize : 0;
    }

    uint64_t HardwareBuffer::get_element_count() const
    {
        if (buffer_id == 0)
            return 0;

        auto handle = globalBufferStorages.try_acquire_read(buffer_id);
        return handle ? handle->elementCount : 0;
    }

    void* HardwareBuffer::get_mapped_data() const
    {
        if (buffer_id == 0)
            return nullptr;

        auto handle = globalBufferStorages.try_acquire_read(buffer_id);
        return handle ? handle->bufferAllocInfo.pMappedData : nullptr;
    }

    bool HardwareBuffer::write_bytes(std::span<const std::byte> data, uint64_t offset) const
    {
        if (!Validation::validateBufferHostWrite(this[0], data.data(), data.size_bytes(), offset))
            return false;

        auto handle = globalBufferStorages.acquire_write(buffer_id);

        auto* mapped_data = static_cast<std::byte*>(handle->bufferAllocInfo.pMappedData);
        std::memcpy(mapped_data + static_cast<size_t>(offset), data.data(), data.size_bytes());

        return true;
    }

    bool HardwareBuffer::read_bytes(std::span<std::byte> output, uint64_t offset) const
    {
        if (!Validation::validateBufferHostRead(this[0], output.data(), output.size_bytes(), offset))
            return false;

        auto handle = globalBufferStorages.acquire_write(buffer_id);

        auto* mapped_data = static_cast<const std::byte*>(handle->bufferAllocInfo.pMappedData);
        std::memcpy(output.data(), mapped_data + static_cast<size_t>(offset), output.size_bytes());

        return true;
    }

    BufferCopyCommand HardwareBuffer::copy_to(const HardwareBuffer& dst, BufferRange src, uint64_t dst_offset) const
    {
        const BufferRange resolved = src.resolve(get_byte_size());

        if (resolved.byte_size == 0)
        {
            Validation::error("HardwareBuffer copy_to size must be greater than 0.");
            return {};
        }

        if (!Validation::validateBufferCopy(this[0], dst, resolved.byte_offset, dst_offset, resolved.byte_size))
            return {};

        return BufferCopyCommand(this[0], dst, resolved.byte_offset, dst_offset, resolved.byte_size);
    }

    BufferToImageCommand HardwareBuffer::copy_to(const HardwareImage& dst, uint64_t buffer_offset, uint32_t image_layer, uint32_t image_mip) const
    {
        if (!valid())
            return {};

        return BufferToImageCommand(this[0], dst, buffer_offset, image_layer, image_mip);
    }

    uint32_t HardwareBuffer::store_descriptor() const
    {
        if (!valid())
            return std::numeric_limits<uint32_t>::max();

        auto handle = globalBufferStorages.acquire_write(buffer_id);
        const int32_t index = globalHardwareContext.getMainDevice()->resourceManager.storeDescriptor(handle);

        if (index < 0)
            return std::numeric_limits<uint32_t>::max();

        return static_cast<uint32_t>(index);
    }

    ExternalHandle HardwareBuffer::export_memory() const
    {
        ExternalHandle result{};

        if (!valid())
            return result;

        auto handle = globalBufferStorages.acquire_write(buffer_id);

        ResourceManager::ExternalMemoryHandle native_handle = globalHardwareContext.getMainDevice()->resourceManager.exportBufferMemory(*handle);

#if _WIN32 || _WIN64
        result.handle = native_handle.handle;
#else
        result.fd = native_handle.fd;
#endif

        return result;
    }
}
