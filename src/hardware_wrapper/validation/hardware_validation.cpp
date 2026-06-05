#include "hardware_validation.h"
#include "hardware_wrapper/image_format_layout.h"
#include "horizon.h"

#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

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

        [[nodiscard]] bool is_depth_stencil_format(Format format) noexcept
        {
            switch (format)
            {
            case Format::D16:
            case Format::D24S8:
            case Format::D32:
            case Format::D32S8:
            case Format::X24G8_UINT:
            case Format::X32G8_UINT:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool shader_has_spirv(const PipelineShaderDesc& shader) noexcept
        {
            const auto* spirv = std::get_if<std::vector<uint32_t>>(&shader.module.shaderCode);
            return spirv != nullptr && !spirv->empty();
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

        [[nodiscard]] ImageSubresourceRange resolve_range(ImageSubresourceRange range, const HardwareImageDesc& desc) noexcept
        {
            if (range.layer_count == ImageSubresourceRange::remaining)
                range.layer_count = range.base_layer < desc.array_layers ? desc.array_layers - range.base_layer : 0;

            if (range.mip_count == ImageSubresourceRange::remaining)
                range.mip_count = range.base_mip < desc.mip_levels ? desc.mip_levels - range.base_mip : 0;

            return range;
        }

        [[nodiscard]] bool valid_subresource(const HardwareImageDesc& desc, uint32_t layer, uint32_t mip) noexcept
        {
            return layer < desc.array_layers && mip < desc.mip_levels;
        }

        [[nodiscard]] ImageExtent image_mip_extent(const HardwareImageDesc& desc, uint32_t mip) noexcept
        {
            return detail::mip_extent(desc.extent, mip);
        }

        bool validate_image_host_layout(const HardwareImageDesc& desc,
                                        ImageSubresourceRange range,
                                        uint32_t layer_index,
                                        uint32_t mip_index,
                                        uint64_t host_size,
                                        uint64_t row_pitch,
                                        uint64_t slice_pitch,
                                        std::string_view operation)
        {
            const ImageSubresourceRange current = resolve_range(range, desc);
            if (layer_index >= current.layer_count || mip_index >= current.mip_count)
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " subresource is outside the current image view range.");

            const uint32_t absolute_layer = current.base_layer + layer_index;
            const uint32_t absolute_mip = current.base_mip + mip_index;
            if (!valid_subresource(desc, absolute_layer, absolute_mip))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " subresource exceeds the image descriptor range.");

            const detail::FormatBlockLayout format = detail::format_block_layout(desc.format);
            if (format.bytes_per_block == 0)
            {
                if (is_depth_stencil_format(desc.format))
                    return validation_error(std::string("HardwareImage ") + std::string(operation) + " does not support depth/stencil byte I/O yet.");

                return validation_error(std::string("HardwareImage ") + std::string(operation) + " requires a supported byte-addressable format.");
            }

            const ImageExtent extent = image_mip_extent(desc, absolute_mip);
            const uint32_t row_blocks = detail::div_ceil(extent.width, format.block_width);
            const uint32_t row_count = detail::div_ceil(extent.height, format.block_height);
            const uint32_t slice_count = detail::div_ceil(extent.depth, format.block_depth);

            uint64_t row_bytes = 0;
            if (!detail::checked_mul(row_blocks, format.bytes_per_block, row_bytes))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " row byte size overflows.");

            const uint64_t host_row_pitch = row_pitch != 0 ? row_pitch : row_bytes;
            uint64_t host_slice_pitch = slice_pitch;
            if (host_slice_pitch == 0 && !detail::checked_mul(host_row_pitch, row_count, host_slice_pitch))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " default slice pitch overflows.");

            if (host_row_pitch < row_bytes)
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " row_pitch is smaller than one tightly packed row.");

            uint64_t slice_size = 0;
            if (!detail::strided_byte_size(1, row_count, 0, host_row_pitch, row_bytes, slice_size))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " slice byte size overflows.");

            if (slice_count > 1 && host_slice_pitch < slice_size)
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " slice_pitch is smaller than one caller-data slice.");

            uint64_t required_size = 0;
            if (!detail::strided_byte_size(slice_count, row_count, host_slice_pitch, host_row_pitch, row_bytes, required_size))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " caller byte size overflows.");

            if (host_size < required_size)
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " caller data is smaller than the requested subresource.");

            return true;
        }

        bool resolve_tightly_packed_image_bytes(const HardwareImageDesc& desc,
                                                ImageSubresourceRange range,
                                                uint32_t layer_index,
                                                uint32_t mip_index,
                                                uint64_t& required_size,
                                                std::string_view operation)
        {
            required_size = 0;

            const ImageSubresourceRange current = resolve_range(range, desc);
            if (layer_index >= current.layer_count || mip_index >= current.mip_count)
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " subresource is outside the current image view range.");

            const uint32_t absolute_layer = current.base_layer + layer_index;
            const uint32_t absolute_mip = current.base_mip + mip_index;
            if (!valid_subresource(desc, absolute_layer, absolute_mip))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " subresource exceeds the image descriptor range.");

            const detail::FormatBlockLayout format = detail::format_block_layout(desc.format);
            if (format.bytes_per_block == 0)
            {
                if (is_depth_stencil_format(desc.format))
                    return validation_error(std::string("HardwareImage ") + std::string(operation) + " does not support depth/stencil buffer copy byte layout yet.");

                return validation_error(std::string("HardwareImage ") + std::string(operation) + " requires a supported byte-addressable format.");
            }

            const ImageExtent extent = image_mip_extent(desc, absolute_mip);
            const uint32_t row_blocks = detail::div_ceil(extent.width, format.block_width);
            const uint32_t row_count = detail::div_ceil(extent.height, format.block_height);
            const uint32_t slice_count = detail::div_ceil(extent.depth, format.block_depth);

            uint64_t row_bytes = 0;
            if (!detail::checked_mul(row_blocks, format.bytes_per_block, row_bytes))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " row byte size overflows.");

            uint64_t slice_pitch = 0;
            if (!detail::checked_mul(row_bytes, row_count, slice_pitch))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " slice byte size overflows.");

            if (!detail::strided_byte_size(slice_count, row_count, slice_pitch, row_bytes, row_bytes, required_size))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " byte size overflows.");

            return true;
        }

        bool validate_copyable_image_subresource(const HardwareImageDesc& desc,
                                                 ImageSubresourceRange range,
                                                 uint32_t layer_index,
                                                 uint32_t mip_index,
                                                 ImageUsageFlags required_usage,
                                                 std::string_view operation)
        {
            if (!has_flag(desc.usage, required_usage))
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " requires the matching transfer usage flag.");

            if (desc.sample_count != 1)
                return validation_error(std::string("HardwareImage ") + std::string(operation) + " requires a single-sampled image.");

            uint64_t unused_size = 0;
            return resolve_tightly_packed_image_bytes(desc, range, layer_index, mip_index, unused_size, operation);
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

        if (!upload_data.empty() && upload_data.data() == nullptr)
            return validation_error("HardwareBuffer upload data must not be null.", true);

        if (!upload_data.empty() && desc.cpu_access == CpuAccessMode::None)
        {
            return validation_error(
                "HardwareBuffer initial upload requires host-visible memory; create the buffer without upload data and record HardwareBuffer::upload for device-local buffers.",
                true);
        }

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

    bool validate_buffer_upload(const HardwareBuffer& dst, std::span<const std::byte> data, uint64_t dst_offset)
    {
        if (data.empty())
            return true;

        if (data.data() == nullptr)
            return validation_error("HardwareBuffer upload data must not be null.", true);

        if (!optional_validation_enabled())
            return true;

        if (!dst)
            return validation_error("HardwareBuffer upload requires a valid destination buffer.");

        return validate_range(dst.get_byte_size(), dst_offset, data.size_bytes(), "HardwareBuffer upload range exceeds destination buffer size.");
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

        const uint32_t max_mips = detail::max_mip_levels(desc.extent);
        if (desc.mip_levels > max_mips)
            return validation_error("HardwareImageDesc mip_levels exceeds the extent mip chain length.", true);

        if (!is_supported_sample_count(desc.sample_count))
            return validation_error("HardwareImageDesc sample_count must be 1, 2, 4, 8, 16, 32, or 64.", true);

        if (desc.dimension == ImageDimension::Image3D && desc.array_layers != 1)
            return validation_error("3D HardwareImage resources must use one array layer.", true);

        if ((desc.dimension == ImageDimension::Cube || desc.dimension == ImageDimension::CubeArray) && desc.array_layers % 6 != 0)
            return validation_error("Cube HardwareImage resources require an array layer count divisible by 6.", true);

        if (desc.sample_count > 1 && desc.mip_levels > 1)
            return validation_error("Multisampled HardwareImage resources must use one mip level.", true);

        if (!upload_data.empty() && upload_data.data() == nullptr)
            return validation_error("HardwareImage upload data must not be null.", true);

        if (!upload_data.empty() && desc.cpu_access == CpuAccessMode::None)
        {
            return validation_error(
                "HardwareImage initial upload requires host-visible memory; create the image without upload data and record an explicit upload/copy path for device-local images.",
                true);
        }

        if (!optional_validation_enabled())
            return true;

        if (desc.usage == ImageUsageFlags::None)
            return validation_error("HardwareImageDesc usage must not be None.");

        if (desc.cpu_access != CpuAccessMode::None && desc.sample_count > 1)
            return validation_error("Host-visible HardwareImage resources must not be multisampled.");

        if (desc.exportable && !desc.dedicated)
            validation_warning("Exportable HardwareImage will force dedicated allocation.");

        return true;
    }

    bool validate_image_upload(const HardwareImageDesc& desc,
                               ImageSubresourceRange range,
                               uint32_t layer_index,
                               uint32_t mip_index,
                               std::span<const std::byte> data)
    {
        if (data.empty())
            return true;

        if (data.data() == nullptr)
            return validation_error("HardwareImage upload data must not be null.", true);

        if (!optional_validation_enabled())
            return true;

        uint64_t required_size = 0;
        if (!validate_copyable_image_subresource(desc, range, layer_index, mip_index, ImageUsageFlags::TransferDst, "upload") ||
            !resolve_tightly_packed_image_bytes(desc, range, layer_index, mip_index, required_size, "upload"))
        {
            return false;
        }

        if (data.size_bytes() < required_size)
            return validation_error("HardwareImage upload data is smaller than the requested subresource.");

        return true;
    }

    bool validate_image_copy(const HardwareImageDesc& src_desc,
                             ImageSubresourceRange src_range,
                             uint32_t src_layer,
                             uint32_t src_mip,
                             const HardwareImageDesc& dst_desc,
                             ImageSubresourceRange dst_range,
                             uint32_t dst_layer,
                             uint32_t dst_mip)
    {
        if (!optional_validation_enabled())
            return true;

        if (src_desc.format != dst_desc.format)
            return validation_error("HardwareImage copy requires source and destination images with the same Format.");

        if (!validate_copyable_image_subresource(src_desc, src_range, src_layer, src_mip, ImageUsageFlags::TransferSrc, "copy source"))
            return false;

        if (!validate_copyable_image_subresource(dst_desc, dst_range, dst_layer, dst_mip, ImageUsageFlags::TransferDst, "copy destination"))
            return false;

        return true;
    }

    bool validate_buffer_to_image_copy(const HardwareBuffer& src,
                                       uint64_t buffer_offset,
                                       const HardwareImageDesc& dst_desc,
                                       ImageSubresourceRange dst_range,
                                       uint32_t dst_layer,
                                       uint32_t dst_mip)
    {
        if (!optional_validation_enabled())
            return true;

        if (!src)
            return validation_error("Buffer-to-image copy requires a valid source HardwareBuffer.");

        uint64_t required_size = 0;
        if (!validate_copyable_image_subresource(dst_desc, dst_range, dst_layer, dst_mip, ImageUsageFlags::TransferDst, "copy destination") ||
            !resolve_tightly_packed_image_bytes(dst_desc, dst_range, dst_layer, dst_mip, required_size, "copy destination"))
        {
            return false;
        }

        return validate_range(src.get_byte_size(), buffer_offset, required_size, "Buffer-to-image copy source range exceeds source buffer size.");
    }

    bool validate_image_to_buffer_copy(const HardwareImageDesc& src_desc,
                                       ImageSubresourceRange src_range,
                                       uint32_t src_layer,
                                       uint32_t src_mip,
                                       const HardwareBuffer& dst,
                                       uint64_t buffer_offset)
    {
        if (!optional_validation_enabled())
            return true;

        if (!dst)
            return validation_error("Image-to-buffer copy requires a valid destination HardwareBuffer.");

        uint64_t required_size = 0;
        if (!validate_copyable_image_subresource(src_desc, src_range, src_layer, src_mip, ImageUsageFlags::TransferSrc, "copy source") ||
            !resolve_tightly_packed_image_bytes(src_desc, src_range, src_layer, src_mip, required_size, "copy source"))
        {
            return false;
        }

        return validate_range(dst.get_byte_size(), buffer_offset, required_size, "Image-to-buffer copy destination range exceeds destination buffer size.");
    }

    bool validate_image_host_write(const HardwareImageDesc& desc,
                                   ImageSubresourceRange range,
                                   uint32_t layer_index,
                                   uint32_t mip_index,
                                   std::span<const std::byte> data,
                                   uint64_t row_pitch,
                                   uint64_t slice_pitch)
    {
        if (data.empty())
            return true;

        if (data.data() == nullptr)
            return validation_error("HardwareImage write data must not be null.");

        if (!optional_validation_enabled())
            return true;

        if (desc.cpu_access != CpuAccessMode::Write && desc.cpu_access != CpuAccessMode::ReadWrite)
            return validation_error("HardwareImage write requires host-write image memory.");

        return validate_image_host_layout(desc, range, layer_index, mip_index, data.size_bytes(), row_pitch, slice_pitch, "write");
    }

    bool validate_image_host_read(const HardwareImageDesc& desc,
                                  ImageSubresourceRange range,
                                  uint32_t layer_index,
                                  uint32_t mip_index,
                                  std::span<std::byte> output,
                                  uint64_t row_pitch,
                                  uint64_t slice_pitch)
    {
        if (output.empty())
            return true;

        if (output.data() == nullptr)
            return validation_error("HardwareImage read output must not be null.");

        if (!optional_validation_enabled())
            return true;

        if (desc.cpu_access != CpuAccessMode::Read && desc.cpu_access != CpuAccessMode::ReadWrite)
            return validation_error("HardwareImage read requires host-readable image memory.");

        return validate_image_host_layout(desc, range, layer_index, mip_index, output.size_bytes(), row_pitch, slice_pitch, "read");
    }

    bool validate_rasterizer_pipeline_desc(const RasterizerPipelineDesc& desc)
    {
        if (!desc.vertex_shader.is_vertex())
            return validation_error("RasterizerPipelineDesc requires a vertex shader.", true);

        if (!desc.fragment_shader.is_fragment())
            return validation_error("RasterizerPipelineDesc requires a fragment shader.", true);

        if (!shader_has_spirv(desc.vertex_shader))
            return validation_error("RasterizerPipelineDesc vertex shader must contain SPIR-V code.", true);

        if (!shader_has_spirv(desc.fragment_shader))
            return validation_error("RasterizerPipelineDesc fragment shader must contain SPIR-V code.", true);

        if (desc.multiview_count == 0)
            return validation_error("RasterizerPipelineDesc multiview_count must be greater than zero.", true);

        const uint32_t sample_count = static_cast<uint32_t>(desc.multisample.sample_count);
        if (!is_supported_sample_count(sample_count))
            return validation_error("RasterizerPipelineDesc multisample sample_count is not supported.", true);

        if (!std::isfinite(desc.rasterizer.line_width) || desc.rasterizer.line_width <= 0.0f)
            return validation_error("RasterizerPipelineDesc rasterizer line_width must be finite and greater than zero.", true);

        if (!std::isfinite(desc.multisample.min_sample_shading) ||
            desc.multisample.min_sample_shading < 0.0f ||
            desc.multisample.min_sample_shading > 1.0f)
        {
            return validation_error("RasterizerPipelineDesc min_sample_shading must be in [0, 1].", true);
        }

        if (!optional_validation_enabled())
            return true;

        if (desc.depth_attachment.enabled)
        {
            if (!is_depth_stencil_format(desc.depth_attachment.format))
                return validation_error("RasterizerPipelineDesc depth_attachment requires a depth/stencil format.");
        }
        else if (desc.depth_stencil.depth_test_enabled || desc.depth_stencil.stencil_test_enabled)
        {
            validation_warning("RasterizerPipelineDesc enables depth/stencil tests without a depth attachment.");
        }

        if (desc.blend.attachments.empty())
            validation_warning("RasterizerPipelineDesc has no color blend attachments; bind render targets before recording draw work.");

        return true;
    }

    bool validate_rasterizer_pipeline_record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params)
    {
        if (!optional_validation_enabled())
            return true;

        if (!index_buffer || !vertex_buffer)
            return validation_error("RasterizerPipeline::record requires valid index and vertex buffers.");

        const uint64_t index_element_size = index_buffer.get_element_size();
        if (!is_index_element_size(static_cast<uint32_t>(index_element_size)))
            return validation_error("RasterizerPipeline::record index buffer element size must be 2 or 4 bytes.");

        const uint64_t index_count = index_buffer.get_element_count();
        if (index_count == 0)
            return validation_error("RasterizerPipeline::record index buffer must contain at least one element.");

        if (params.first_index > index_count)
            return validation_error("RasterizerPipeline::record first_index exceeds the index buffer element count.");

        const uint64_t requested_count = params.index_count == 0
                                             ? index_count - params.first_index
                                             : params.index_count;
        if (requested_count == 0)
            return validation_error("RasterizerPipeline::record index_count resolves to zero.");

        if (requested_count > index_count - params.first_index)
            return validation_error("RasterizerPipeline::record index range exceeds the index buffer element count.");

        if (vertex_buffer.get_element_count() == 0 || vertex_buffer.get_element_size() == 0)
            return validation_error("RasterizerPipeline::record vertex buffer must contain typed vertex elements.");

        return true;
    }
}
