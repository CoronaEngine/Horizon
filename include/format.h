#pragma once

#include <cstdint>
#include <string>

namespace Corona::Horizon
{
    // ================================================================
    // Format
    // ================================================================

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



    // ================================================================
    // Buffer
    // ================================================================

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
        // GPU-readable draw arguments for vkCmdDraw*Indirect / MultiDrawIndirect.
        Indirect = 1 << 6,
    };

    constexpr BufferUsageFlags operator|(BufferUsageFlags a, BufferUsageFlags b)
    {
        return BufferUsageFlags(uint32_t(a) | uint32_t(b));
    }

    constexpr BufferUsageFlags operator&(BufferUsageFlags a, BufferUsageFlags b)
    {
        return BufferUsageFlags(uint32_t(a) & uint32_t(b));
    }

    constexpr BufferUsageFlags &operator|=(BufferUsageFlags& a, BufferUsageFlags b) noexcept
    {
        a = a | b;
        return a;
    }

    constexpr bool has_flag(BufferUsageFlags flags, BufferUsageFlags bit) noexcept
    {
        return uint32_t(flags & bit) != 0;
    }

    struct BufferRange
    {
        static constexpr uint64_t whole_size = ~uint64_t{0};

        uint64_t byte_offset = 0;
        uint64_t byte_size = whole_size;

        static constexpr BufferRange entire() noexcept
        {
            return {};
        }

        [[nodiscard]] constexpr BufferRange resolve(uint64_t total_size) const noexcept
        {
            BufferRange result = *this;

            if (result.byte_size == whole_size)
                result.byte_size = result.byte_offset <= total_size ? total_size - result.byte_offset : 0;

            return result;
        }
    };



    // ================================================================
    // Native Interop
    // ================================================================

    enum class ExternalMemoryHandleType : uint8_t
    {
        None,
        OpaqueFd,
        OpaqueWin32,
    };

    struct ExternalMemoryHandle
    {
#if defined(_WIN32) || defined(_WIN64)
        static constexpr ExternalMemoryHandleType platform_type = ExternalMemoryHandleType::OpaqueWin32;
#else
        static constexpr ExternalMemoryHandleType platform_type = ExternalMemoryHandleType::OpaqueFd;
#endif

        ExternalMemoryHandleType type = ExternalMemoryHandleType::None;

        void* handle = nullptr;
        int fd = -1;

        uint64_t allocation_size = 0;
        BufferRange memory_range = BufferRange::entire();

        [[nodiscard]] static constexpr ExternalMemoryHandle win32(void *value, uint64_t size = 0, BufferRange range = BufferRange::entire()) noexcept
        {
            return {ExternalMemoryHandleType::OpaqueWin32, value, -1, size, range};
        }

        [[nodiscard]] static constexpr ExternalMemoryHandle opaque_fd(int value, uint64_t size = 0, BufferRange range = BufferRange::entire()) noexcept
        {
            return {ExternalMemoryHandleType::OpaqueFd, nullptr, value, size, range};
        }

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return type == ExternalMemoryHandleType::OpaqueWin32 ? handle != nullptr : type == ExternalMemoryHandleType::OpaqueFd ? fd >= 0 : false;
        }

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return valid();
        }
    };



    // ================================================================
    // Image
    // ================================================================

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
            return 
            {
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

    // ================================================================
    // Draw
    // ================================================================

    enum class IndexType : uint32_t
    {
        Auto = 0,
        UInt16 = 1,
        UInt32 = 2,
    };

    struct ScissorRect
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct DrawIndexedParams
    {
        uint32_t index_count = 0;
        uint32_t instance_count = 1;
        uint32_t first_index = 0;
        int32_t vertex_offset = 0;
        uint32_t first_instance = 0;
        IndexType index_type = IndexType::Auto;
        bool enable_scissor = false;
        ScissorRect scissor{};
        std::string debug_label;
    };

    // Matches VkDrawIndexedIndirectCommand (20 bytes, tightly packed).
    struct DrawIndexedIndirectCommand
    {
        uint32_t index_count = 0;
        uint32_t instance_count = 0;
        uint32_t first_index = 0;
        int32_t vertex_offset = 0;
        uint32_t first_instance = 0;
    };
    static_assert(sizeof(DrawIndexedIndirectCommand) == 20, "DrawIndexedIndirectCommand must match Vulkan layout");

    struct DrawIndexedIndirectParams
    {
        uint32_t draw_count = 0;
        uint64_t indirect_offset = 0;
        // 0 means sizeof(DrawIndexedIndirectCommand).
        uint32_t stride = 0;
        IndexType index_type = IndexType::Auto;
        bool enable_scissor = false;
        ScissorRect scissor{};
        std::string debug_label;
    };

    // ================================================================
    // Pipeline Enums
    // ================================================================

    enum class PipelineShaderStage : uint16_t
    {
        Compute = 0,
        Vertex = 1,
        Fragment = 2,
        RayGeneration = 3,
        Miss = 4,
        ClosestHit = 5,
        AnyHit = 6,
        Intersection = 7,
        Callable = 8,
    };

    enum class PrimitiveTopology : uint16_t
    {
        TriangleList = 0,
        TriangleStrip,
        LineList,
        LineStrip,
        PointList,
    };

    enum class PolygonFillMode : uint16_t
    {
        Fill = 0,
        Line,
        Point,
    };

    enum class CullMode : uint16_t
    {
        None = 0,
        Front,
        Back,
        FrontAndBack,
    };

    enum class CompareOp : uint16_t
    {
        Never = 0,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    enum class StencilOp : uint16_t
    {
        Keep = 0,
        Zero,
        Replace,
        IncrementAndClamp,
        DecrementAndClamp,
        Invert,
        IncrementAndWrap,
        DecrementAndWrap,
    };

    enum class BlendFactor : uint16_t
    {
        Zero = 0,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
    };

    enum class BlendOp : uint16_t
    {
        Add = 0,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    enum class SampleCount : uint16_t
    {
        Count1 = 1,
        Count2 = 2,
        Count4 = 4,
        Count8 = 8,
        Count16 = 16,
    };

    enum class ColorWriteMask : uint8_t
    {
        None = 0,
        R = 1 << 0,
        G = 1 << 1,
        B = 1 << 2,
        A = 1 << 3,
        RGB = R | G | B,
        RGBA = R | G | B | A,
    };

    constexpr ColorWriteMask operator|(ColorWriteMask a, ColorWriteMask b) noexcept
    {
        return ColorWriteMask(uint8_t(a) | uint8_t(b));
    }

    constexpr ColorWriteMask operator&(ColorWriteMask a, ColorWriteMask b) noexcept
    {
        return ColorWriteMask(uint8_t(a) & uint8_t(b));
    }

    constexpr ColorWriteMask &operator|=(ColorWriteMask& a, ColorWriteMask b) noexcept
    {
        a = a | b;
        return a;
    }



    // ================================================================
    // Pipeline State
    // ================================================================

    struct RasterizerStateDesc
    {
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        PolygonFillMode fill_mode = PolygonFillMode::Fill;
        CullMode cull_mode = CullMode::None;

        bool depth_clamp_enabled = false;
        bool rasterizer_discard_enabled = false;

        float line_width = 1.0f;
    };

    struct DepthStencilOpDesc
    {
        StencilOp fail_op = StencilOp::Keep;
        StencilOp pass_op = StencilOp::Keep;
        StencilOp depth_fail_op = StencilOp::Keep;
        CompareOp compare_op = CompareOp::Always;
    };

    struct DepthStencilStateDesc
    {
        bool depth_test_enabled = true;
        bool depth_write_enabled = true;
        CompareOp depth_compare_op = CompareOp::LessOrEqual;

        bool stencil_test_enabled = false;
        DepthStencilOpDesc front;
        DepthStencilOpDesc back;
        uint32_t stencil_read_mask = 0xff;
        uint32_t stencil_write_mask = 0xff;
        uint32_t stencil_reference = 0;
    };

    struct BlendAttachmentDesc
    {
        bool blend_enabled = false;

        BlendFactor src_color_blend_factor = BlendFactor::SrcAlpha;
        BlendFactor dst_color_blend_factor = BlendFactor::OneMinusSrcAlpha;
        BlendOp color_blend_op = BlendOp::Add;

        BlendFactor src_alpha_blend_factor = BlendFactor::One;
        BlendFactor dst_alpha_blend_factor = BlendFactor::OneMinusSrcAlpha;
        BlendOp alpha_blend_op = BlendOp::Add;

        ColorWriteMask color_write_mask = ColorWriteMask::RGBA;
    };

    struct MultisampleStateDesc
    {
        SampleCount sample_count = SampleCount::Count1;
        bool sample_shading_enabled = false;
        float min_sample_shading = 1.0f;
    };



    // ================================================================
    // Ray Tracing
    // ================================================================

    // ================================================================
    // Validation
    // ================================================================

    enum class HardwareValidationMode : uint8_t
    {
        Disabled,
        Log,
        Throw,
    };

}
