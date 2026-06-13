#include "hardware_wrapper_vulkan/hardware/command.h"
#include "hardware_wrapper_vulkan/hardware/resource_manager.h"
#include "horizon.h"
#include "image_format_layout.h"
#include "validation/hardware_validation.h"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace Corona::Horizon
{
    ResourceManager& resource_manager();

    namespace
    {
        [[nodiscard]] ResourceStore<ImageWrap, ImageReleaser>::Read read_image(const HardwareImage& image)
        {
            return read<ResourceStore<ImageWrap, ImageReleaser>>(ResourceBridge::token(image));
        }

        [[nodiscard]] ResourceStore<ImageWrap, ImageReleaser>::Write write_image(const HardwareImage& image)
        {
            return write<ResourceStore<ImageWrap, ImageReleaser>>(ResourceBridge::token(image));
        }

        [[nodiscard]] ResourceStore<BufferWrap, BufferReleaser>::Read read_buffer(const HardwareBuffer& buffer)
        {
            return read<ResourceStore<BufferWrap, BufferReleaser>>(ResourceBridge::token(buffer));
        }

        [[nodiscard]] BufferRef buffer_ref(const HardwareBuffer& buffer)
        {
            return { static_cast<const ResourceHandle&>(buffer) };
        }

        [[nodiscard]] ImageRef image_ref(const HardwareImage& image)
        {
            return { static_cast<const ResourceHandle&>(image) };
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

        [[nodiscard]] bool resolve_absolute_subresource(const HardwareImageDesc& desc,
                                                        ImageSubresourceRange range,
                                                        uint32_t layer,
                                                        uint32_t mip,
                                                        ImageSubresource& out) noexcept
        {
            const ImageSubresourceRange current = resolve_range(range, desc);
            if (layer >= current.layer_count || mip >= current.mip_count)
                return false;

            out = { current.base_layer + layer, current.base_mip + mip };
            return valid_subresource(desc, out.layer, out.mip);
        }

        [[nodiscard]] ImageExtent image_mip_extent(const HardwareImageDesc& desc, uint32_t mip) noexcept
        {
            return detail::mip_extent(desc.extent, mip);
        }

        [[nodiscard]] bool fits_range(uint64_t total_size, uint64_t byte_offset, uint64_t byte_count) noexcept
        {
            return byte_offset <= total_size && byte_count <= total_size - byte_offset;
        }

        [[nodiscard]] bool byte_offset_to_size_t(uint64_t byte_offset, size_t& out) noexcept
        {
            if (byte_offset > std::numeric_limits<size_t>::max())
                return false;

            out = static_cast<size_t>(byte_offset);
            return true;
        }

        struct HostImageCopyLayout
        {
            uint64_t row_bytes { 0 };
            uint32_t row_count { 0 };
            uint32_t slice_count { 0 };
            uint64_t host_row_pitch { 0 };
            uint64_t host_slice_pitch { 0 };
            size_t mapped_base_offset { 0 };
            size_t row_size { 0 };
        };

        [[nodiscard]] bool resolve_host_image_copy_layout(const HardwareImageDesc& desc,
                                                          ImageSubresourceLayout layout,
                                                          uint64_t mapped_size,
                                                          uint64_t row_pitch,
                                                          uint64_t slice_pitch,
                                                          uint64_t host_size,
                                                          HostImageCopyLayout& out)
        {
            const detail::FormatBlockLayout format = detail::format_block_layout(desc.format);
            if (format.bytes_per_block == 0 || layout.extent.width == 0 || layout.extent.height == 0 || layout.extent.depth == 0)
                return false;

            out = {};
            out.row_count = detail::div_ceil(layout.extent.height, format.block_height);
            out.slice_count = detail::div_ceil(layout.extent.depth, format.block_depth);

            const uint32_t row_blocks = detail::div_ceil(layout.extent.width, format.block_width);
            if (!detail::checked_mul(row_blocks, format.bytes_per_block, out.row_bytes))
                return false;

            out.host_row_pitch = row_pitch != 0 ? row_pitch : out.row_bytes;
            if (slice_pitch != 0)
            {
                out.host_slice_pitch = slice_pitch;
            }
            else if (!detail::checked_mul(out.host_row_pitch, out.row_count, out.host_slice_pitch))
            {
                return false;
            }

            if (out.host_row_pitch < out.row_bytes || layout.row_pitch < out.row_bytes)
                return false;

            uint64_t host_slice_size = 0;
            uint64_t device_slice_size = 0;
            if (!detail::strided_byte_size(1, out.row_count, 0, out.host_row_pitch, out.row_bytes, host_slice_size) ||
                !detail::strided_byte_size(1, out.row_count, 0, layout.row_pitch, out.row_bytes, device_slice_size))
            {
                return false;
            }

            if ((out.slice_count > 1 && out.host_slice_pitch < host_slice_size) ||
                (out.slice_count > 1 && layout.slice_pitch < device_slice_size))
            {
                return false;
            }

            uint64_t required_host_size = 0;
            uint64_t required_device_size = 0;
            if (!detail::strided_byte_size(out.slice_count,
                                           out.row_count,
                                           out.host_slice_pitch,
                                           out.host_row_pitch,
                                           out.row_bytes,
                                           required_host_size) ||
                !detail::strided_byte_size(out.slice_count,
                                           out.row_count,
                                           layout.slice_pitch,
                                           layout.row_pitch,
                                           out.row_bytes,
                                           required_device_size))
            {
                return false;
            }

            if (host_size < required_host_size || !fits_range(layout.byte_size, 0, required_device_size))
                return false;

            if (!fits_range(mapped_size, layout.byte_offset, layout.byte_size) ||
                !byte_offset_to_size_t(layout.byte_offset, out.mapped_base_offset) ||
                !byte_offset_to_size_t(out.row_bytes, out.row_size))
            {
                return false;
            }

            return true;
        }

        [[nodiscard]] bool copy_host_to_image(std::byte* mapped,
                                              uint64_t mapped_size,
                                              const HardwareImageDesc& desc,
                                              ImageSubresourceLayout layout,
                                              std::span<const std::byte> data,
                                              uint64_t row_pitch,
                                              uint64_t slice_pitch)
        {
            if (mapped == nullptr || data.data() == nullptr)
                return false;

            HostImageCopyLayout copy {};
            if (!resolve_host_image_copy_layout(desc, layout, mapped_size, row_pitch, slice_pitch, data.size_bytes(), copy))
                return false;

            for (uint32_t slice = 0; slice < copy.slice_count; ++slice)
            {
                const uint64_t dst_slice_offset = layout.slice_pitch * slice;
                const uint64_t src_slice_offset = copy.host_slice_pitch * slice;
                for (uint32_t row = 0; row < copy.row_count; ++row)
                {
                    const uint64_t dst_offset = dst_slice_offset + layout.row_pitch * row;
                    const uint64_t src_offset = src_slice_offset + copy.host_row_pitch * row;
                    size_t dst_offset_size = 0;
                    size_t src_offset_size = 0;
                    if (!fits_range(layout.byte_size, dst_offset, copy.row_bytes) ||
                        !fits_range(data.size_bytes(), src_offset, copy.row_bytes) ||
                        !byte_offset_to_size_t(dst_offset, dst_offset_size) ||
                        !byte_offset_to_size_t(src_offset, src_offset_size))
                    {
                        return false;
                    }

                    std::memcpy(mapped + copy.mapped_base_offset + dst_offset_size,
                                data.data() + src_offset_size,
                                copy.row_size);
                }
            }

            return true;
        }

        [[nodiscard]] bool copy_image_to_host(const std::byte* mapped,
                                              uint64_t mapped_size,
                                              const HardwareImageDesc& desc,
                                              ImageSubresourceLayout layout,
                                              std::span<std::byte> output,
                                              uint64_t row_pitch,
                                              uint64_t slice_pitch)
        {
            if (mapped == nullptr || output.data() == nullptr)
                return false;

            HostImageCopyLayout copy {};
            if (!resolve_host_image_copy_layout(desc, layout, mapped_size, row_pitch, slice_pitch, output.size_bytes(), copy))
                return false;

            for (uint32_t slice = 0; slice < copy.slice_count; ++slice)
            {
                const uint64_t src_slice_offset = layout.slice_pitch * slice;
                const uint64_t dst_slice_offset = copy.host_slice_pitch * slice;
                for (uint32_t row = 0; row < copy.row_count; ++row)
                {
                    const uint64_t src_offset = src_slice_offset + layout.row_pitch * row;
                    const uint64_t dst_offset = dst_slice_offset + copy.host_row_pitch * row;
                    size_t src_offset_size = 0;
                    size_t dst_offset_size = 0;
                    if (!fits_range(layout.byte_size, src_offset, copy.row_bytes) ||
                        !fits_range(output.size_bytes(), dst_offset, copy.row_bytes) ||
                        !byte_offset_to_size_t(src_offset, src_offset_size) ||
                        !byte_offset_to_size_t(dst_offset, dst_offset_size))
                    {
                        return false;
                    }

                    std::memcpy(output.data() + dst_offset_size,
                                mapped + copy.mapped_base_offset + src_offset_size,
                                copy.row_size);
                }
            }

            return true;
        }
    }

    HardwareImage::HardwareImage(const HardwareImageDesc& desc, std::span<const std::byte> upload_data)
    {
        if (!validate_image_desc(desc, upload_data))
            return;

        auto handle = resource_pool().images.create(
            [&desc] {
                return resource_manager().create_image(desc);
            });

        ResourceBridge::set(*this, make_token<ResourceStore<ImageWrap, ImageReleaser>>(std::move(handle)));

        if (!upload_data.empty())
            (void)write_bytes(upload_data);
    }

    HardwareImage HardwareImage::whole() const
    {
        HardwareImage image = *this;
        image.range_ = ImageSubresourceRange::whole();
        return image;
    }

    HardwareImage HardwareImage::layer(uint32_t layer_index) const
    {
        const auto image = read_image(*this);
        if (!image)
            return {};

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (layer_index >= current.layer_count)
            return {};

        HardwareImage view = *this;
        view.range_ = {
            .base_layer = current.base_layer + layer_index,
            .layer_count = 1,
            .base_mip = current.base_mip,
            .mip_count = current.mip_count,
        };
        return view;
    }

    HardwareImage HardwareImage::mip(uint32_t mip_index) const
    {
        const auto image = read_image(*this);
        if (!image)
            return {};

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (mip_index >= current.mip_count)
            return {};

        HardwareImage view = *this;
        view.range_ = {
            .base_layer = current.base_layer,
            .layer_count = current.layer_count,
            .base_mip = current.base_mip + mip_index,
            .mip_count = 1,
        };
        return view;
    }

    HardwareImage HardwareImage::subresource(uint32_t layer_index, uint32_t mip_index) const
    {
        const auto image = read_image(*this);
        if (!image)
            return {};

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (layer_index >= current.layer_count || mip_index >= current.mip_count)
            return {};

        HardwareImage view = *this;
        view.range_ = ImageSubresourceRange::single(current.base_layer + layer_index, current.base_mip + mip_index);
        return view;
    }

    uint32_t HardwareImage::subresource_index(uint32_t layer_index, uint32_t mip_index) const
    {
        const auto image = read_image(*this);
        if (!image)
            return 0;

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (layer_index >= current.layer_count || mip_index >= current.mip_count)
            return 0;

        return ImageSubresource { current.base_layer + layer_index, current.base_mip + mip_index }.index(image->desc.mip_levels);
    }

    uint32_t HardwareImage::subresource_count() const noexcept
    {
        const auto image = read_image(*this);
        if (!image)
            return 0;

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (current.layer_count == 0 || current.mip_count == 0)
            return 0;

        if (current.layer_count > std::numeric_limits<uint32_t>::max() / current.mip_count)
            return 0;

        return current.layer_count * current.mip_count;
    }

    ImageExtent HardwareImage::mip_extent(uint32_t mip_index) const
    {
        const auto image = read_image(*this);
        if (!image)
            return {};

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (mip_index >= current.mip_count)
            return {};

        return image_mip_extent(image->desc, current.base_mip + mip_index);
    }

    ImageExtent HardwareImage::extent() const
    {
        return mip_extent(0);
    }

    bool HardwareImage::write_subresource_bytes(uint32_t layer_index,
                                                uint32_t mip_index,
                                                std::span<const std::byte> data,
                                                uint64_t row_pitch,
                                                uint64_t slice_pitch) const
    {
        if (data.empty())
            return true;

        const auto image = write_image(*this);
        if (!image)
            return false;

        if (!validate_image_host_write(image->desc, range_, layer_index, mip_index, data, row_pitch, slice_pitch))
            return false;

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (layer_index >= current.layer_count || mip_index >= current.mip_count)
            return false;

        const uint32_t absolute_layer = current.base_layer + layer_index;
        const uint32_t absolute_mip = current.base_mip + mip_index;
        if (!valid_subresource(image->desc, absolute_layer, absolute_mip))
            return false;

        ImageSubresourceLayout layout = resource_manager().image_subresource_layout(*image, absolute_layer, absolute_mip);
        if (!copy_host_to_image(static_cast<std::byte*>(image->mapped_data()),
                                image->mapped_size(),
                                image->desc,
                                layout,
                                data,
                                row_pitch,
                                slice_pitch))
        {
            return false;
        }

        if (image->resource_manager != nullptr)
            image->resource_manager->flush_image(*image, layout.byte_offset, layout.byte_size);

        return true;
    }

    bool HardwareImage::read_subresource_bytes(uint32_t layer_index,
                                               uint32_t mip_index,
                                               std::span<std::byte> output,
                                               uint64_t row_pitch,
                                               uint64_t slice_pitch) const
    {
        if (output.empty())
            return true;

        const auto image = read_image(*this);
        if (!image)
            return false;

        if (!validate_image_host_read(image->desc, range_, layer_index, mip_index, output, row_pitch, slice_pitch))
            return false;

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (layer_index >= current.layer_count || mip_index >= current.mip_count)
            return false;

        const uint32_t absolute_layer = current.base_layer + layer_index;
        const uint32_t absolute_mip = current.base_mip + mip_index;
        if (!valid_subresource(image->desc, absolute_layer, absolute_mip))
            return false;

        ImageSubresourceLayout layout = resource_manager().image_subresource_layout(*image, absolute_layer, absolute_mip);
        if (image->resource_manager != nullptr)
            image->resource_manager->invalidate_image(*image, layout.byte_offset, layout.byte_size);

        return copy_image_to_host(static_cast<const std::byte*>(image->mapped_data()),
                                  image->mapped_size(),
                                  image->desc,
                                  layout,
                                  output,
                                  row_pitch,
                                  slice_pitch);
    }

    bool HardwareImage::write_bytes(std::span<const std::byte> data, uint64_t row_pitch, uint64_t slice_pitch) const
    {
        return write_subresource_bytes(0, 0, data, row_pitch, slice_pitch);
    }

    bool HardwareImage::read_bytes(std::span<std::byte> output, uint64_t row_pitch, uint64_t slice_pitch) const
    {
        return read_subresource_bytes(0, 0, output, row_pitch, slice_pitch);
    }

    void HardwareImage::set_clear_color(float r, float g, float b, float a)
    {
        const auto image = write_image(*this);
        if (!image)
            return;

        image->clear_value.color = { { r, g, b, a } };
    }

    void HardwareImage::set_clear_depth(float depth, uint32_t stencil)
    {
        const auto image = write_image(*this);
        if (!image)
            return;

        image->clear_value.depthStencil = { depth, stencil };
    }

    CommandBatch HardwareImage::upload(std::span<const std::byte> data, uint32_t image_layer, uint32_t image_mip) const
    {
        CommandBatch batch;
        if (data.empty())
            return batch;

        const auto image = read_image(*this);
        if (!image)
            return batch;

        if (!validate_image_upload(image->desc, range_, image_layer, image_mip, data))
            return batch;

        ImageSubresource absolute {};
        if (!resolve_absolute_subresource(image->desc, range_, image_layer, image_mip, absolute))
            return batch;

        std::string staging_name;
        if (!image->desc.debug_name.empty())
            staging_name = image->desc.debug_name + ".upload";

        HardwareBufferDesc staging_desc;
        staging_desc.element_count = static_cast<uint64_t>(data.size_bytes());
        staging_desc.element_size = 1;
        staging_desc.usage = BufferUsageFlags::TransferSrc;
        staging_desc.cpu_access = CpuAccessMode::Write;
        staging_desc.debug_name = std::move(staging_name);

        HardwareBuffer staging(staging_desc, data);
        if (!staging)
            return batch;

        batch << copy_to_image(buffer_ref(staging), image_ref(*this), { 0, absolute.layer, absolute.mip });
        batch << keep_alive(std::move(staging));
        return batch;
    }

    CopyImageCommand HardwareImage::copy_to(const HardwareImage& dst,
                                            uint32_t src_layer,
                                            uint32_t dst_layer,
                                            uint32_t src_mip,
                                            uint32_t dst_mip) const
    {
        if (!*this || !dst)
            return {};

        const auto src_image = read_image(*this);
        const auto dst_image = read_image(dst);
        if (!src_image || !dst_image)
            return {};

        if (!validate_image_copy(src_image->desc, range_, src_layer, src_mip, dst_image->desc, dst.range_, dst_layer, dst_mip))
            return {};

        ImageSubresource absolute_src {};
        ImageSubresource absolute_dst {};
        if (!resolve_absolute_subresource(src_image->desc, range_, src_layer, src_mip, absolute_src) ||
            !resolve_absolute_subresource(dst_image->desc, dst.range_, dst_layer, dst_mip, absolute_dst))
        {
            return {};
        }

        return copy_image(image_ref(*this), image_ref(dst), { absolute_src.layer, absolute_dst.layer, absolute_src.mip, absolute_dst.mip });
    }

    CopyImageToBufferCommand HardwareImage::copy_to(const HardwareBuffer& dst,
                                                    uint32_t image_layer,
                                                    uint32_t image_mip,
                                                    uint64_t buffer_offset) const
    {
        if (!*this || !dst)
            return {};

        const auto image = read_image(*this);
        if (!image)
            return {};

        if (!validate_image_to_buffer_copy(image->desc, range_, image_layer, image_mip, dst, buffer_offset))
            return {};

        ImageSubresource absolute {};
        if (!resolve_absolute_subresource(image->desc, range_, image_layer, image_mip, absolute))
            return {};

        return copy_to_buffer(image_ref(*this), buffer_ref(dst), { buffer_offset, absolute.layer, absolute.mip });
    }

    CopyBufferToImageCommand HardwareImage::copy_from(const HardwareBuffer& src,
                                                      uint64_t buffer_offset,
                                                      uint32_t image_layer,
                                                      uint32_t image_mip) const
    {
        if (!src || !*this)
            return {};

        const auto buffer = read_buffer(src);
        if (!buffer || buffer_offset > buffer->logical_size())
            return {};

        const auto image = read_image(*this);
        if (!image)
            return {};

        if (!validate_buffer_to_image_copy(src, buffer_offset, image->desc, range_, image_layer, image_mip))
            return {};

        ImageSubresource absolute {};
        if (!resolve_absolute_subresource(image->desc, range_, image_layer, image_mip, absolute))
            return {};

        return copy_to_image(buffer_ref(src), image_ref(*this), { buffer_offset, absolute.layer, absolute.mip });
    }

    uint32_t HardwareImage::store_descriptor() const
    {
        const auto image = write_image(*this);
        if (!image)
            throw std::invalid_argument("HardwareImage::store_descriptor requires a valid image.");

        return resource_manager().store_descriptor(*image);
    }

    uint32_t HardwareImage::store_sampled_descriptor() const
    {
        const auto image = write_image(*this);
        if (!image)
            throw std::invalid_argument("HardwareImage::store_sampled_descriptor requires a valid image.");

        return resource_manager().store_sampled_descriptor(*image);
    }

    uint32_t HardwareImage::store_storage_descriptor() const
    {
        const auto image = write_image(*this);
        if (!image)
            throw std::invalid_argument("HardwareImage::store_storage_descriptor requires a valid image.");

        return resource_manager().store_storage_descriptor(*image);
    }

    HardwareImage HardwareImage::import_external(const ExternalMemoryHandle& handle, const HardwareImageDesc& desc, uint64_t allocation_size)
    {
        if (!handle)
            throw std::invalid_argument("HardwareImage::import_external requires a valid external memory handle.");

        if (!validate_image_desc(desc))
            return {};

        HardwareImage image;
        auto resource = resource_pool().images.create(
            [&handle, &desc, allocation_size] {
                return resource_manager().import_image(handle, desc, allocation_size);
            });

        ResourceBridge::set(image, make_token<ResourceStore<ImageWrap, ImageReleaser>>(std::move(resource)));
        return image;
    }

    ExternalMemoryHandle HardwareImage::export_external() const
    {
        auto image = write_image(*this);
        if (!image)
            throw std::invalid_argument("HardwareImage::export_external requires a valid image.");

        return resource_manager().export_image(*image);
    }

    HardwareImage HardwareImageLayerSelector::operator[](uint32_t mip_index) const
    {
        return image_.subresource(layer_, mip_index);
    }
}
