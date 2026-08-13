#pragma once

#include <horizon.h>  // Format / ImageExtent (format.h merged into horizon.h)

#include <cstdint>
#include <limits>

namespace Corona::Horizon::detail
{
    struct FormatBlockLayout
    {
        uint32_t block_width { 1 };
        uint32_t block_height { 1 };
        uint32_t block_depth { 1 };
        uint32_t bytes_per_block { 0 };
    };

    [[nodiscard]] inline FormatBlockLayout format_block_layout(Format format) noexcept
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
            return { 1, 1, 1, 4 };
        case Format::RGBA16_UINT:
        case Format::RGBA16_SINT:
        case Format::RGBA16_FLOAT:
        case Format::RGBA16_UNORM:
        case Format::RGBA16_SNORM:
        case Format::RG32_UINT:
        case Format::RG32_SINT:
        case Format::RG32_FLOAT:
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
        case Format::D16:
        case Format::D24S8:
        case Format::X24G8_UINT:
        case Format::D32:
        case Format::D32S8:
        case Format::X32G8_UINT:
        case Format::UNKNOWN:
        case Format::COUNT:
            break;
        }

        return {};
    }

    [[nodiscard]] inline uint32_t div_ceil(uint32_t value, uint32_t divisor) noexcept
    {
        return divisor == 0 ? 0 : value / divisor + (value % divisor != 0 ? 1u : 0u);
    }

    [[nodiscard]] inline uint32_t mip_dimension(uint32_t value, uint32_t mip) noexcept
    {
        while (mip > 0 && value > 1)
        {
            value >>= 1;
            --mip;
        }

        return value == 0 ? 1 : value;
    }

    [[nodiscard]] inline ImageExtent mip_extent(ImageExtent extent, uint32_t mip) noexcept
    {
        return {
            mip_dimension(extent.width, mip),
            mip_dimension(extent.height, mip),
            mip_dimension(extent.depth, mip),
        };
    }

    [[nodiscard]] inline uint32_t max_mip_levels(ImageExtent extent) noexcept
    {
        uint32_t largest = extent.width;
        if (largest < extent.height)
            largest = extent.height;
        if (largest < extent.depth)
            largest = extent.depth;

        uint32_t levels = 1;
        while (largest > 1)
        {
            largest >>= 1;
            ++levels;
        }

        return levels;
    }

    [[nodiscard]] inline bool checked_add(uint64_t a, uint64_t b, uint64_t& out) noexcept
    {
        if (a > std::numeric_limits<uint64_t>::max() - b)
            return false;

        out = a + b;
        return true;
    }

    [[nodiscard]] inline bool checked_mul(uint64_t a, uint64_t b, uint64_t& out) noexcept
    {
        if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a)
            return false;

        out = a * b;
        return true;
    }

    [[nodiscard]] inline bool strided_byte_size(uint32_t slice_count,
                                                uint32_t row_count,
                                                uint64_t slice_pitch,
                                                uint64_t row_pitch,
                                                uint64_t row_bytes,
                                                uint64_t& out) noexcept
    {
        if (slice_count == 0 || row_count == 0)
        {
            out = 0;
            return true;
        }

        uint64_t last_slice_offset = 0;
        uint64_t last_row_offset = 0;
        uint64_t required = 0;
        if (!checked_mul(slice_pitch, slice_count - 1u, last_slice_offset) ||
            !checked_mul(row_pitch, row_count - 1u, last_row_offset) ||
            !checked_add(last_slice_offset, last_row_offset, required) ||
            !checked_add(required, row_bytes, out))
        {
            return false;
        }

        return true;
    }
}
