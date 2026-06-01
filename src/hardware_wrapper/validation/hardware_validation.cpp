#include "hardware_validation.h"
#include "horizon_refac.h"

#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

#include "corona/kernel/core/i_logger.h"

namespace Corona::Horizon
{
    namespace
    {
        std::mutex validation_config_mutex;
        HardwareValidationConfig validation_config;

        [[nodiscard]] bool validation_compiled() noexcept
        {
#if HORIZON_ENABLE_VALIDATION
            return true;
#else
            return false;
#endif
        }

        [[nodiscard]] bool is_index_element_size(uint32_t element_size) noexcept
        {
            return element_size == sizeof(uint16_t) || element_size == sizeof(uint32_t);
        }

        [[nodiscard]] bool is_supported_sample_count(uint32_t sample_count) noexcept
        {
            switch (sample_count)
            {
            case 1:
            case 2:
            case 4:
            case 8:
            case 16:
            case 32:
            case 64:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] HardwareValidationConfig read_validation_config()
        {
            std::lock_guard lock(validation_config_mutex);
            return validation_config;
        }

        [[nodiscard]] bool optional_validation_enabled()
        {
            return validation_compiled() && read_validation_config().mode != HardwareValidationMode::Disabled;
        }

        bool validation_error(std::string_view message, bool hard_error = false)
        {
            const HardwareValidationConfig config = read_validation_config();

            if (hard_error || (validation_compiled() && config.mode == HardwareValidationMode::Throw))
                throw std::invalid_argument(std::string(message));

#if HORIZON_ENABLE_VALIDATION
            if (config.mode == HardwareValidationMode::Log)
                CFW_LOG_ERROR("[Horizon validation] {}", message);
#else
            (void)message;
#endif

            return false;
        }

        void validation_warning(std::string_view message)
        {
#if HORIZON_ENABLE_VALIDATION
            if (read_validation_config().mode != HardwareValidationMode::Disabled)
                CFW_LOG_WARNING("[Horizon validation] {}", message);
#else
            (void)message;
#endif
        }

        bool validate_range(uint64_t total_size, uint64_t offset, uint64_t size, std::string_view message)
        {
            if (offset > total_size)
                return validation_error(message);

            if (size > total_size - offset)
                return validation_error(message);

            return true;
        }
    }

    void set_hardware_validation_config(HardwareValidationConfig config)
    {
        if (!validation_compiled())
            config.mode = HardwareValidationMode::Disabled;

        std::lock_guard lock(validation_config_mutex);
        validation_config = config;
    }

    HardwareValidationConfig get_hardware_validation_config()
    {
        HardwareValidationConfig config = read_validation_config();
        if (!validation_compiled())
            config.mode = HardwareValidationMode::Disabled;
        return config;
    }

    bool is_hardware_validation_enabled()
    {
        return optional_validation_enabled();
    }

    bool validate_buffer_source_data(std::span<const std::byte> data, uint32_t element_size)
    {
        if (element_size == 0)
            return validation_error("element_size must not be zero.", true);

        if (data.size_bytes() % element_size != 0)
            return validation_error("data size must be divisible by element_size.", true);

        return true;
    }

    bool validate_buffer_desc(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data)
    {
        if (desc.element_size == 0)
            return validation_error("HardwareBufferDesc element_size must not be zero.", true);

        const uint64_t total_byte_size = desc.byte_size();
        if (upload_data.size_bytes() > total_byte_size)
            return validation_error("HardwareBuffer upload data exceeds buffer byte size.", true);

        if (!optional_validation_enabled())
            return true;

        if (desc.element_count == 0)
            return validation_error("HardwareBufferDesc element_count must not be zero.");

        if (desc.usage == BufferUsageFlags::None)
            return validation_error("HardwareBufferDesc usage must not be None.");

        if (has_flag(desc.usage, BufferUsageFlags::Index) && !is_index_element_size(desc.element_size))
            return validation_error("HardwareBufferDesc index element_size must be 2 or 4 bytes.");

        if (desc.exportable && !desc.dedicated)
            validation_warning("Exportable HardwareBuffer will force dedicated allocation.");

        return true;
    }

