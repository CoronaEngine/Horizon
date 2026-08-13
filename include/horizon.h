#pragma once

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <ranges>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <ktm/ktm.h>

#include "Codegen/ComputePipelineObject.h"
#include "Codegen/RasterizedPipelineObject.h"
#include "Codegen/VariateProxy.h"
#include "Compiler/ShaderCodeCompiler.h"
#include "Compiler/ShaderLanguageConverter.h"

#ifndef HORIZON_ENABLE_HARDWARE_VALIDATION
#if defined(NDEBUG)
#define HORIZON_ENABLE_HARDWARE_VALIDATION 0
#else
#define HORIZON_ENABLE_HARDWARE_VALIDATION 1
#endif
#endif

namespace Corona::Horizon
{
    // ================================================================
    // [merged from resource.h]  Resource handle bridge
    // ================================================================

    struct ResourceBridge;

    struct IResourceRef
    {
        virtual ~IResourceRef() = default;

        [[nodiscard]] virtual std::uintptr_t id() const noexcept = 0;
        [[nodiscard]] virtual bool valid() const noexcept = 0;
    };

    class ResourceHandle
    {
    public:
        ResourceHandle() noexcept = default;
        ResourceHandle(const ResourceHandle&) noexcept = default;
        ResourceHandle(ResourceHandle&&) noexcept = default;
        ResourceHandle& operator=(const ResourceHandle&) noexcept = default;
        ResourceHandle& operator=(ResourceHandle&&) noexcept = default;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            // Avoid shared_ptr copy to prevent refcount operation
            return resource_ != nullptr && resource_->valid();
        }

    protected:
        [[nodiscard]] std::uintptr_t resource_id() const noexcept
        {
            return resource_ ? resource_->id() : 0;
        }

    private:
        friend struct ResourceBridge;

