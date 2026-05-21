#include "validation/hardware_validation.h"

#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>

#include "hardware_wrapper_vulkan/resource_pool.h"

namespace Corona::Horizon
{
    Context g_context{};
    std::mutex g_context_mutex;

    struct BufferInfo
    {
        bool valid = false;
        uint64_t byte_size = 0;
        VkBufferUsageFlags usage = 0;
        void* mapped_data = nullptr;
    };

    BufferInfo read_buffer_info(HardwareBuffer::Id id) noexcept
    {
        BufferInfo info{};

        if (id == 0) return info;

        try
        {
            auto handle = globalBufferStorages.try_acquire_read(id);
            if (!handle || handle->bufferHandle == VK_NULL_HANDLE) return info;

            info.valid = true;
            info.byte_size = uint64_t(handle->elementCount) * uint64_t(handle->elementSize);
            info.usage = handle->bufferUsage;
            info.mapped_data = handle->bufferAllocInfo.pMappedData;
        }
        catch (...)
        {
        }

        return info;
    }

    bool validate_range(uint64_t total_size, uint64_t offset, uint64_t size, std::string_view message)
    {
        if (offset > total_size)
            return error(message);

        if (size > total_size - offset)
            return error(message);

        return true;
    }

    bool is_index_stride(uint32_t stride) noexcept
    {
        return stride == sizeof(uint16_t) || stride == sizeof(uint32_t);
    }

    void set_context(Context context)
    {
        std::lock_guard lock(g_context_mutex);
        g_context = context;
    }

    Context get_context()
    {
        std::lock_guard lock(g_context_mutex);
        return g_context;
    }

    bool error(std::string_view message)
    {
        const Context ctx = get_context();

        if (ctx.enabled)
            CFW_LOG_ERROR("[Horizon validation] {}", message);

        if (ctx.enabled && ctx.throw_on_error)
            throw std::runtime_error(std::string(message));

        return false;
    }

    void warning(std::string_view message)
    {
        const Context ctx = get_context();

        if (ctx.enabled)
            CFW_LOG_WARNING("[Horizon validation] {}", message);
    }

    bool validate_buffer_desc(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data)
    {
        if (desc.byte_size == 0)
            return error("HardwareBufferDesc.byte_size must be greater than 0.");

        if (desc.element_stride == 0)
            return error("HardwareBufferDesc.element_stride must be greater than 0.");

        if (desc.byte_size % desc.element_stride != 0)
            return error("HardwareBufferDesc.byte_size must be aligned to element_stride.");

        if (desc.usage == BufferUsageFlags::None)
            return error("HardwareBufferDesc.usage must not be None.");

        const uint64_t element_count = desc.byte_size / desc.element_stride;
        if (element_count > std::numeric_limits<uint32_t>::max())
            return error("HardwareBuffer element count exceeds uint32_t.");

        if (hasFlag(desc.usage, BufferUsageFlags::Index) && !is_index_stride(desc.element_stride))
            return error("Index buffer element_stride must be 2 or 4 bytes.");

        if (!upload_data.empty())
        {
            if (!upload_data.data())
                return error("HardwareBuffer upload_data must not be null.");

            if (upload_data.size_bytes() > desc.byte_size)
                return error("HardwareBuffer upload_data exceeds buffer byte_size.");
        }

        if (desc.exportable && !desc.dedicated)
            warning("Exportable HardwareBuffer will force dedicated allocation.");

        return true;
    }

    bool validate_buffer_copy(const HardwareBuffer& src, const HardwareBuffer& dst, BufferRange src_range, uint64_t dst_offset)
    {
        const BufferInfo src_info = read_buffer_info(src.get_buffer_id());
        const BufferInfo dst_info = read_buffer_info(dst.get_buffer_id());

        if (!src_info.valid || !dst_info.valid)
            return error("Buffer copy requires valid source and destination buffers.");

        if ((src_info.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0)
            return error("Source buffer was not created with TransferSrc usage.");

        if ((dst_info.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0)
            return error("Destination buffer was not created with TransferDst usage.");

        const BufferRange resolved = src_range.resolve(src_info.byte_size);

        if (resolved.byte_size == 0)
            return error("Buffer copy size must be greater than 0.");

        if (!validate_range(src_info.byte_size, resolved.byte_offset, resolved.byte_size, "Buffer copy source range exceeds source buffer size."))
            return false;

        if (!validate_range(dst_info.byte_size, dst_offset, resolved.byte_size, "Buffer copy destination range exceeds destination buffer size."))
            return false;

        return true;
    }

    bool validate_buffer_host_write(const HardwareBuffer& buffer, std::span<const std::byte> data, uint64_t offset)
    {
        if (data.empty())
            return true;

        if (!data.data())
            return error("HardwareBuffer write data must not be null.");

        const BufferInfo info = read_buffer_info(buffer.get_buffer_id());

        if (!info.valid)
            return error("HardwareBuffer write requires a valid buffer.");

        if (!info.mapped_data)
            return error("HardwareBuffer write requires host-mapped memory.");

        return validate_range(info.byte_size, offset, data.size_bytes(), "HardwareBuffer write range exceeds buffer size.");
    }

    bool validate_buffer_host_read(const HardwareBuffer& buffer, std::span<std::byte> output, uint64_t offset)
    {
        if (output.empty())
            return true;

        if (!output.data())
            return error("HardwareBuffer read output must not be null.");

        const BufferInfo info = read_buffer_info(buffer.get_buffer_id());

        if (!info.valid)
            return error("HardwareBuffer read requires a valid buffer.");

        if (!info.mapped_data)
            return error("HardwareBuffer read requires host-mapped memory.");

        return validate_range(info.byte_size, offset, output.size_bytes(), "HardwareBuffer read range exceeds buffer size.");
    }

    void set_hardware_validation_config(const HardwareValidationConfig& config)
    {
        Validation::set_context({.enabled = config.enabled, .throw_on_error = config.throw_on_error});
    }
}