    bool validate_buffer_copy(const HardwareBuffer& src, const HardwareBuffer& dst, BufferRange src_range, uint64_t dst_offset)
    {
        if (!optional_validation_enabled())
            return true;

        if (!src || !dst)
            return validation_error("Buffer copy requires valid source and destination buffers.");

        const BufferRange resolved = src_range.resolve(src.get_byte_size());
        if (resolved.byte_size == 0)
            return validation_error("Buffer copy size must be greater than zero.");

        if (!validate_range(src.get_byte_size(), resolved.byte_offset, resolved.byte_size, "Buffer copy source range exceeds source buffer size."))
            return false;

        if (!validate_range(dst.get_byte_size(), dst_offset, resolved.byte_size, "Buffer copy destination range exceeds destination buffer size."))
            return false;

        return true;
    }

    bool validate_buffer_host_write(const HardwareBuffer& buffer, std::span<const std::byte> data, uint64_t offset)
    {
        if (!optional_validation_enabled() || data.empty())
            return true;

        if (data.data() == nullptr)
            return validation_error("HardwareBuffer write data must not be null.");

        if (!buffer)
            return validation_error("HardwareBuffer write requires a valid buffer.");

        if (buffer.get_mapped_data() == nullptr)
            return validation_error("HardwareBuffer write requires host-mapped memory.");

        return validate_range(buffer.get_byte_size(), offset, data.size_bytes(), "HardwareBuffer write range exceeds buffer size.");
    }

    bool validate_buffer_host_read(const HardwareBuffer& buffer, std::span<std::byte> output, uint64_t offset)
    {
        if (!optional_validation_enabled() || output.empty())
            return true;

        if (output.data() == nullptr)
            return validation_error("HardwareBuffer read output must not be null.");

        if (!buffer)
            return validation_error("HardwareBuffer read requires a valid buffer.");

        if (buffer.get_mapped_data() == nullptr)
            return validation_error("HardwareBuffer read requires host-mapped memory.");

        return validate_range(buffer.get_byte_size(), offset, output.size_bytes(), "HardwareBuffer read range exceeds buffer size.");
    }

    bool validate_image_desc(const HardwareImageDesc& desc, std::span<const std::byte> upload_data)
    {
        if (desc.format == Format::UNKNOWN || desc.format == Format::COUNT)
            return validation_error("HardwareImageDesc format must be a concrete image format.", true);

        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0)
            return validation_error("HardwareImageDesc extent dimensions must be greater than zero.", true);

        if (desc.array_layers == 0)
            return validation_error("HardwareImageDesc array_layers must be greater than zero.", true);

        if (desc.mip_levels == 0)
            return validation_error("HardwareImageDesc mip_levels must be greater than zero.", true);

        if (!is_supported_sample_count(desc.sample_count))
            return validation_error("HardwareImageDesc sample_count must be 1, 2, 4, 8, 16, 32, or 64.", true);

        if (desc.dimension == ImageDimension::Image3D && desc.array_layers != 1)
            return validation_error("3D HardwareImage resources must use one array layer.", true);

        if ((desc.dimension == ImageDimension::Cube || desc.dimension == ImageDimension::CubeArray) && desc.array_layers % 6 != 0)
            return validation_error("Cube HardwareImage resources require an array layer count divisible by 6.", true);

        if (desc.sample_count > 1 && desc.mip_levels > 1)
            return validation_error("Multisampled HardwareImage resources must use one mip level.", true);

        if (!optional_validation_enabled())
            return true;

        if (desc.usage == ImageUsageFlags::None)
            return validation_error("HardwareImageDesc usage must not be None.");

        if (desc.cpu_access != CpuAccessMode::None && desc.sample_count > 1)
            return validation_error("Host-visible HardwareImage resources must not be multisampled.");

        if (desc.exportable && !desc.dedicated)
            validation_warning("Exportable HardwareImage will force dedicated allocation.");

        if (!upload_data.empty() && desc.cpu_access == CpuAccessMode::None)
            validation_warning("HardwareImage upload data only copies immediately for host-visible images until GPU upload commands are encoded.");

        return true;
    }
}