        std::shared_ptr<IResourceRef> resource_{};
    };

    struct ResourceBridge
    {
        static void set(ResourceHandle& owner, std::shared_ptr<IResourceRef> resource) noexcept
        {
            owner.resource_ = std::move(resource);
        }

        [[nodiscard]] static std::shared_ptr<IResourceRef> token(const ResourceHandle& owner) noexcept
        {
            return owner.resource_;
        }

        [[nodiscard]] static std::shared_ptr<const IResourceRef> keep_alive(const ResourceHandle& owner) noexcept
        {
            return owner.resource_;
        }
    };


    // ================================================================
    // [merged from format.h]  Format / buffer / image / draw / state enums
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
    // Forward Declarations
    // ================================================================

    class HardwareBuffer;
    class HardwareImage;

    struct CopyBufferToImageCommand;

    class CommandBatch;

    class ComputePipelineBase;
    class RasterizerPipelineBase;
    template <typename CS = void>
    class ComputePipeline;
    template <typename VS = void, typename FS = void>
    class RasterizerPipeline;

    class HardwareExecutor;
    class HardwareDisplayer;

    // ================================================================
    // Device memory query (VRAM capacity)
    // ================================================================

    struct BindingSlot;

    // ================================================================
    // Execution / Command Public Types
    // ================================================================
    //
    // 执行 / 命令 IR 层（QueueCapability、DeviceMask、CommandIR、
    // RecordedTask、SubmissionToken、SubmitReceipt、CommandRecorder 等）。
    // 原 horizon_execution.h 已并入本文件（内部 / 高级 API）。普通用户
    // 无需直接接触这些类型；下方的资源 / 管线 / 命令门面会在内部使用它们。
    // ================================================================

    class RasterizerPipelineBase;
    class ComputePipelineBase;

    class Queue;
    class CommandRecorder;
    class ExecutionCompiler;
    struct ExecutionPlan;
    class SubmissionSync;
    enum class QueueCapability;
    enum class AccessKind;
    enum class CommandOp;
    enum class FeatureRequirement;
    enum class DispatchBindingKind;
    enum class PresentStatus;
    struct QueueId;
    struct CopyRegion;
    struct ImageCopyRegion;
    struct ResourceUse;
    struct DispatchResourceBinding;
    struct UniformBufferBindingData;
    struct DispatchDesc;
    struct RenderingDesc;
    struct DrawIndexedDesc;
    struct DrawIndexedBatchItem;
    struct DrawIndexedBatchDesc;
    struct DrawIndexedIndirectDesc;
    struct PresentDesc;
    struct CommandPayload;
    struct CommandIR;
    struct RequirementSet;
    struct RecordedTask;
    struct ResourceBarrier;
    struct PresentResult;
    struct CrossDeviceDependency;
    struct SubmissionDependency;
    class SubmissionKeepAlive;
    struct SubmissionToken;
    struct QueueSubmission;

    struct DeviceId
    {
        uint32_t value { 0 };

        [[nodiscard]] friend bool operator==(DeviceId left, DeviceId right) noexcept
        {
            return left.value == right.value;
        }
    };

    struct DeviceMask
    {
        uint32_t bits { 1 };
    };

    struct BufferRef
    {
        ResourceHandle handle {};
    };

    struct ImageRef
    {
        ResourceHandle handle {};
    };

    struct DisplayerRef
    {
        std::uintptr_t id { 0 };
    };

    struct BufferImageCopyRegion
    {
        uint64_t buffer_offset { 0 };
        uint32_t image_layer { 0 };
        uint32_t image_mip { 0 };
    };

    // SubmitReceipt 的完整载荷（SubmissionToken / PresentResult 序列）由内部
    // SubmitReceiptData 持有，经 shared_ptr<const void> 不透明传递。公共用户只
    // 能读 serial、并把它当作 wait / wait_idle 的参数，不触达内部 token。
    struct SubmitReceipt
    {
        uint64_t serial { 0 };
        std::shared_ptr<const void> data {};
    };

    struct CommitCommand
    {
    };

    [[nodiscard]] CommitCommand commit() noexcept;

    class StreamCommand
    {
    public:
        StreamCommand() = default;
        explicit StreamCommand(std::function<void(CommandRecorder&)> recorder);

        // 任何提供 record(CommandRecorder&) const 的值命令都可隐式转成 StreamCommand。
        // 这是唯一的折叠点：此前 horizon.h 里 9 个值命令各自手抄一遍
        // stream_command() + operator StreamCommand()，做的都是这同一件事。
        //
        // 排除 StreamCommand 自身是必须的：它自己也有 record(CommandRecorder&) const，
        // 不排除则该模板会与拷贝/移动构造函数竞争。
        template <typename Command>
            requires(!std::is_same_v<std::remove_cvref_t<Command>, StreamCommand> &&
                     requires(const std::remove_cvref_t<Command>& command, CommandRecorder& recorder) {
                         command.record(recorder);
                     })
        StreamCommand(Command&& command)
            : recorder_([command = std::remove_cvref_t<Command>(std::forward<Command>(command))](
                            CommandRecorder& recorder) { command.record(recorder); })
        {
        }

        void record(CommandRecorder& recorder) const;
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(recorder_); }

    private:
        std::function<void(CommandRecorder&)> recorder_ {};
    };

    class CommandBatch
    {
    public:
        CommandBatch& operator<<(StreamCommand command);
        [[nodiscard]] const std::vector<StreamCommand>& commands() const noexcept { return commands_; }

    private:
        std::vector<StreamCommand> commands_;
    };


    class HardwareDisplayer
    {
    public:
        HardwareDisplayer();
        explicit HardwareDisplayer(void* native_window);
        HardwareDisplayer(const HardwareDisplayer& other) noexcept = default;
        HardwareDisplayer(HardwareDisplayer&& other) noexcept = default;
        ~HardwareDisplayer();

        HardwareDisplayer& operator=(const HardwareDisplayer& other) noexcept = default;
        HardwareDisplayer& operator=(HardwareDisplayer&& other) noexcept = default;

        [[nodiscard]] explicit operator bool() const noexcept { return displayer_.id != 0 && manager_ != nullptr; }
        [[nodiscard]] DisplayerRef displayer_ref() const noexcept { return displayer_; }

    private:
        DisplayerRef displayer_ {};
        std::shared_ptr<void> manager_ {};
    };

    class HardwareStream
    {
    public:
        explicit HardwareStream(HardwareExecutor& executor);

        HardwareStream(const HardwareStream&) = delete;
        HardwareStream& operator=(const HardwareStream&) = delete;
        HardwareStream(HardwareStream&&) noexcept = default;
        HardwareStream& operator=(HardwareStream&&) noexcept = default;

        ~HardwareStream();

        HardwareStream& operator<<(const StreamCommand& command);
        HardwareStream& operator<<(const CommandBatch& commands);
        // 便捷门面：pipeline 可直接流入，内部等价于 `<< pipeline.command_batch()`。
        // 高级/测试路径仍可显式写 `<< pipeline(...).command_batch()`。
        HardwareStream& operator<<(ComputePipelineBase& pipeline);
        HardwareStream& operator<<(RasterizerPipelineBase& pipeline);
        [[nodiscard]] SubmitReceipt operator<<(CommitCommand command);

    private:
        void ensure_open() const;

        HardwareExecutor* executor_ {};
        // CommandRecorder 定义在内部 command_ir.h；公共头只反声明，故按值成员改为
        // 独占指针，实例化与 reset 都发生在硬件层。
        std::unique_ptr<CommandRecorder> recorder_ {};
        bool committed_ { false };
    };

    class HardwareExecutor
    {
    public:
        using QueueResolver = std::function<Queue&(DeviceId device, QueueCapability capability)>;

        HardwareExecutor();
        explicit HardwareExecutor(QueueResolver queue_resolver);

        HardwareExecutor(const HardwareExecutor&) = delete;
        HardwareExecutor& operator=(const HardwareExecutor&) = delete;
        HardwareExecutor(HardwareExecutor&&) = delete;
        HardwareExecutor& operator=(HardwareExecutor&&) = delete;

        [[nodiscard]] HardwareStream stream();
        // 便捷门面：`executor << pipeline` 直接开流，免去显式 `.stream()`。
        [[nodiscard]] HardwareStream operator<<(RasterizerPipelineBase& pipeline);
        [[nodiscard]] SubmitReceipt commit(RecordedTask task);
        HardwareExecutor& wait(const SubmitReceipt& receipt);
        HardwareExecutor& wait_idle(const SubmitReceipt& receipt);

    private:
        [[nodiscard]] std::vector<SubmissionToken> submit(ExecutionPlan& plan,
                                                          std::vector<PresentResult>* present_results,
                                                          std::span<const SubmissionToken> wait_tokens,
                                                          struct ExecutionCommitProfileSample* profile = nullptr) const;
        [[nodiscard]] std::vector<std::shared_ptr<SubmissionToken>> consume_pending_waits();

        std::shared_ptr<ExecutionCompiler> compiler_;
        QueueResolver queue_resolver_ {};
        mutable std::mutex mutex_;
        uint64_t next_submit_serial_ { 0 };
        // SubmissionToken 定义在内部 command_ir.h，公共头不可按值持有，故用 shared_ptr。
        std::vector<std::shared_ptr<SubmissionToken>> pending_waits_;
    };

    // 隐式提交标记：`stream << pipeline << H::submit` 等价于 `<< H::commit()`。
    // 复用既有的 `operator<<(CommitCommand)`，不引入新算子。
    inline constexpr CommitCommand submit {};



    // ================================================================
    // HardwareBuffer
    // ================================================================

    // Hardware data must be plain value data; do not store CPU pointers inside transferred structs.
    template <typename T>
    concept HardwareTransferable = std::is_trivially_copyable_v<std::remove_cvref_t<T>> && !std::is_pointer_v<std::remove_cvref_t<T>>;

    // index limited to uint16_t/uint32_t.
    template <typename T>
    concept HardwareIndexType = std::same_as<std::remove_cvref_t<T>, uint16_t> || std::same_as<std::remove_cvref_t<T>, uint32_t>;

    struct HardwareBufferDesc
    {
        uint64_t element_count = 0;
        uint32_t element_size = 0;
        BufferUsageFlags usage = BufferUsageFlags::None;
        CpuAccessMode cpu_access = CpuAccessMode::Write;
        bool dedicated = false;
        bool exportable = false;
        std::string debug_name;

        [[nodiscard]] uint64_t byte_size() const
        {
            if (element_count == 0 || element_size == 0)
                return 0;

            if (element_count > std::numeric_limits<uint64_t>::max() / element_size)
                throw std::overflow_error("HardwareBufferDesc total byte size overflow.");

            return element_count * uint64_t(element_size);
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBufferDesc typed(uint64_t count, BufferUsageFlags usage, std::string name = {})
        {
            HardwareBufferDesc desc;
            desc.element_count = count;
            desc.element_size = uint32_t(sizeof(T));
            desc.usage = usage;
            desc.debug_name = std::move(name);
            (void)desc.byte_size();
            return desc;
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBufferDesc vertex(uint64_t count, std::string name = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferDst | BufferUsageFlags::Vertex, std::move(name));
        }

        template <HardwareIndexType T>
        [[nodiscard]] static HardwareBufferDesc index(uint64_t count, std::string name = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferDst | BufferUsageFlags::Index, std::move(name));
        }

        [[nodiscard]] static HardwareBufferDesc indirect(uint64_t command_count, std::string name = {})
        {
            return typed<DrawIndexedIndirectCommand>(
                command_count,
                BufferUsageFlags::TransferDst | BufferUsageFlags::Indirect | BufferUsageFlags::Storage,
                std::move(name));
        }
    };

    class HardwareBuffer : public ResourceHandle
    {
    public:
        HardwareBuffer() = default;
        HardwareBuffer(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data = {});

        // Copies share the same underlying GPU buffer handle.
        HardwareBuffer(const HardwareBuffer& other) noexcept = default;
        HardwareBuffer(HardwareBuffer&& other) noexcept = default;
        ~HardwareBuffer() = default;

        HardwareBuffer& operator=(const HardwareBuffer& other) noexcept = default;
        HardwareBuffer& operator=(HardwareBuffer&& other) noexcept = default;
        [[nodiscard]] explicit operator bool() const noexcept { return ResourceHandle::operator bool(); }

        [[nodiscard]] uint64_t get_element_size() const;
        [[nodiscard]] uint64_t get_element_count() const;
        [[nodiscard]] uint64_t get_byte_size() const
        {
            const uint64_t element_count = get_element_count();
            const uint64_t element_size = get_element_size();
            if (element_size != 0 && element_count > std::numeric_limits<uint64_t>::max() / element_size)
                throw std::overflow_error("HardwareBuffer total byte size overflow.");

            return element_count * element_size;
        }
        [[nodiscard]] void* get_mapped_data() const;

        [[nodiscard]] bool write_bytes(std::span<const std::byte> data, uint64_t byte_offset = 0) const;

        [[nodiscard]] static HardwareBuffer from_bytes(std::span<const std::byte> data, uint32_t element_size, BufferUsageFlags usage, std::string name = {});

        template <std::ranges::contiguous_range Range>
            requires std::ranges::sized_range<Range> && HardwareTransferable<std::ranges::range_value_t<Range>>
        [[nodiscard]] static HardwareBuffer vertex(const Range& data, std::string name = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return HardwareBuffer(HardwareBufferDesc::vertex<T>(data.size(), std::move(name)),
                                  std::as_bytes(std::span<const T>(std::ranges::data(data), std::ranges::size(data))));
        }

        template <std::ranges::contiguous_range Range>
            requires std::ranges::sized_range<Range> && HardwareIndexType<std::ranges::range_value_t<Range>>
        [[nodiscard]] static HardwareBuffer index(const Range& data, std::string name = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return HardwareBuffer(HardwareBufferDesc::index<T>(data.size(), std::move(name)),
                                  std::as_bytes(std::span<const T>(std::ranges::data(data), std::ranges::size(data))));
        }

        template <std::ranges::contiguous_range Range>
            requires std::ranges::sized_range<Range> &&
                     std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, DrawIndexedIndirectCommand>
        [[nodiscard]] static HardwareBuffer indirect(const Range& commands, std::string name = {})
        {
            return HardwareBuffer(HardwareBufferDesc::indirect(commands.size(), std::move(name)),
                                  std::as_bytes(std::span<const DrawIndexedIndirectCommand>(std::ranges::data(commands), std::ranges::size(commands))));
        }

        [[nodiscard]] uint32_t store_descriptor() const;
        [[nodiscard]] static HardwareBuffer import_external(const ExternalMemoryHandle& handle, const HardwareBufferDesc& desc);
        [[nodiscard]] ExternalMemoryHandle export_external() const;

    private:
        friend class HardwareImage;
    };



    // ================================================================
    // HardwareImage
    // ================================================================

    struct HardwareImageDesc
    {
        ImageDimension dimension = ImageDimension::Image2D;
        ImageExtent extent {};
        Format format = Format::UNKNOWN;
        ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst;
        CpuAccessMode cpu_access = CpuAccessMode::None;
        uint32_t array_layers = 1;
        uint32_t mip_levels = 1;
        uint32_t sample_count = 1;
        bool dedicated = false;
        bool exportable = false;
        std::string debug_name;

        static HardwareImageDesc texture_2d(uint32_t width,
                                            uint32_t height,
                                            Format format,
                                            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                            std::string name = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Image2D;
            desc.extent = { width, height, 1 };
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc;
        }

        static HardwareImageDesc texture_2d_array(uint32_t width,
                                                  uint32_t height,
                                                  uint32_t layers,
                                                  Format format,
                                                  ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                                  std::string name = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Image2DArray;
            desc.extent = { width, height, 1 };
            desc.array_layers = layers;
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc;
        }

        static HardwareImageDesc cube(uint32_t size,
                                      Format format,
                                      ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                      std::string name = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Cube;
            desc.extent = { size, size, 1 };
            desc.array_layers = 6;
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc;
        }

        static HardwareImageDesc depth_attachment(uint32_t width,
                                                  uint32_t height,
                                                  Format format,
                                                  std::string name = {})
        {
            return texture_2d(width,
                              height,
                              format,
                              ImageUsageFlags::DepthStencilAttachment | ImageUsageFlags::Sampled | ImageUsageFlags::TransferSrc | ImageUsageFlags::TransferDst,
                              std::move(name));
        }
    };

    class HardwareImage : public ResourceHandle
    {
    public:
        HardwareImage() = default;
        HardwareImage(const HardwareImageDesc& desc, std::span<const std::byte> upload_data = {});

        HardwareImage(const HardwareImage& other) noexcept = default;
        HardwareImage(HardwareImage&& other) noexcept = default;
        ~HardwareImage() = default;

        HardwareImage& operator=(const HardwareImage& other) noexcept = default;
        HardwareImage& operator=(HardwareImage&& other) noexcept = default;

        [[nodiscard]] explicit operator bool() const noexcept { return ResourceHandle::operator bool(); }
        [[nodiscard]] HardwareImage subresource(uint32_t layer_index, uint32_t mip_index) const;
        bool write_bytes(std::span<const std::byte> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;

        void set_clear_color(float r, float g, float b, float a);
        void set_clear_depth(float depth, uint32_t stencil = 0);

        [[nodiscard]] CopyBufferToImageCommand copy_from(const HardwareBuffer& src, uint64_t buffer_offset = 0, uint32_t image_layer = 0, uint32_t image_mip = 0) const;
        [[nodiscard]] uint32_t store_descriptor() const;
        static HardwareImage import_external(const ExternalMemoryHandle& handle, const HardwareImageDesc& desc, uint64_t allocation_size = 0);
        [[nodiscard]] ExternalMemoryHandle export_external() const;

    private:
        ImageSubresourceRange range_ = ImageSubresourceRange::whole();

        friend class HardwareBuffer;
    };



    // ================================================================
    // Pipeline Descriptors
    // ================================================================

    struct PipelineShaderDesc
    {
        PipelineShaderStage stage;
        EmbeddedShader::ShaderCodeModule module;

        PipelineShaderDesc(PipelineShaderStage stage, EmbeddedShader::ShaderCodeModule module) : stage(stage), module(std::move(module)) {}
    };

    struct EdslPipelineOptions
    {
        EmbeddedShader::CompilerOption compiler;
        bool auto_bind = true;
    };

    struct ComputePipelineDesc
    {
        PipelineShaderDesc compute_shader;
        std::shared_ptr<EmbeddedShader::ComputePipelineObject> pipelineObject;
        ktm::uvec3 thread_group_size = { 1, 1, 1 };
        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries;
        std::string debug_name;

        ComputePipelineDesc(PipelineShaderDesc shader, ktm::uvec3 numthreads = { 1, 1, 1 }) : compute_shader(std::move(shader)), thread_group_size(numthreads)
        {
            if (compute_shader.stage != PipelineShaderStage::Compute)
                throw std::invalid_argument("ComputePipelineDesc requires a compute shader.");
        }

        // 解析真正生效的 workgroup local size。
        // 优先取反射(entryPointInfoPool 里 compute entry 的 numthreads)——这是编译进 shader 的
        // 真实值(源码路径下也正确);反射缺失(旧 hardcode 表未序列化该字段)时回退到 thread_group_size
        // 覆盖值(EDSL 由构造参数喂入,与 codegen 一致)。
        [[nodiscard]] ktm::uvec3 resolved_thread_group_size() const
        {
            for (const auto& entry : compute_shader.module.shaderResources.entryPointInfoPool)
            {
                if (entry.stage != EmbeddedShader::ShaderStage::ComputeShader)
                    continue;
                if (entry.numthreads.x != 0 && entry.numthreads.y != 0 && entry.numthreads.z != 0)
                    return entry.numthreads;
            }
            return thread_group_size;
        }
    };

    struct BlendStateDesc
    {
        static constexpr BlendAttachmentDesc opaque_attachment() noexcept
        {
            BlendAttachmentDesc desc;
            desc.blend_enabled = false;
            return desc;
        }

        static constexpr BlendAttachmentDesc alpha_blend_attachment() noexcept
        {
            BlendAttachmentDesc desc;
            desc.blend_enabled = true;
            return desc;
        }

        bool logic_op_enabled = false;
        std::vector<BlendAttachmentDesc> attachments = { alpha_blend_attachment() };
    };

    // 前置声明：真正的定义在下方 RasterizerPipelineDesc 之后，但 set_shaders_from_slang
    // 的内联成员函数体会先于它被编译，必须先看到签名。
    [[nodiscard]] inline PipelineShaderDesc compile_slang_stage(
        PipelineShaderStage stage,
        EmbeddedShader::SlangModule& module,
        EmbeddedShader::CompilerOption compiler_option = {});

    struct RasterizerPipelineDesc
    {
        PipelineShaderDesc vertex_shader { PipelineShaderStage::Vertex, EmbeddedShader::ShaderCodeModule {} };
        PipelineShaderDesc fragment_shader { PipelineShaderStage::Fragment, EmbeddedShader::ShaderCodeModule {} };
        std::shared_ptr<EmbeddedShader::RasterizedPipelineObject> pipelineObject;

        RasterizerStateDesc rasterizer;
        DepthStencilStateDesc depth_stencil;
        BlendStateDesc blend;
        MultisampleStateDesc multisample;

        uint32_t multiview_count = 1;

        bool clear_color_target = true;
        // When a depth target is bound, clear it at pass begin unless the caller opts out
        // (needed for landscape-then-sky with depth Equal on the sky pass).
        bool clear_depth_target = true;

        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries;
        std::string debug_name;

        RasterizerPipelineDesc() = default;

        RasterizerPipelineDesc(PipelineShaderDesc vertex, PipelineShaderDesc fragment)
        {
            if (vertex.stage != PipelineShaderStage::Vertex)
                throw std::invalid_argument("RasterizerPipelineDesc requires a vertex shader.");

            if (fragment.stage != PipelineShaderStage::Fragment)
                throw std::invalid_argument("RasterizerPipelineDesc requires a fragment shader.");

            vertex_shader = std::move(vertex);
            fragment_shader = std::move(fragment);
        }
    };

    // ================================================================
    // Slang 编译
    // ================================================================

    // 编译单个 Slang shader module 为 PipelineShaderDesc。EDSL 路径（栅格 + compute）
    // 共用：填 SlangCompileArgs2 → slangCompilerWithModules → 取 SPIR-V → 反射短名裁剪。
    // 集中成一处，避免两份手抄在改约定（短名裁剪）时静默漂移。
    [[nodiscard]] inline PipelineShaderDesc compile_slang_stage(
        PipelineShaderStage stage,
        EmbeddedShader::SlangModule& module,
        EmbeddedShader::CompilerOption compiler_option)
    {
        EmbeddedShader::SlangCompileArgs2 args;
        args.sourceLanguage = EmbeddedShader::ShaderLanguage::Slang;
        args.targetLanguages = { EmbeddedShader::ShaderLanguage::SpirV };
        args.stage = [&] {
            switch (stage)
            {
            case PipelineShaderStage::Vertex:
                return EmbeddedShader::ShaderStage::VertexShader;
            case PipelineShaderStage::Fragment:
                return EmbeddedShader::ShaderStage::FragmentShader;
            case PipelineShaderStage::Compute:
                return EmbeddedShader::ShaderStage::ComputeShader;
            default:
                throw std::invalid_argument("compile_slang_stage only supports vertex, fragment, and compute stages.");
            }
        }();
        args.module = &module;
        args.deps = std::move(compiler_option.slangModules);
        args.enableReflection = true;

        EmbeddedShader::SlangCompileResult result =
            EmbeddedShader::ShaderLanguageConverter::slangCompilerWithModules(args);
        auto spirv = result.binaryTargets.find(EmbeddedShader::ShaderLanguage::SpirV);
        if (spirv == result.binaryTargets.end())
            throw std::runtime_error("Slang module compilation did not produce SPIR-V.");

        auto reflection = result.reflections.find(EmbeddedShader::ShaderLanguage::SpirV);
        EmbeddedShader::ShaderCodeModule::ShaderResources resources;
        if (reflection != result.reflections.end())
            resources = std::move(reflection->second);

        // Slang 反射产出点分全名（如 global_ubo.field），下游 codegen/runtime 按短名查找，
        // 这里裁剪成 '.' 后的短段，与离线 codegen (tools/main.cpp) 的约定保持一致。
        for (auto& info : resources.bindInfoPool)
        {
            if (auto pos = info.variateName.find_last_of('.'); pos != std::string::npos)
                info.variateName = info.variateName.substr(pos + 1);
        }

        return PipelineShaderDesc(
            stage, EmbeddedShader::ShaderCodeModule(std::move(spirv->second), std::move(resources)));
    }

    // ================================================================
    // Pipeline Binding
    // ================================================================

    template <typename T>
    concept ReflectedBindingKey = requires(const T& t)
    {
        t.location;
    } && (requires(const T& t)
    {
        t.byte_offset;
        t.type_size;
        t.bind_type;
    } || requires(const T& t)
    {
        t.byteOffset;
        t.typeSize;
        t.bindType;
    });

    struct BindingSlot
    {
        uint64_t byte_offset = 0;
        uint32_t type_size = 0;
        int32_t bind_type = -1;
        uint32_t location = 0;
        uint32_t set = 0;
        uint32_t binding = 0;

        template <ReflectedBindingKey T>
        static constexpr BindingSlot from(const T& key) noexcept
        {
            BindingSlot slot;
            if constexpr (requires { key.byte_offset; key.type_size; key.bind_type; })
            {
                slot.byte_offset = key.byte_offset;
                slot.type_size = key.type_size;
                slot.bind_type = key.bind_type;
                slot.location = key.location;
            }
            else
            {
                slot.byte_offset = key.byteOffset;
                slot.type_size = key.typeSize;
                slot.bind_type = key.bindType;
                slot.location = key.location;
            }

            if constexpr (requires { key.set; })
                slot.set = key.set;
            if constexpr (requires { key.binding; })
            {
                slot.binding = key.binding;
                // Legacy codegen packed descriptor binding into `location` and left
                // `set`/`binding` unset (0). Prefer that encoding only when the
                // explicit set/binding metadata was never populated.
                if (slot.set == 0 && slot.binding == 0)
                    slot.binding = slot.location;
            }
            else
            {
                slot.binding = slot.location;
            }

            return slot;
        }
    };

    // 非虚绑定分派 (旧 friend 两指针设计): ResourceProxy 模板持具体管线引用,
    // 直接调基类的 public bind_* 成员, 无抽象基、无 virtual、无继承。
    template <typename Pipeline>
    class ResourceProxy
    {
    public:
        ResourceProxy(Pipeline& pipeline, BindingSlot slot) noexcept : pipeline_(pipeline), slot_(slot) {}

        ResourceProxy& operator=(const ResourceProxy&) = delete;
        ResourceProxy& operator=(ResourceProxy&&) = delete;

        template <typename T>
            requires(!std::same_as<std::remove_cvref_t<T>, ResourceProxy>)
        ResourceProxy& operator=(const T& value)
        {
            if constexpr (std::same_as<std::remove_cvref_t<T>, HardwareBuffer>)
            {
                pipeline_.bind_buffer(slot_, value);
            }
            else if constexpr (std::same_as<std::remove_cvref_t<T>, HardwareImage>)
            {
                pipeline_.bind_image(slot_, value);
            }
            else
            {
                static_assert(HardwareTransferable<std::remove_cvref_t<T>>, "Pipeline push constants must be trivially copyable non-pointer values.");
                pipeline_.bind_push_constant(slot_, &value, sizeof(std::remove_cvref_t<T>));
            }

            return *this;
        }

    private:
        Pipeline& pipeline_;
        BindingSlot slot_;
    };

    // ================================================================
    // Pipeline Runtime
    // ================================================================

    class ComputePipelineBase : public ResourceHandle
    {
    public:
        friend class HardwareExecutor;
        ComputePipelineBase();
        explicit ComputePipelineBase(ComputePipelineDesc desc, const std::source_location& source_location = std::source_location::current());

        ComputePipelineBase(const ComputePipelineBase& other);
        ComputePipelineBase(ComputePipelineBase&& other) noexcept;
        ~ComputePipelineBase();

        ComputePipelineBase& operator=(const ComputePipelineBase& other);
        ComputePipelineBase& operator=(ComputePipelineBase&& other) noexcept;
        ComputePipelineBase& operator()(uint16_t x, uint16_t y, uint16_t z);
        [[nodiscard]] ComputePipelineDesc desc() const;

        // 根据管线自身的 workgroup local size 将像素尺寸换算为 dispatch group 数。
        // 消除调用方对 local_size 的硬编码依赖（ceil(w/tgs.x), ceil(h/tgs.y)）。
        // local size 优先取自反射（编译进 shader 的真实值），构造参数仅作反射缺失时的回退。
        struct DispatchGroups { uint32_t x; uint32_t y; };
        [[nodiscard]] DispatchGroups dispatch_groups(uint32_t width, uint32_t height) const
        {
            const ktm::uvec3 tgs = desc().resolved_thread_group_size();
            if (tgs.x == 0 || tgs.y == 0)
                throw std::logic_error("ComputePipeline workgroup local size is zero; reflection failed and no override was provided.");
            return { (width  + tgs.x - 1u) / tgs.x,
                     (height + tgs.y - 1u) / tgs.y };
        }

        [[nodiscard]] CommandBatch command_batch();
        [[nodiscard]] explicit operator bool() const noexcept;

        void rebuild_pipeline(ComputePipelineDesc desc, const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& conditionInfo);
        const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& compute_condition_info();
    public:
        // 非虚绑定转发入口 (ResourceProxy 直接调用); set_*_direct 保留私有。
        void bind_push_constant(const BindingSlot& slot, const void* data, size_t size)
        {
            set_push_constant_direct(slot.byte_offset, data, size, slot.bind_type, slot.set, slot.binding);
        }

        void bind_buffer(const BindingSlot& slot, const HardwareBuffer& buffer)
        {
            set_resource_direct(slot.byte_offset, slot.type_size, buffer, slot.bind_type, slot.set, slot.binding);
        }

        void bind_image(const BindingSlot& slot, const HardwareImage& image)
        {
            set_resource_direct(slot.byte_offset, slot.type_size, image, slot.bind_type, slot.set, slot.binding);
        }

    private:
        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo condition_info_;
        std::unordered_map<std::string, std::shared_ptr<IResourceRef>> pipeline_pool_;
        std::source_location location_;
    };

    class RasterizerPipelineBase : public ResourceHandle
    {
    public:
        friend class HardwareExecutor;
        RasterizerPipelineBase();
        explicit RasterizerPipelineBase(RasterizerPipelineDesc desc, const std::source_location& source_location = std::source_location::current());

        RasterizerPipelineBase(const RasterizerPipelineBase& other);
        RasterizerPipelineBase(RasterizerPipelineBase&& other) noexcept;
        ~RasterizerPipelineBase();

        RasterizerPipelineBase& operator=(const RasterizerPipelineBase& other);
        RasterizerPipelineBase& operator=(RasterizerPipelineBase&& other) noexcept;

        RasterizerPipelineBase& operator()(uint16_t width, uint16_t height);
        RasterizerPipelineBase& record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
        RasterizerPipelineBase& record_indirect(const HardwareBuffer& index_buffer,
                                                const HardwareBuffer& vertex_buffer,
                                                const HardwareBuffer& indirect_buffer,
                                                const DrawIndexedIndirectParams& params);
        RasterizerPipelineBase& clear_records();
        RasterizerPipelineBase& bind_depth_target(HardwareImage& image);
        [[nodiscard]] RasterizerPipelineDesc desc() const;
        [[nodiscard]] CommandBatch command_batch() const;
        // 与 command_batch() 等价，但直接录进 recorder，省掉中间容器与一次批次拷贝。
        void record_into(CommandRecorder& recorder) const;
        [[nodiscard]] explicit operator bool() const noexcept;

        void rebuild_pipeline(RasterizerPipelineDesc desc, const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertConditionInfo, const EmbeddedShader::
                              ShaderCodeCompiler::ConditionInfo& fragConditionInfo);
        const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertex_condition_info() const;
        const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& fragment_condition_info() const;
    public:
        // 非虚绑定转发入口 (ResourceProxy 直接调用); set_*_direct 保留私有。
        void bind_push_constant(const BindingSlot& slot, const void* data, size_t size)
        {
            set_push_constant_direct(slot.byte_offset, data, size, slot.bind_type, slot.set, slot.binding);
        }

        void bind_buffer(const BindingSlot& slot, const HardwareBuffer& buffer)
        {
            set_resource_direct(slot.byte_offset, slot.type_size, buffer, slot.bind_type, slot.set, slot.binding);
        }

        void bind_image(const BindingSlot& slot, const HardwareImage& image)
        {
            set_resource_direct(slot.byte_offset, slot.type_size, image, slot.bind_type, slot.location, slot.set, slot.binding);
        }

    private:
        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t location = 0, uint32_t set = 0, uint32_t binding = 0);
        void add_auto_bind_entry(EmbeddedShader::AutoBindEntry entry);
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo vert_condition_info_;
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo frag_condition_info_;
        std::unordered_map<std::string, std::shared_ptr<IResourceRef>> pipeline_pool_;
        std::source_location location_;
    };

    namespace PipelineDetail
    {
        template <typename CS>
        concept GeneratedComputeShaderObject =
            (
            requires {
                std::remove_cvref_t<CS>::spirv;
            } ||
            requires {
                std::remove_cvref_t<CS>::slangModule;
            }) &&
            requires {
                typename std::remove_cvref_t<CS>::template Bindings<ComputePipelineBase>;
            };

        template <typename VS, typename FS>
        concept GeneratedRasterizerShaderObjects =
            requires {
                std::remove_cvref_t<VS>::slangModule;
                std::remove_cvref_t<FS>::slangModule;
            } &&
            requires {
                typename std::remove_cvref_t<VS>::template ResourceBindings<RasterizerPipelineBase>;
                typename std::remove_cvref_t<FS>::template ResourceBindings<RasterizerPipelineBase>;
                typename std::remove_cvref_t<FS>::template OutputBindings<RasterizerPipelineBase>;
            };

        template <typename F>
        concept EdslComputeShaderCode =
            !GeneratedComputeShaderObject<F> &&
            !std::same_as<std::remove_cvref_t<F>, ComputePipelineDesc>;

        template <typename VS, typename FS>
        concept EdslRasterizerShaderCode =
            !GeneratedRasterizerShaderObjects<VS, FS>;
    }


    template <>
    class ComputePipeline<void> : public ComputePipelineBase
    {
    public:
        using ComputePipelineBase::ComputePipelineBase;
        ComputePipeline() = default;

        template <PipelineDetail::EdslComputeShaderCode F>
        explicit ComputePipeline(F&& compute_shader_code,
                                 ktm::uvec3 numthreads = { 1, 1, 1 },
                                 EdslPipelineOptions options = {},
                                 const std::source_location& source_location = std::source_location::current())
            : ComputePipelineBase(
                  make_desc_from_edsl(std::forward<F>(compute_shader_code),
                                     numthreads,
                                     std::move(options),
                                     source_location),
                  source_location)
        {
        }

    private:
        template <typename F>
        static ComputePipelineDesc make_desc_from_edsl(F&& compute_shader_code,
                                                       ktm::uvec3 numthreads,
                                                       EdslPipelineOptions options,
                                                       std::source_location source_location)
        {
            options.compiler.enableMatrixColumnMajor = true;
            std::shared_ptr<EmbeddedShader::ComputePipelineObject> object{
                new EmbeddedShader::ComputePipelineObject(
                    EmbeddedShader::ComputePipelineObject::compile(
                        std::forward<F>(compute_shader_code), numthreads,
                        options.compiler, source_location))};

            ComputePipelineDesc desc(
                PipelineShaderDesc{
                    PipelineShaderStage::Compute,
                    object->compute->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,
                                                   options.compiler.enableBindless) },
                numthreads);
            desc.pipelineObject = object;

            if (options.auto_bind)
                desc.auto_bind_entries = object->autoBindEntries;
            return desc;
        }
    public:

        template <PipelineDetail::EdslComputeShaderCode F>
        explicit ComputePipeline(F&& compute_shader_code,
                                 ktm::uvec3 numthreads,
                                 const std::source_location& source_location)
            : ComputePipeline(std::forward<F>(compute_shader_code), numthreads, {}, source_location)
        {
        }
    };

    template <typename CS>
    class ComputePipeline : public ComputePipelineBase, public CS::template Bindings<ComputePipelineBase>
    {
    public:
        using ShaderBindings = typename CS::template Bindings<ComputePipelineBase>;

        static ComputePipelineDesc make_desc(ktm::uvec3 numthreads = { 1, 1, 1 })
        {
            return ComputePipelineDesc(
                compile_slang_stage(PipelineShaderStage::Compute, CS::slangModule),
                numthreads);
        }

        explicit ComputePipeline(CS, ktm::uvec3 numthreads = { 1, 1, 1 },
                                 const std::source_location& source_location = std::source_location::current())
            : ComputePipelineBase(make_desc(numthreads), source_location),
              ShaderBindings(static_cast<ComputePipelineBase*>(this))
        {
        }

        explicit ComputePipeline(ktm::uvec3 numthreads = { 1, 1, 1 },
                                 const std::source_location& source_location = std::source_location::current())
            : ComputePipelineBase(make_desc(numthreads), source_location),
              ShaderBindings(static_cast<ComputePipelineBase*>(this))
        {
        }

        explicit ComputePipeline(ComputePipelineDesc desc,
                                 const std::source_location& source_location = std::source_location::current())
            : ComputePipelineBase(std::move(desc), source_location),
              ShaderBindings(static_cast<ComputePipelineBase*>(this))
        {
        }

        ComputePipeline(const ComputePipeline& other)
            : ComputePipelineBase(other),
              ShaderBindings(static_cast<ComputePipelineBase*>(this))
        {
        }

        ComputePipeline(ComputePipeline&& other) noexcept
            : ComputePipelineBase(std::move(other)),
              ShaderBindings(static_cast<ComputePipelineBase*>(this))
        {
        }

        ComputePipeline& operator=(const ComputePipeline& other)
        {
            ComputePipelineBase::operator=(other);
            return *this;
        }

        ComputePipeline& operator=(ComputePipeline&& other) noexcept
        {
            ComputePipelineBase::operator=(std::move(other));
            return *this;
        }
    };

    ComputePipeline() -> ComputePipeline<>;
    ComputePipeline(ComputePipelineDesc) -> ComputePipeline<>;
    ComputePipeline(ComputePipelineDesc, const std::source_location&) -> ComputePipeline<>;
    template <PipelineDetail::GeneratedComputeShaderObject CS>
    ComputePipeline(CS) -> ComputePipeline<std::remove_cvref_t<CS>>;
    template <PipelineDetail::GeneratedComputeShaderObject CS>
    ComputePipeline(CS, ktm::uvec3) -> ComputePipeline<std::remove_cvref_t<CS>>;
    template <PipelineDetail::GeneratedComputeShaderObject CS>
    ComputePipeline(CS, ktm::uvec3, const std::source_location&) -> ComputePipeline<std::remove_cvref_t<CS>>;
    template <PipelineDetail::EdslComputeShaderCode F>
    ComputePipeline(F) -> ComputePipeline<>;
    template <PipelineDetail::EdslComputeShaderCode F>
    ComputePipeline(F, ktm::uvec3) -> ComputePipeline<>;
    template <PipelineDetail::EdslComputeShaderCode F>
    ComputePipeline(F, ktm::uvec3, EdslPipelineOptions) -> ComputePipeline<>;
    template <PipelineDetail::EdslComputeShaderCode F>
    ComputePipeline(F, ktm::uvec3, const std::source_location&) -> ComputePipeline<>;
    template <PipelineDetail::EdslComputeShaderCode F>
    ComputePipeline(F, ktm::uvec3, EdslPipelineOptions, const std::source_location&) -> ComputePipeline<>;

    template <>
    class RasterizerPipeline<void, void> : public RasterizerPipelineBase
    {
    public:
        using RasterizerPipelineBase::RasterizerPipelineBase;
        RasterizerPipeline() = default;

        template <typename VS, typename FS>
            requires PipelineDetail::EdslRasterizerShaderCode<VS, FS>
        static RasterizerPipelineDesc make_desc(VS&& vertex_shader_code,
                                                FS&& fragment_shader_code,
                                                RasterizerPipelineDesc state = {},
                                                EdslPipelineOptions options = {},
                                                std::source_location source_location = std::source_location::current())
        {
            // Inline from_edsl logic
            options.compiler.enableMatrixColumnMajor = true;
            std::shared_ptr<EmbeddedShader::RasterizedPipelineObject> object{
                new EmbeddedShader::RasterizedPipelineObject(
                    EmbeddedShader::RasterizedPipelineObject::compile(
                        std::forward<VS>(vertex_shader_code),
                        std::forward<FS>(fragment_shader_code),
                        options.compiler,
                        source_location))};

            RasterizerPipelineDesc desc(
                PipelineShaderDesc {
                    PipelineShaderStage::Vertex,
                    object->vertex->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,
                                                 options.compiler.enableBindless) },
                PipelineShaderDesc {
                    PipelineShaderStage::Fragment,
                    object->fragment->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,
                                                   options.compiler.enableBindless) });
            desc.pipelineObject = object;

            if (options.auto_bind)
                desc.auto_bind_entries = object->autoBindEntries;

            // 用调用方传入的状态覆盖 EDSL 派生的默认状态（不透传 shader）。
            desc.rasterizer = std::move(state.rasterizer);
            desc.depth_stencil = std::move(state.depth_stencil);
            desc.blend = std::move(state.blend);
            desc.multisample = std::move(state.multisample);
            desc.multiview_count = state.multiview_count;
            desc.clear_color_target = state.clear_color_target;
            desc.clear_depth_target = state.clear_depth_target;
            desc.debug_name = std::move(state.debug_name);
            return desc;
        }

        template <typename VS, typename FS>
            requires PipelineDetail::EdslRasterizerShaderCode<VS, FS>
        explicit RasterizerPipeline(VS&& vertex_shader_code,
                                    FS&& fragment_shader_code,
                                    RasterizerPipelineDesc desc = {},
                                    EdslPipelineOptions options = {},
                                    const std::source_location& source_location = std::source_location::current())
            : RasterizerPipelineBase(
                  make_desc(std::forward<VS>(vertex_shader_code),
                            std::forward<FS>(fragment_shader_code),
                            std::move(desc),
                            std::move(options),
                            source_location),
                  source_location)
        {
        }
    };

    template <typename VS, typename FS>
    class RasterizerPipeline : public RasterizerPipelineBase,
                               public VS::template ResourceBindings<RasterizerPipelineBase>,
                               public FS::template ResourceBindings<RasterizerPipelineBase>,
                               public FS::template OutputBindings<RasterizerPipelineBase>
    {
    public:
        using VertexResourceBindings = typename VS::template ResourceBindings<RasterizerPipelineBase>;
        using FragmentResourceBindings = typename FS::template ResourceBindings<RasterizerPipelineBase>;
        using FragmentOutputBindings = typename FS::template OutputBindings<RasterizerPipelineBase>;

        static RasterizerPipelineDesc make_desc(RasterizerPipelineDesc desc = {})
        {
            desc.vertex_shader = compile_slang_stage(PipelineShaderStage::Vertex, VS::slangModule, {});
            desc.fragment_shader = compile_slang_stage(PipelineShaderStage::Fragment, FS::slangModule, {});
            return desc;
        }

        explicit RasterizerPipeline(const std::source_location& source_location = std::source_location::current())
            : RasterizerPipelineBase(make_desc(), source_location),
              VertexResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentOutputBindings(static_cast<RasterizerPipelineBase*>(this))
        {
        }

        explicit RasterizerPipeline(VS, FS, RasterizerPipelineDesc desc = {},
                                    const std::source_location& source_location = std::source_location::current())
            : RasterizerPipelineBase(make_desc(std::move(desc)), source_location),
              VertexResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentOutputBindings(static_cast<RasterizerPipelineBase*>(this))
        {
        }

        explicit RasterizerPipeline(RasterizerPipelineDesc desc,
                                    const std::source_location& source_location = std::source_location::current())
            : RasterizerPipelineBase(std::move(desc), source_location),
              VertexResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentOutputBindings(static_cast<RasterizerPipelineBase*>(this))
        {
        }

        RasterizerPipeline(const RasterizerPipeline& other)
            : RasterizerPipelineBase(other),
              VertexResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentOutputBindings(static_cast<RasterizerPipelineBase*>(this))
        {
        }

        RasterizerPipeline(RasterizerPipeline&& other) noexcept
            : RasterizerPipelineBase(std::move(other)),
              VertexResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentOutputBindings(static_cast<RasterizerPipelineBase*>(this))
        {
        }

        RasterizerPipeline& operator=(const RasterizerPipeline& other)
        {
            RasterizerPipelineBase::operator=(other);
            return *this;
        }

        RasterizerPipeline& operator=(RasterizerPipeline&& other) noexcept
        {
            RasterizerPipelineBase::operator=(std::move(other));
            return *this;
        }
    };

    RasterizerPipeline() -> RasterizerPipeline<>;
    RasterizerPipeline(RasterizerPipelineDesc) -> RasterizerPipeline<>;
    RasterizerPipeline(RasterizerPipelineDesc, const std::source_location&) -> RasterizerPipeline<>;
    template <typename VS, typename FS>
        requires PipelineDetail::GeneratedRasterizerShaderObjects<VS, FS>
    RasterizerPipeline(VS, FS) -> RasterizerPipeline<std::remove_cvref_t<VS>, std::remove_cvref_t<FS>>;
    template <typename VS, typename FS>
        requires PipelineDetail::GeneratedRasterizerShaderObjects<VS, FS>
    RasterizerPipeline(VS, FS, RasterizerPipelineDesc) -> RasterizerPipeline<std::remove_cvref_t<VS>, std::remove_cvref_t<FS>>;
    template <typename VS, typename FS>
        requires PipelineDetail::GeneratedRasterizerShaderObjects<VS, FS>
    RasterizerPipeline(VS, FS, RasterizerPipelineDesc, const std::source_location&) -> RasterizerPipeline<std::remove_cvref_t<VS>, std::remove_cvref_t<FS>>;
    template <typename VS, typename FS>
        requires PipelineDetail::EdslRasterizerShaderCode<VS, FS>
    RasterizerPipeline(VS, FS) -> RasterizerPipeline<>;
    template <typename VS, typename FS>
        requires PipelineDetail::EdslRasterizerShaderCode<VS, FS>
    RasterizerPipeline(VS, FS, RasterizerPipelineDesc) -> RasterizerPipeline<>;
    template <typename VS, typename FS>
        requires PipelineDetail::EdslRasterizerShaderCode<VS, FS>
    RasterizerPipeline(VS, FS, RasterizerPipelineDesc, EdslPipelineOptions) -> RasterizerPipeline<>;
    template <typename VS, typename FS>
        requires PipelineDetail::EdslRasterizerShaderCode<VS, FS>
    RasterizerPipeline(VS, FS, RasterizerPipelineDesc, EdslPipelineOptions, const std::source_location&) -> RasterizerPipeline<>;

    // ================================================================
    // Value Command Facades
    // ================================================================

    struct CopyBufferToImageCommand
    {
        BufferRef src {};
        ImageRef dst {};
        BufferImageCopyRegion region {};
        DeviceMask devices {};

        // 定义移入硬件层 execution.cpp（command_ir.h 内的 CommandRecorder 可见）。
        void record(CommandRecorder& recorder) const;
    };

    struct PresentCommand
    {
        DisplayerRef displayer {};
        ImageRef image {};
        DeviceId present_device {};
        bool allow_cpu_bridge_fallback { true };

        // 定义移入硬件层 execution.cpp（command_ir.h 内的 CommandRecorder 可见）。
        void record(CommandRecorder& recorder) const;
    };

    [[nodiscard]] inline CopyBufferToImageCommand copy_to_image(BufferRef src, ImageRef dst, BufferImageCopyRegion region, DeviceMask devices = {})
    {
        return { src, dst, region, devices };
    }

    [[nodiscard]] inline PresentCommand present(DisplayerRef displayer, ImageRef image, DeviceId present_device = {}, bool allow_cpu_bridge_fallback = true)
    {
        return { displayer, image, present_device, allow_cpu_bridge_fallback };
    }

    [[nodiscard]] inline PresentCommand present(const HardwareDisplayer& displayer, const HardwareImage& image, DeviceId present_device = {}, bool allow_cpu_bridge_fallback = true)
    {
        return present(displayer.displayer_ref(),
                       { static_cast<const ResourceHandle&>(image) },
                       present_device,
                       allow_cpu_bridge_fallback);
    }

}

