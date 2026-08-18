#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ktm/ktm.h>

#include "Codegen/ComputePipelineObject.h"
#include "Codegen/RasterizedPipelineObject.h"
#include "Codegen/VariateProxy.h"
#include "Compiler/ShaderCodeCompiler.h"

namespace Corona::Horizon
{

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

    using BufferUsageFlags = uint32_t;

    enum : BufferUsageFlags
    {
        BufferUsage_None = 0,
        BufferUsage_TransferSrc = 1 << 0,
        BufferUsage_TransferDst = 1 << 1,
        BufferUsage_Vertex = 1 << 2,
        BufferUsage_Index = 1 << 3,
        BufferUsage_Uniform = 1 << 4,
        BufferUsage_Storage = 1 << 5,
        BufferUsage_Indirect = 1 << 6,
    };

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

    enum class ExternalMemoryHandleType : uint8_t
    {
        None,
        OpaqueFd,
        OpaqueWin32,
    };

    struct ExternalMemoryHandle
    {
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

    enum class ImageDimension : uint8_t
    {
        Image1D,
        Image2D,
        Image3D,
        Cube,
        Image2DArray,
        CubeArray,
    };

    using ImageUsageFlags = uint32_t;

    enum : ImageUsageFlags
    {
        ImageUsage_None = 0,
        ImageUsage_TransferSrc = 1 << 0,
        ImageUsage_TransferDst = 1 << 1,
        ImageUsage_Sampled = 1 << 2,
        ImageUsage_Storage = 1 << 3,
        ImageUsage_ColorAttachment = 1 << 4,
        ImageUsage_DepthStencilAttachment = 1 << 5,
    };

    struct ImageExtent
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
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
    };

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

        uint32_t stride = 0;
        IndexType index_type = IndexType::Auto;
        bool enable_scissor = false;
        ScissorRect scissor{};
        std::string debug_label;
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

    using ColorWriteMask = uint32_t;

    enum : ColorWriteMask
    {
        ColorWrite_None = 0,
        ColorWrite_R = 1 << 0,
        ColorWrite_G = 1 << 1,
        ColorWrite_B = 1 << 2,
        ColorWrite_A = 1 << 3,
        ColorWrite_RGB = ColorWrite_R | ColorWrite_G | ColorWrite_B,
        ColorWrite_RGBA = ColorWrite_R | ColorWrite_G | ColorWrite_B | ColorWrite_A,
    };

    class HardwareBuffer;
    class HardwareImage;

    struct CopyBufferToImageCommand;

    class ComputePipelineBase;
    class RasterizerPipelineBase;
    template <typename CS = void>
    class ComputePipeline;
    template <typename VS = void, typename FS = void>
    class RasterizerPipeline;

    class HardwareExecutor;
    class HardwareDisplayer;

    class CommandRecorder;
    class Queue;
    class ExecutionCompiler;
    class VulkanCommandEncoder;
    struct ExecutionPlan;
    enum class QueueCapability;
    struct RecordedTask;
    struct PresentResult;
    struct SubmissionToken;

    struct DeviceId
    {
        uint32_t value { 0 };

        [[nodiscard]] friend bool operator==(DeviceId left, DeviceId right) noexcept
        {
            return left.value == right.value;
        }
    };

    struct DisplayerRef
    {
        std::uintptr_t id { 0 };
    };

    struct SubmitReceipt
    {
        uint64_t serial { 0 };
        std::shared_ptr<const void> data {};
    };

    struct CommitCommand
    {
    };

    [[nodiscard]] CommitCommand commit() noexcept;

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

        HardwareStream& operator<<(ComputePipelineBase& pipeline);
        HardwareStream& operator<<(RasterizerPipelineBase& pipeline);
        [[nodiscard]] SubmitReceipt operator<<(CommitCommand command);

