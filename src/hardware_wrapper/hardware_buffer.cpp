#include "hardware_wrapper_vulkan/hardware/resource_manager.h"
#include "hardware_wrapper_vulkan/hardware/command.h"
#include "horizon_refac.h"
#include "validation/hardware_validation.h"

#include <cstring>
#include <limits>
#include <stdexcept>

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

        [[nodiscard]] bool fits_range_u64(uint64_t total_size, uint64_t byte_offset, uint64_t byte_count) noexcept
        {
            return byte_offset <= total_size && byte_count <= total_size - byte_offset;
        }

        [[nodiscard]] bool offset_to_size_t(uint64_t byte_offset, size_t& out) noexcept
        {
            if (byte_offset > std::numeric_limits<size_t>::max())
                return false;

            out = static_cast<size_t>(byte_offset);
            return true;
        }

        [[nodiscard]] BufferRef buffer_ref(const HardwareBuffer& buffer)
        {
            return { static_cast<const ResourceHandle&>(buffer) };
        }

        [[nodiscard]] ImageRef image_ref(const HardwareImage& image)
        {
            return { static_cast<const ResourceHandle&>(image) };
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
                                              std::string name,
                                              HardwareBufferOptions options)
    {
        if (!validate_buffer_source_data(data, element_size))
            return {};

        HardwareBufferDesc desc;
        desc.element_count = static_cast<uint64_t>(data.size_bytes() / element_size);
        desc.element_size = element_size;
        desc.usage = usage;
        desc.debug_name = std::move(name);
        desc.apply(options);
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

    bool HardwareBuffer::read_bytes(std::span<std::byte> output, uint64_t byte_offset) const
    {
        if (output.empty())
            return true;

        if (output.data() == nullptr)
            return false;

        if (!validate_buffer_host_read(*this, output, byte_offset))
            return false;

        const auto buffer = read_buffer(*this);
        if (!buffer)
            return false;

        const auto* mapped = static_cast<const std::byte*>(buffer->mapped_data());
        if (mapped == nullptr)
            return false;

        size_t mapped_offset = 0;
        if (!offset_to_size_t(byte_offset, mapped_offset))
            return false;

        if (!fits_range(buffer->logical_size(), byte_offset, output.size_bytes()))
            return false;

        if (!fits_range(buffer->mapped_size(), byte_offset, output.size_bytes()))
            return false;

        if (buffer->resource_manager != nullptr)
            buffer->resource_manager->invalidate_buffer(*buffer, byte_offset, static_cast<uint64_t>(output.size_bytes()));

        std::memcpy(output.data(), mapped + mapped_offset, output.size_bytes());
        return true;
    }

    CopyBufferCommand HardwareBuffer::copy_to(const HardwareBuffer& dst, BufferRange src, uint64_t dst_offset) const
    {
        if (!validate_buffer_copy(*this, dst, src, dst_offset))
            return {};

        if (!*this || !dst)
            return {};

        const uint64_t src_size = get_byte_size();
        const BufferRange resolved = src.resolve(src_size);
        if (resolved.byte_size == 0)
            return {};

        if (!fits_range_u64(src_size, resolved.byte_offset, resolved.byte_size))
            return {};

        if (!fits_range_u64(dst.get_byte_size(), dst_offset, resolved.byte_size))
            return {};

        return copy(buffer_ref(*this), buffer_ref(dst), { resolved.byte_offset, dst_offset, resolved.byte_size });
    }

    CopyBufferToImageCommand HardwareBuffer::copy_to(const HardwareImage& dst, uint64_t buffer_offset, uint32_t image_layer, uint32_t image_mip) const
    {
        if (!*this || !dst)
            return {};

        if (buffer_offset > get_byte_size())
            return {};

        return copy_to_image(buffer_ref(*this), image_ref(dst), { buffer_offset, image_layer, image_mip });
    }

    uint32_t HardwareBuffer::store_descriptor() const
    {
        const auto buffer = write_buffer(*this);
        if (!buffer)
            throw std::invalid_argument("HardwareBuffer::store_descriptor requires a valid buffer.");

        return resource_manager().store_descriptor(*buffer);
    }

    HardwareBuffer HardwareBuffer::import_external(const ExternalMemoryHandle& handle, const HardwareBufferDesc& desc)
    {
        if (!handle)
            throw std::invalid_argument("HardwareBuffer::import_external requires a valid external memory handle.");

        if (!validate_buffer_desc(desc))
            return {};

        HardwareBuffer buffer;
        auto resource = resource_pool().buffers.create(
            [&handle, &desc] {
                return resource_manager().import_buffer(handle, desc);
            });

        ResourceBridge::set(buffer, make_token<ResourceStore<BufferWrap, BufferReleaser>>(std::move(resource)));
        return buffer;
    }

    ExternalMemoryHandle HardwareBuffer::export_external() const
    {
        auto buffer = write_buffer(*this);
        if (!buffer)
            throw std::invalid_argument("HardwareBuffer::export_external requires a valid buffer.");

        return resource_manager().export_buffer(*buffer);
    }
}