template <typename PipelineType>
template <typename T>
EmbeddedShader::BoundField<PipelineType>& EmbeddedShader::BoundField<PipelineType>::operator=(const T& value)
{
    Corona::Horizon::ResourceProxy proxy(*pipeline_, Corona::Horizon::BindingSlot::from(*this));
    proxy = value;
    return *this;
}

// ================================================================
// Profiling instrumentation (merged from horizon_profiling.h)
// ----------------------------------------------------------------
// Tracy wrapper. When HORIZON_TRACY_ENABLED is OFF the macros expand to
// nothing, so call sites never need #ifdef guards. Enabled via
// HORIZON_ENABLE_TRACY (see src/CMakeLists.txt).
// ================================================================
#if defined(HORIZON_TRACY_ENABLED)

#include <tracy/Tracy.hpp>

// Marks the end of a frame (call once per presented frame).
#define HORIZON_PROFILE_FRAME() FrameMark
// Scoped CPU zone named after the enclosing function.
#define HORIZON_PROFILE_SCOPE() ZoneScoped
// Scoped CPU zone with an explicit name (string literal).
#define HORIZON_PROFILE_SCOPE_N(name) ZoneScopedN(name)
// Plots a numeric value over time (name must be a string literal).
#define HORIZON_PROFILE_PLOT(name, value) TracyPlot(name, static_cast<double>(value))
// Names the current thread in the Tracy UI.
#define HORIZON_PROFILE_THREAD(name) tracy::SetThreadName(name)

#else

#define HORIZON_PROFILE_FRAME()
#define HORIZON_PROFILE_SCOPE()
#define HORIZON_PROFILE_SCOPE_N(name)
#define HORIZON_PROFILE_PLOT(name, value)
#define HORIZON_PROFILE_THREAD(name)

#endif