        template <typename Command>
            requires(!std::is_same_v<std::remove_cvref_t<Command>, CommitCommand> &&
                     !std::is_same_v<std::remove_cvref_t<Command>, ComputePipelineBase> &&
                     !std::is_same_v<std::remove_cvref_t<Command>, RasterizerPipelineBase> &&
                     requires(const std::remove_cvref_t<Command>& cmd, CommandRecorder& rec) {
                         cmd.record(rec);
                     })
        HardwareStream& operator<<(Command&& command)
        {
            ensure_open();
            command.record(*recorder_);
            return *this;
        }

    private:
        void ensure_open() const;

        HardwareExecutor* executor_ {};

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

        [[nodiscard]] HardwareStream operator<<(RasterizerPipelineBase& pipeline);
        [[nodiscard]] HardwareStream operator<<(ComputePipelineBase& pipeline);
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

        std::vector<std::shared_ptr<SubmissionToken>> pending_waits_;
    };

    template <typename T>
    concept HardwareTransferable = std::is_trivially_copyable_v<std::remove_cvref_t<T>> && !std::is_pointer_v<std::remove_cvref_t<T>>;

    template <typename T>
    concept HardwareIndexType = std::same_as<std::remove_cvref_t<T>, uint16_t> || std::same_as<std::remove_cvref_t<T>, uint32_t>;

    struct HardwareBufferDesc
    {
        uint64_t element_count = 0;
        uint32_t element_size = 0;
        BufferUsageFlags usage = BufferUsage_None;
        CpuAccessMode cpu_access = CpuAccessMode::Write;
        bool dedicated = false;
        bool exportable = false;
        std::string debug_name;

        [[nodiscard]] uint64_t byte_size() const;

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
            return typed<T>(count, BufferUsage_TransferDst | BufferUsage_Vertex, std::move(name));
        }

        template <HardwareIndexType T>
        [[nodiscard]] static HardwareBufferDesc index(uint64_t count, std::string name = {})
        {
            return typed<T>(count, BufferUsage_TransferDst | BufferUsage_Index, std::move(name));
        }

        [[nodiscard]] static HardwareBufferDesc indirect(uint64_t command_count, std::string name = {})
        {
            return typed<DrawIndexedIndirectCommand>(
                command_count,
                BufferUsage_TransferDst | BufferUsage_Indirect | BufferUsage_Storage,
                std::move(name));
        }
    };

    class HardwareBuffer : public ResourceHandle
    {
    public:
        HardwareBuffer() = default;
        HardwareBuffer(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data = {});

        HardwareBuffer(const HardwareBuffer& other) noexcept = default;
        HardwareBuffer(HardwareBuffer&& other) noexcept = default;
        ~HardwareBuffer() = default;

        HardwareBuffer& operator=(const HardwareBuffer& other) noexcept = default;
        HardwareBuffer& operator=(HardwareBuffer&& other) noexcept = default;
        [[nodiscard]] explicit operator bool() const noexcept { return ResourceHandle::operator bool(); }

        [[nodiscard]] uint64_t get_element_size() const;
        [[nodiscard]] uint64_t get_element_count() const;
        [[nodiscard]] uint64_t get_byte_size() const;
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

    struct HardwareImageDesc
    {
        ImageDimension dimension = ImageDimension::Image2D;
        ImageExtent extent {};
        Format format = Format::UNKNOWN;
        ImageUsageFlags usage = ImageUsage_Sampled | ImageUsage_TransferDst;
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
                                            ImageUsageFlags usage = ImageUsage_Sampled | ImageUsage_TransferDst,
                                            std::string name = {});

        static HardwareImageDesc texture_2d_array(uint32_t width,
                                                  uint32_t height,
                                                  uint32_t layers,
                                                  Format format,
                                                  ImageUsageFlags usage = ImageUsage_Sampled | ImageUsage_TransferDst,
                                                  std::string name = {});

        static HardwareImageDesc cube(uint32_t size,
                                      Format format,
                                      ImageUsageFlags usage = ImageUsage_Sampled | ImageUsage_TransferDst,
                                      std::string name = {});

        static HardwareImageDesc depth_attachment(uint32_t width,
                                                  uint32_t height,
                                                  Format format,
                                                  std::string name = {});
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

    struct ComputePipelineShaders
    {
        EmbeddedShader::ShaderCodeModule compute;
        std::shared_ptr<EmbeddedShader::ComputePipelineObject> object;
        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries;
    };

    struct RasterizerPipelineShaders
    {
        EmbeddedShader::ShaderCodeModule vertex;
        EmbeddedShader::ShaderCodeModule fragment;
        std::shared_ptr<EmbeddedShader::RasterizedPipelineObject> object;
        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries;
    };

    struct ComputePipelineDesc
    {

        ktm::uvec3 thread_group_size = { 1, 1, 1 };
    };

    struct RasterizerPipelineDesc
    {
        bool depth_clamp_enabled = false;
        bool rasterizer_discard_enabled = false;
        float line_width = 1.0f;

        bool depth_test_enabled = true;
        bool depth_write_enabled = true;
        CompareOp depth_compare_op = CompareOp::LessOrEqual;

        bool blend_enabled = true;
        BlendFactor src_color_blend_factor = BlendFactor::SrcAlpha;
        BlendFactor dst_color_blend_factor = BlendFactor::OneMinusSrcAlpha;
        BlendOp color_blend_op = BlendOp::Add;
        BlendFactor src_alpha_blend_factor = BlendFactor::One;
        BlendFactor dst_alpha_blend_factor = BlendFactor::OneMinusSrcAlpha;
        BlendOp alpha_blend_op = BlendOp::Add;
        ColorWriteMask color_write_mask = ColorWrite_RGBA;
        bool logic_op_enabled = false;

        SampleCount sample_count = SampleCount::Count1;
        bool sample_shading_enabled = false;
        float min_sample_shading = 1.0f;

        uint32_t multiview_count = 1;

        bool clear_color_target = true;

        bool clear_depth_target = true;

        std::string debug_name;
    };

    [[nodiscard]] EmbeddedShader::ShaderCodeModule compile_slang_stage(
        EmbeddedShader::ShaderStage stage,
        EmbeddedShader::SlangModule& module);

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

    class ComputePipelineBase : public ResourceHandle
    {
    public:

        friend class HardwareExecutor;
        friend class HardwareStream;
        friend class VulkanCommandEncoder;

        ComputePipelineBase();
        ComputePipelineBase(ComputePipelineDesc desc,
                            ComputePipelineShaders shaders,
                            const std::source_location& source_location = std::source_location::current());

        ComputePipelineBase(const ComputePipelineBase& other);
        ComputePipelineBase(ComputePipelineBase&& other) noexcept;
        ~ComputePipelineBase();

        ComputePipelineBase& operator=(const ComputePipelineBase& other);
        ComputePipelineBase& operator=(ComputePipelineBase&& other) noexcept;

        ComputePipelineBase& groups(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z = 1);
        ComputePipelineBase& dispatch_extent(uint32_t width, uint32_t height);

        [[nodiscard]] explicit operator bool() const noexcept;

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
        void record_into(CommandRecorder& recorder);

        bool sync_shader_conditions(const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& conditionInfo);

        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void rebuild_pipeline(ComputePipelineDesc desc,
                              ComputePipelineShaders shaders,
                              const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& conditionInfo);
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo condition_info_;
        std::unordered_map<std::string, std::shared_ptr<IResourceRef>> pipeline_pool_;
        std::source_location location_;
    };

    class RasterizerPipelineBase : public ResourceHandle
    {
    public:

        friend class HardwareExecutor;
        friend class HardwareStream;
        friend class VulkanCommandEncoder;

        RasterizerPipelineBase();
        RasterizerPipelineBase(RasterizerPipelineDesc desc,
                               RasterizerPipelineShaders shaders,
                               const std::source_location& source_location = std::source_location::current());

        RasterizerPipelineBase(const RasterizerPipelineBase& other);
        RasterizerPipelineBase(RasterizerPipelineBase&& other) noexcept;
        ~RasterizerPipelineBase();

        RasterizerPipelineBase& operator=(const RasterizerPipelineBase& other);
        RasterizerPipelineBase& operator=(RasterizerPipelineBase&& other) noexcept;

        RasterizerPipelineBase& extent(uint32_t width, uint32_t height);
        RasterizerPipelineBase& record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
        RasterizerPipelineBase& record_indirect(const HardwareBuffer& index_buffer,
                                                const HardwareBuffer& vertex_buffer,
                                                const HardwareBuffer& indirect_buffer,
                                                const DrawIndexedIndirectParams& params);
        RasterizerPipelineBase& clear_records();
        RasterizerPipelineBase& bind_depth_target(HardwareImage& image);
        [[nodiscard]] explicit operator bool() const noexcept;

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
        void record_into(CommandRecorder& recorder) const;

        bool sync_shader_conditions(const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertConditionInfo,
                                    const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& fragConditionInfo);

        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t location = 0, uint32_t set = 0, uint32_t binding = 0);
        void rebuild_pipeline(RasterizerPipelineDesc desc,
                              RasterizerPipelineShaders shaders,
                              const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertConditionInfo,
                              const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& fragConditionInfo);
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
                                 const std::source_location& source_location = std::source_location::current())
            : ComputePipelineBase(
                  ComputePipelineDesc { numthreads },
                  compile_edsl(std::forward<F>(compute_shader_code), numthreads, source_location),
                  source_location)
        {
        }

    private:

        template <typename F>
        static ComputePipelineShaders compile_edsl(F&& compute_shader_code,
                                                  ktm::uvec3 numthreads,
                                                  std::source_location source_location)
        {
            EmbeddedShader::CompilerOption compiler;
            compiler.enableMatrixColumnMajor = true;

            ComputePipelineShaders shaders;
            shaders.object = std::make_shared<EmbeddedShader::ComputePipelineObject>(
                EmbeddedShader::ComputePipelineObject::compile(
                    std::forward<F>(compute_shader_code), numthreads,
                    compiler, source_location));
            shaders.compute = shaders.object->compute->getShaderCode(
                EmbeddedShader::ShaderLanguage::SpirV, compiler.enableBindless);

            shaders.auto_bind_entries = shaders.object->autoBindEntries;
            return shaders;
        }
    };

    template <typename CS>
    class ComputePipeline : public ComputePipelineBase, public CS::template Bindings<ComputePipelineBase>
    {
    public:
        using ShaderBindings = typename CS::template Bindings<ComputePipelineBase>;

        static ComputePipelineShaders make_shaders()
        {
            return { compile_slang_stage(EmbeddedShader::ShaderStage::ComputeShader, CS::slangModule) };
        }

        explicit ComputePipeline(CS, ktm::uvec3 numthreads = { 1, 1, 1 },
                                 const std::source_location& source_location = std::source_location::current())
            : ComputePipelineBase(ComputePipelineDesc { numthreads }, make_shaders(), source_location),
              ShaderBindings(static_cast<ComputePipelineBase*>(this))
        {
        }

        explicit ComputePipeline(ktm::uvec3 numthreads = { 1, 1, 1 },
                                 const std::source_location& source_location = std::source_location::current())
            : ComputePipelineBase(ComputePipelineDesc { numthreads }, make_shaders(), source_location),
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
    ComputePipeline(F, ktm::uvec3, const std::source_location&) -> ComputePipeline<>;

    template <>
    class RasterizerPipeline<void, void> : public RasterizerPipelineBase
    {
    public:
        using RasterizerPipelineBase::RasterizerPipelineBase;
        RasterizerPipeline() = default;

        template <typename VS, typename FS>
            requires PipelineDetail::EdslRasterizerShaderCode<VS, FS>
        explicit RasterizerPipeline(VS&& vertex_shader_code,
                                    FS&& fragment_shader_code,
                                    RasterizerPipelineDesc desc = {},
                                    const std::source_location& source_location = std::source_location::current())
            : RasterizerPipelineBase(
                  std::move(desc),
                  compile_edsl(std::forward<VS>(vertex_shader_code),
                               std::forward<FS>(fragment_shader_code),
                               source_location),
                  source_location)
        {
        }

    private:

        template <typename VS, typename FS>
        static RasterizerPipelineShaders compile_edsl(VS&& vertex_shader_code,
                                                     FS&& fragment_shader_code,
                                                     std::source_location source_location)
        {
            EmbeddedShader::CompilerOption compiler;
            compiler.enableMatrixColumnMajor = true;

            RasterizerPipelineShaders shaders;
            shaders.object = std::make_shared<EmbeddedShader::RasterizedPipelineObject>(
                EmbeddedShader::RasterizedPipelineObject::compile(
                    std::forward<VS>(vertex_shader_code),
                    std::forward<FS>(fragment_shader_code),
                    compiler,
                    source_location));
            shaders.vertex = shaders.object->vertex->getShaderCode(
                EmbeddedShader::ShaderLanguage::SpirV, compiler.enableBindless);
            shaders.fragment = shaders.object->fragment->getShaderCode(
                EmbeddedShader::ShaderLanguage::SpirV, compiler.enableBindless);

            shaders.auto_bind_entries = shaders.object->autoBindEntries;
            return shaders;
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

        static RasterizerPipelineShaders make_shaders()
        {
            return { compile_slang_stage(EmbeddedShader::ShaderStage::VertexShader, VS::slangModule),
                     compile_slang_stage(EmbeddedShader::ShaderStage::FragmentShader, FS::slangModule) };
        }

        explicit RasterizerPipeline(RasterizerPipelineDesc desc = {},
                                    const std::source_location& source_location = std::source_location::current())
            : RasterizerPipelineBase(std::move(desc), make_shaders(), source_location),
              VertexResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentResourceBindings(static_cast<RasterizerPipelineBase*>(this)),
              FragmentOutputBindings(static_cast<RasterizerPipelineBase*>(this))
        {
        }

        explicit RasterizerPipeline(VS, FS, RasterizerPipelineDesc desc = {},
                                    const std::source_location& source_location = std::source_location::current())
            : RasterizerPipelineBase(std::move(desc), make_shaders(), source_location),
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
    RasterizerPipeline(VS, FS, RasterizerPipelineDesc, const std::source_location&) -> RasterizerPipeline<>;

    struct CopyBufferToImageCommand
    {
        HardwareBuffer src {};
        HardwareImage dst {};
        uint64_t buffer_offset { 0 };
        uint32_t image_layer { 0 };
        uint32_t image_mip { 0 };
        uint32_t device_mask_bits { 1 };

        void record(CommandRecorder& recorder) const;
    };

    struct PresentCommand
    {
        HardwareDisplayer displayer {};
        HardwareImage image {};
        DeviceId present_device {};
        bool allow_cpu_bridge_fallback { true };

        void record(CommandRecorder& recorder) const;
    };

    [[nodiscard]] PresentCommand present(const HardwareDisplayer& displayer,
                                         const HardwareImage& image,
                                         DeviceId present_device = {},
                                         bool allow_cpu_bridge_fallback = true);

}

#ifndef HORIZON_NO_SHORT_NAMESPACE
namespace horizon = Corona::Horizon;
#endif

template <typename PipelineType>
template <typename T>
EmbeddedShader::BoundField<PipelineType>& EmbeddedShader::BoundField<PipelineType>::operator=(const T& value)
{
    Corona::Horizon::ResourceProxy proxy(*pipeline_, Corona::Horizon::BindingSlot::from(*this));
    proxy = value;
    return *this;
}
