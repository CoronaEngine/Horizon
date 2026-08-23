#include <cstring>
#include <limits>
#include <stdexcept>

#include "hardware_wrapper_vulkan/hardware/resource_manager.h"
#include "hardware_wrapper_vulkan/hardware/command.h"
#include "horizon.h"
#include "horizon_resource_bridge.h"
#include "validation/hardware_validation.h"

namespace Corona::Horizon
{
    ResourceManager& resource_manager();

    namespace
    {
        [[nodiscard]] ResourceStore<BufferWrap, BufferReleaser>::Read read_buffer(const HardwareBuffer& buffer)
        {
            return read<ResourceStore<BufferWrap, BufferReleaser>>(ResourceBridge::token(buffer));
        }

        [[nodiscard]] ResourceStore<BufferWrap, BufferReleaser>::Write write_buffer(const HardwareBuffer& buffer)
        {
            return write<ResourceStore<BufferWrap, BufferReleaser>>(ResourceBridge::token(buffer));
        }

        [[nodiscard]] bool fits_range(uint64_t total_size, uint64_t byte_offset, size_t byte_count) noexcept
        {
            if (byte_offset > total_size)
                return false;

            if constexpr (sizeof(size_t) > sizeof(uint64_t))
            {
                if (byte_count > std::numeric_limits<uint64_t>::max())
                    return false;
            }

            return static_cast<uint64_t>(byte_count) <= total_size - byte_offset;
        }

        [[nodiscard]] bool offset_to_size_t(uint64_t byte_offset, size_t& out) noexcept
        {
            if (byte_offset > std::numeric_limits<size_t>::max())
                return false;

            out = static_cast<size_t>(byte_offset);
            return true;
        }

    }

    HardwareBuffer::HardwareBuffer(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data)
    {
        if (!validate_buffer_desc(desc, upload_data))
            return;

        auto handle = resource_pool().buffers.create(
            [&desc] {
                return resource_manager().create_buffer(desc);
            });

        ResourceBridge::set(*this, make_token<ResourceStore<BufferWrap, BufferReleaser>>(std::move(handle)));

        if (!upload_data.empty())
        {
            const auto buffer = write_buffer(*this);
            auto* mapped = buffer ? static_cast<std::byte*>(buffer->mapped_data()) : nullptr;
            if (mapped != nullptr && fits_range(buffer->logical_size(), 0, upload_data.size_bytes()) && fits_range(buffer->mapped_size(), 0, upload_data.size_bytes()))
            {
                std::memcpy(mapped, upload_data.data(), upload_data.size_bytes());
                if (buffer->resource_manager != nullptr)
                    buffer->resource_manager->flush_buffer(*buffer, 0, static_cast<uint64_t>(upload_data.size_bytes()));
            }
        }
    }

    HardwareBuffer HardwareBuffer::from_bytes(std::span<const std::byte> data,
                                              uint32_t element_size,
                                              BufferUsageFlags usage,
                                              std::string name)
    {
        if (!validate_buffer_source_data(data, element_size))
            return {};

        HardwareBufferDesc desc;
        desc.element_count = static_cast<uint64_t>(data.size_bytes() / element_size);
        desc.element_size = element_size;
        desc.usage = usage;
        desc.debug_name = std::move(name);
        return HardwareBuffer(desc, data);
    }

    uint64_t HardwareBuffer::get_element_size() const
    {
        const auto buffer = read_buffer(*this);
        return buffer ? buffer->desc.element_size : 0;
    }

    uint64_t HardwareBuffer::get_element_count() const
    {
        const auto buffer = read_buffer(*this);
        return buffer ? buffer->desc.element_count : 0;
    }

    void* HardwareBuffer::get_mapped_data() const
    {
        const auto buffer = read_buffer(*this);
        return buffer ? buffer->mapped_data() : nullptr;
    }

    bool HardwareBuffer::write_bytes(std::span<const std::byte> data, uint64_t byte_offset) const
    {
        if (data.empty())
            return true;

        if (data.data() == nullptr)
            return false;

        if (!validate_buffer_host_write(*this, data, byte_offset))
            return false;

        const auto buffer = write_buffer(*this);
        if (!buffer)
            return false;

        auto* mapped = static_cast<std::byte*>(buffer->mapped_data());
        if (mapped == nullptr)
            return false;

        size_t mapped_offset = 0;
        if (!offset_to_size_t(byte_offset, mapped_offset))
            return false;

        if (!fits_range(buffer->logical_size(), byte_offset, data.size_bytes()))
            return false;

        if (!fits_range(buffer->mapped_size(), byte_offset, data.size_bytes()))
            return false;

        std::memcpy(mapped + mapped_offset, data.data(), data.size_bytes());
        if (buffer->resource_manager != nullptr)
            buffer->resource_manager->flush_buffer(*buffer, byte_offset, static_cast<uint64_t>(data.size_bytes()));
        return true;
    }

    uint32_t HardwareBuffer::store_descriptor() const
    {
        const auto buffer = write_buffer(*this);
        if (!buffer)
            throw std::invalid_argument("HardwareBuffer::store_descriptor requires a valid buffer.");

        return resource_manager().store_descriptor(*buffer);
    }

    // 以下两个从 horizon.h 移出（原为 inline）。只为压缩公共头，语义未变。
    uint64_t HardwareBufferDesc::byte_size() const
    {
        if (element_count == 0 || element_size == 0)
            return 0;

        if (element_count > std::numeric_limits<uint64_t>::max() / element_size)
            throw std::overflow_error("HardwareBufferDesc total byte size overflow.");

        return element_count * uint64_t(element_size);
    }

    uint64_t HardwareBuffer::get_byte_size() const
    {
        const uint64_t element_count = get_element_count();
        const uint64_t element_size = get_element_size();
        if (element_size != 0 && element_count > std::numeric_limits<uint64_t>::max() / element_size)
            throw std::overflow_error("HardwareBuffer total byte size overflow.");

        return element_count * element_size;
    }
}
