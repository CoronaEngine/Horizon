#include "hardware_wrapper_vulkan/hardware/resource_manager.h"
#include "hardware_wrapper_vulkan/hardware/command.h"
#include "horizon.h"
#include "validation/hardware_validation.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Corona::Horizon
{
    ResourceManager& resource_manager();

    namespace
    {
        using ImageStore = ResourceStore<ImageWrap, ImageReleaser>;
        using BufferStore = ResourceStore<BufferWrap, BufferReleaser>;

        struct FormatLayout
        {
            uint32_t block_width { 1 };
            uint32_t block_height { 1 };
            uint32_t block_depth { 1 };
            uint32_t bytes_per_block { 0 };
        };

        [[nodiscard]] ImageStore::Read read_image(const HardwareImage& image)
        {
            return read<ImageStore>(ResourceBridge::token(image));
        }

        [[nodiscard]] ImageStore::Write write_image(const HardwareImage& image)
        {
            return write<ImageStore>(ResourceBridge::token(image));
        }

        [[nodiscard]] BufferStore::Read read_buffer(const HardwareBuffer& buffer)
        {
            return read<BufferStore>(ResourceBridge::token(buffer));
        }

        [[nodiscard]] BufferRef buffer_ref(const HardwareBuffer& buffer)
        {
            return { static_cast<const ResourceHandle&>(buffer) };
        }

        [[nodiscard]] ImageRef image_ref(const HardwareImage& image)
        {
            return { static_cast<const ResourceHandle&>(image) };
        }

        [[nodiscard]] FormatLayout format_layout(Format format) noexcept
        {
            switch (format)
            {
            case Format::R8_UINT:
            case Format::R8_SINT:
            case Format::R8_UNORM:
            case Format::R8_SNORM:
                return { 1, 1, 1, 1 };
            case Format::RG8_UINT:
            case Format::RG8_SINT:
            case Format::RG8_UNORM:
            case Format::RG8_SNORM:
            case Format::R16_UINT:
            case Format::R16_SINT:
            case Format::R16_UNORM:
            case Format::R16_SNORM:
            case Format::R16_FLOAT:
            case Format::BGRA4_UNORM:
            case Format::B5G6R5_UNORM:
            case Format::B5G5R5A1_UNORM:
            case Format::D16:
                return { 1, 1, 1, 2 };
            case Format::RGBA8_UINT:
            case Format::RGBA8_SINT:
            case Format::RGBA8_UNORM:
            case Format::RGBA8_SNORM:
            case Format::BGRA8_UNORM:
            case Format::BGRX8_UNORM:
            case Format::SRGBA8_UNORM:
            case Format::SBGRA8_UNORM:
            case Format::SBGRX8_UNORM:
            case Format::R10G10B10A2_UNORM:
            case Format::R11G11B10_FLOAT:
            case Format::RG16_UINT:
            case Format::RG16_SINT:
            case Format::RG16_UNORM:
            case Format::RG16_SNORM:
            case Format::RG16_FLOAT:
            case Format::R32_UINT:
            case Format::R32_SINT:
            case Format::R32_FLOAT:
            case Format::D24S8:
            case Format::X24G8_UINT:
            case Format::D32:
            case Format::X32G8_UINT:
                return { 1, 1, 1, 4 };
            case Format::RGBA16_UINT:
            case Format::RGBA16_SINT:
            case Format::RGBA16_FLOAT:
            case Format::RGBA16_UNORM:
            case Format::RGBA16_SNORM:
            case Format::RG32_UINT:
            case Format::RG32_SINT:
            case Format::RG32_FLOAT:
            case Format::D32S8:
                return { 1, 1, 1, 8 };
            case Format::RGB32_UINT:
            case Format::RGB32_SINT:
            case Format::RGB32_FLOAT:
                return { 1, 1, 1, 12 };
            case Format::RGBA32_UINT:
            case Format::RGBA32_SINT:
            case Format::RGBA32_FLOAT:
                return { 1, 1, 1, 16 };
            case Format::BC1_UNORM:
            case Format::BC1_UNORM_SRGB:
            case Format::BC4_UNORM:
            case Format::BC4_SNORM:
                return { 4, 4, 1, 8 };
            case Format::BC2_UNORM:
            case Format::BC2_UNORM_SRGB:
            case Format::BC3_UNORM:
            case Format::BC3_UNORM_SRGB:
            case Format::BC5_UNORM:
            case Format::BC5_SNORM:
            case Format::BC6H_UFLOAT:
            case Format::BC6H_SFLOAT:
            case Format::BC7_UNORM:
            case Format::BC7_UNORM_SRGB:
                return { 4, 4, 1, 16 };
            case Format::UNKNOWN:
            case Format::COUNT:
                break;
            }

            return {};
        }

        [[nodiscard]] uint32_t div_ceil(uint32_t value, uint32_t divisor) noexcept
        {
            return divisor == 0 ? 0 : (value + divisor - 1) / divisor;
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
            return {
                std::max(1u, desc.extent.width >> mip),
                std::max(1u, desc.extent.height >> mip),
                std::max(1u, desc.extent.depth >> mip),
            };
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

            const FormatLayout format = format_layout(desc.format);
            if (format.bytes_per_block == 0)
                return false;

            const uint64_t row_bytes = uint64_t(div_ceil(layout.extent.width, format.block_width)) * format.bytes_per_block;
            const uint32_t row_count = div_ceil(layout.extent.height, format.block_height);
            const uint32_t slice_count = div_ceil(layout.extent.depth, format.block_depth);
            const uint64_t src_row_pitch = row_pitch != 0 ? row_pitch : row_bytes;
            const uint64_t src_slice_pitch = slice_pitch != 0 ? slice_pitch : src_row_pitch * row_count;

            if (src_row_pitch < row_bytes || layout.row_pitch < row_bytes)
                return false;

            if (row_count > 1 && src_slice_pitch < src_row_pitch * (row_count - 1) + row_bytes)
                return false;

            if (slice_count > 1 && layout.slice_pitch < layout.row_pitch * (row_count - 1) + row_bytes)
                return false;

            const uint64_t src_size = slice_count == 0 ? 0 : src_slice_pitch * (slice_count - 1) + src_row_pitch * (row_count - 1) + row_bytes;
            if (data.size_bytes() < src_size)
                return false;

            if (!fits_range(mapped_size, layout.byte_offset, layout.byte_size))
                return false;

            size_t base_offset = 0;
            if (!byte_offset_to_size_t(layout.byte_offset, base_offset))
                return false;

            for (uint32_t slice = 0; slice < slice_count; ++slice)
            {
                const uint64_t dst_slice_offset = layout.slice_pitch * slice;
                const uint64_t src_slice_offset = src_slice_pitch * slice;
                for (uint32_t row = 0; row < row_count; ++row)
                {
                    const uint64_t dst_offset = dst_slice_offset + layout.row_pitch * row;
                    const uint64_t src_offset = src_slice_offset + src_row_pitch * row;
                    if (!fits_range(layout.byte_size, dst_offset, row_bytes) || !fits_range(data.size_bytes(), src_offset, row_bytes))
                        return false;

                    std::memcpy(mapped + base_offset + static_cast<size_t>(dst_offset),
                                data.data() + static_cast<size_t>(src_offset),
                                static_cast<size_t>(row_bytes));
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

            const FormatLayout format = format_layout(desc.format);
            if (format.bytes_per_block == 0)
                return false;

            const uint64_t row_bytes = uint64_t(div_ceil(layout.extent.width, format.block_width)) * format.bytes_per_block;
            const uint32_t row_count = div_ceil(layout.extent.height, format.block_height);
            const uint32_t slice_count = div_ceil(layout.extent.depth, format.block_depth);
            const uint64_t dst_row_pitch = row_pitch != 0 ? row_pitch : row_bytes;
            const uint64_t dst_slice_pitch = slice_pitch != 0 ? slice_pitch : dst_row_pitch * row_count;

            if (dst_row_pitch < row_bytes || layout.row_pitch < row_bytes)
                return false;

            if (row_count > 1 && dst_slice_pitch < dst_row_pitch * (row_count - 1) + row_bytes)
                return false;

            if (slice_count > 1 && layout.slice_pitch < layout.row_pitch * (row_count - 1) + row_bytes)
                return false;

            const uint64_t dst_size = slice_count == 0 ? 0 : dst_slice_pitch * (slice_count - 1) + dst_row_pitch * (row_count - 1) + row_bytes;
            if (output.size_bytes() < dst_size)
                return false;

            if (!fits_range(mapped_size, layout.byte_offset, layout.byte_size))
                return false;

            size_t base_offset = 0;
            if (!byte_offset_to_size_t(layout.byte_offset, base_offset))
                return false;

            for (uint32_t slice = 0; slice < slice_count; ++slice)
            {
                const uint64_t src_slice_offset = layout.slice_pitch * slice;
                const uint64_t dst_slice_offset = dst_slice_pitch * slice;
                for (uint32_t row = 0; row < row_count; ++row)
                {
                    const uint64_t src_offset = src_slice_offset + layout.row_pitch * row;
                    const uint64_t dst_offset = dst_slice_offset + dst_row_pitch * row;
                    if (!fits_range(layout.byte_size, src_offset, row_bytes) || !fits_range(output.size_bytes(), dst_offset, row_bytes))
                        return false;

                    std::memcpy(output.data() + static_cast<size_t>(dst_offset),
                                mapped + base_offset + static_cast<size_t>(src_offset),
                                static_cast<size_t>(row_bytes));
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

        ResourceBridge::set(*this, make_token<ImageStore>(std::move(handle)));

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

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (layer_index >= current.layer_count || mip_index >= current.mip_count)
            return false;

        const uint32_t absolute_layer = current.base_layer + layer_index;
        const uint32_t absolute_mip = current.base_mip + mip_index;
        if (!valid_subresource(image->desc, absolute_layer, absolute_mip))
            return false;

        ImageSubresourceLayout layout = resource_manager().image_subresource_layout(*image, absolute_layer, absolute_mip);
        return copy_host_to_image(static_cast<std::byte*>(image->mapped_data()),
                                  image->mapped_size(),
                                  image->desc,
                                  layout,
                                  data,
                                  row_pitch,
                                  slice_pitch);
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

        const ImageSubresourceRange current = resolve_range(range_, image->desc);
        if (layer_index >= current.layer_count || mip_index >= current.mip_count)
            return false;

        const uint32_t absolute_layer = current.base_layer + layer_index;
        const uint32_t absolute_mip = current.base_mip + mip_index;
        if (!valid_subresource(image->desc, absolute_layer, absolute_mip))
            return false;

        ImageSubresourceLayout layout = resource_manager().image_subresource_layout(*image, absolute_layer, absolute_mip);
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

    CopyImageCommand HardwareImage::copy_to(const HardwareImage& dst,
                                            uint32_t src_layer,
                                            uint32_t dst_layer,
                                            uint32_t src_mip,
                                            uint32_t dst_mip) const
    {
        if (!*this || !dst)
            return {};

        return copy_image(image_ref(*this), image_ref(dst), { src_layer, dst_layer, src_mip, dst_mip });
    }

    CopyImageToBufferCommand HardwareImage::copy_to(const HardwareBuffer& dst,
                                                    uint32_t image_layer,
                                                    uint32_t image_mip,
                                                    uint64_t buffer_offset) const
    {
        if (!*this || !dst)
            return {};

        return copy_to_buffer(image_ref(*this), buffer_ref(dst), { buffer_offset, image_layer, image_mip });
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

        return copy_to_image(buffer_ref(src), image_ref(*this), { buffer_offset, image_layer, image_mip });
    }

    uint32_t HardwareImage::store_descriptor() const
    {
        const auto image = write_image(*this);
        if (!image)
            throw std::invalid_argument("HardwareImage::store_descriptor requires a valid image.");

        return resource_manager().store_descriptor(*image);
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

        ResourceBridge::set(image, make_token<ImageStore>(std::move(resource)));
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
