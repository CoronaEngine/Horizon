#pragma once

namespace Corona::Horizon
{
    enum class Format : uint8_t
    {
        UNKNOWN,

        R8_UINT,
        R8_SINT,
        R8_UNORM,
        R8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RG8_UNORM,
        RG8_SNORM,
        R16_UINT,
        R16_SINT,
        R16_UNORM,
        R16_SNORM,
        R16_FLOAT,
        BGRA4_UNORM,
        B5G6R5_UNORM,
        B5G5R5A1_UNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_UNORM,
        RGBA8_SNORM,
        BGRA8_UNORM,
        BGRX8_UNORM,
        SRGBA8_UNORM,
        SBGRA8_UNORM,
        SBGRX8_UNORM,
        R10G10B10A2_UNORM,
        R11G11B10_FLOAT,
        RG16_UINT,
        RG16_SINT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_FLOAT,
        R32_UINT,
        R32_SINT,
        R32_FLOAT,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_FLOAT,
        RGBA16_UNORM,
        RGBA16_SNORM,
        RG32_UINT,
        RG32_SINT,
        RG32_FLOAT,
        RGB32_UINT,
        RGB32_SINT,
        RGB32_FLOAT,
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_FLOAT,

        D16,
        D24S8,
        X24G8_UINT,
        D32,
        D32S8,
        X32G8_UINT,

        BC1_UNORM,
        BC1_UNORM_SRGB,
        BC2_UNORM,
        BC2_UNORM_SRGB,
        BC3_UNORM,
        BC3_UNORM_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_UNORM_SRGB,

        COUNT,
    };

    enum class CpuAccessMode
    {
        None,
        Read,
        Write,
        ReadWrite,
    };

    enum class BufferUsageFlags : uint32_t
    {
        None = 0,
        TransferSrc = 1 << 0,
        TransferDst = 1 << 1,
        Vertex = 1 << 2,
        Index = 1 << 3,
        Uniform = 1 << 4,
        Storage = 1 << 5,
    };

    constexpr BufferUsageFlags operator|(BufferUsageFlags a, BufferUsageFlags b)
    {
        return BufferUsageFlags(uint32_t(a) | uint32_t(b));
    }

    constexpr BufferUsageFlags operator&(BufferUsageFlags a, BufferUsageFlags b)
    {
        return BufferUsageFlags(uint32_t(a) & uint32_t(b));
    }

    constexpr bool hasFlag(BufferUsageFlags flags, BufferUsageFlags bit)
    {
        return uint32_t(flags & bit) != 0;
    }

    enum class ImageDimension : uint8_t
    {
        Image1D,
        Image2D,
        Image3D,
        Cube,
        Image2DArray,
        CubeArray,
    };

    enum class ImageUsageFlags : uint32_t
    {
        None = 0,
        TransferSrc = 1 << 0,
        TransferDst = 1 << 1,
        Sampled = 1 << 2,
        Storage = 1 << 3,
        ColorAttachment = 1 << 4,
        DepthStencilAttachment = 1 << 5,
    };

constexpr ImageUsageFlags operator|(ImageUsageFlags a, ImageUsageFlags b) noexcept
    {
        return ImageUsageFlags(uint32_t(a) | uint32_t(b));
    }

    constexpr ImageUsageFlags operator&(ImageUsageFlags a, ImageUsageFlags b) noexcept
    {
        return ImageUsageFlags(uint32_t(a) & uint32_t(b));
    }

    constexpr ImageUsageFlags& operator|=(ImageUsageFlags& a, ImageUsageFlags b) noexcept
    {
        a = a | b;
        return a;
    }

    constexpr bool has_flag(ImageUsageFlags flags, ImageUsageFlags bit) noexcept
    {
        return uint32_t(flags & bit) != 0;
    }

    struct ImageExtent
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
    };

    struct ImageSubresource
    {
        uint32_t layer = 0;
        uint32_t mip = 0;

        [[nodiscard]] constexpr uint32_t index(uint32_t mip_levels) const noexcept
        {
            return layer * mip_levels + mip;
        }
    };

    struct ImageSubresourceRange
    {
        static constexpr uint32_t remaining = ~0u;

        uint32_t base_layer = 0;
        uint32_t layer_count = remaining;
        uint32_t base_mip = 0;
        uint32_t mip_count = remaining;

        [[nodiscard]] static constexpr ImageSubresourceRange whole() noexcept
        {
            return {};
        }

        [[nodiscard]] static constexpr ImageSubresourceRange single(uint32_t layer, uint32_t mip) noexcept
        {
            return {
                .base_layer = layer,
                .layer_count = 1,
                .base_mip = mip,
                .mip_count = 1,
            };
        }

        [[nodiscard]] constexpr bool is_single() const noexcept
        {
            return layer_count == 1 && mip_count == 1;
        }
    };

    struct ImageSubresourceLayout
    {
        uint64_t byte_offset = 0;
        uint64_t byte_size = 0;
        uint64_t row_pitch = 0;
        uint64_t slice_pitch = 0;
        ImageExtent extent {};
    };

    struct HardwareImageOptions
    {
        CpuAccessMode cpu_access = CpuAccessMode::None;
        bool dedicated = false;
        bool exportable = false;
    };


enum class IndexType : uint32_t
{
    Auto = 0,
    UInt16 = 1,
    UInt32 = 2,
};

struct ScissorRect
{
    int32_t x{0};
    int32_t y{0};
    uint32_t width{0};
    uint32_t height{0};
};

struct DrawIndexedParams
{
    uint32_t indexCount{0};
    uint32_t firstIndex{0};
    int32_t vertexOffset{0};
    IndexType indexType = IndexType::Auto;
    bool enableScissor{false};
    ScissorRect scissor{};
};

enum class RayTracingShaderStage : uint16_t
{
    RayGeneration = 0,
    Miss = 1,
    ClosestHit = 2,
    AnyHit = 3,
    Intersection = 4,
    Callable = 5,
};

enum class RayTracingHitGroupKind : uint16_t
{
    Triangles = 0,
    Procedural = 1,
};

enum class RayTracingGeometryKind : uint16_t
{
    Triangles = 0,
    Aabbs = 1,
};

enum class AccelerationStructureBuildMode : uint16_t
{
    Build = 0,
    Update = 1,
};



}
