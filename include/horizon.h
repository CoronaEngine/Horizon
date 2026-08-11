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
#include "format.h"
#include "resource.h"
#include "horizon_execution.h"

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
    // Forward Declarations
    // ================================================================

    struct HardwareValidationConfig;
    class HardwareBuffer;
    class HardwareImage;

    struct CopyBufferToImageCommand;

    class CommandBatch;

    class PipelineBindingScope;
    class ResourceProxy;

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
    // RecordedTask、SubmissionToken、SubmitReceipt、CommandRecorder 等）
    // 已抽离到 horizon_execution.h（内部 / 高级 API）。普通用户无需直接
    // 接触这些类型；下方的资源 / 管线 / 命令门面会在内部使用它们。


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
        CommandRecorder recorder_ {};
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
        [[nodiscard]] std::vector<SubmissionToken> consume_pending_waits();

        std::shared_ptr<ExecutionCompiler> compiler_;
        QueueResolver queue_resolver_ {};
        mutable std::mutex mutex_;
        uint64_t next_submit_serial_ { 0 };
        std::vector<SubmissionToken> pending_waits_;
    };

    // 隐式提交标记：`stream << pipeline << H::submit` 等价于 `<< H::commit()`。
    // 复用既有的 `operator<<(CommitCommand)`，不引入新算子。
    inline constexpr CommitCommand submit {};



    // ================================================================
    // Validation
    // ================================================================

    struct HardwareValidationConfig
    {
        HardwareValidationMode mode = HardwareValidationMode::Throw;
    };

    void set_hardware_validation_config(HardwareValidationConfig config);
    [[nodiscard]] HardwareValidationConfig get_hardware_validation_config();
    [[nodiscard]] bool is_hardware_validation_enabled();



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

        template <HardwareTransferable T>
        [[nodiscard]] bool write(std::span<const T> data, uint64_t byte_offset = 0) const
        {
            return write_bytes(std::as_bytes(data), byte_offset);
        }

        [[nodiscard]] static HardwareBuffer from_bytes(std::span<const std::byte> data, uint32_t element_size, BufferUsageFlags usage, std::string name = {});

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBuffer vertex(std::span<const T> data, std::string name = {})
        {
            return HardwareBuffer(HardwareBufferDesc::vertex<T>(data.size(), std::move(name)), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
            requires std::ranges::sized_range<Range> && HardwareTransferable<std::ranges::range_value_t<Range>>
        [[nodiscard]] static HardwareBuffer vertex(const Range& data, std::string name = {})
        {
            return vertex<std::ranges::range_value_t<Range>>(
                std::span<const std::ranges::range_value_t<Range>>(std::ranges::data(data), std::ranges::size(data)),
                std::move(name));
        }

        template <HardwareIndexType T>
        [[nodiscard]] static HardwareBuffer index(std::span<const T> data, std::string name = {})
        {
            return HardwareBuffer(HardwareBufferDesc::index<T>(data.size(), std::move(name)), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
            requires std::ranges::sized_range<Range> && HardwareIndexType<std::ranges::range_value_t<Range>>
        [[nodiscard]] static HardwareBuffer index(const Range& data, std::string name = {})
        {
            return index<std::ranges::range_value_t<Range>>(
                std::span<const std::ranges::range_value_t<Range>>(std::ranges::data(data), std::ranges::size(data)),
                std::move(name));
        }

        [[nodiscard]] static HardwareBuffer indirect(std::span<const DrawIndexedIndirectCommand> commands, std::string name = {})
        {
            return HardwareBuffer(HardwareBufferDesc::indirect(commands.size(), std::move(name)), std::as_bytes(commands));
        }

        template <std::ranges::contiguous_range Range>
            requires std::ranges::sized_range<Range> &&
                     std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, DrawIndexedIndirectCommand>
        [[nodiscard]] static HardwareBuffer indirect(const Range& commands, std::string name = {})
        {
            return indirect(
                std::span<const DrawIndexedIndirectCommand>(std::ranges::data(commands), std::ranges::size(commands)),
                std::move(name));
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
        bool write_subresource_bytes(uint32_t layer_index, uint32_t mip_index, std::span<const std::byte> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;
        bool write_bytes(std::span<const std::byte> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;

        template <HardwareTransferable T>
        bool write_subresource(uint32_t layer_index, uint32_t mip_index, std::span<const T> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return write_subresource_bytes(layer_index, mip_index, std::as_bytes(data), row_pitch, slice_pitch);
        }

        template <HardwareTransferable T>
        bool write(std::span<const T> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return write_bytes(std::as_bytes(data), row_pitch, slice_pitch);
        }

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
            set_shaders(std::move(vertex), std::move(fragment));
        }

        void set_shaders(PipelineShaderDesc vertex, PipelineShaderDesc fragment)
        {
            if (vertex.stage != PipelineShaderStage::Vertex)
                throw std::invalid_argument("RasterizerPipelineDesc requires a vertex shader.");

            if (fragment.stage != PipelineShaderStage::Fragment)
                throw std::invalid_argument("RasterizerPipelineDesc requires a fragment shader.");

            vertex_shader = std::move(vertex);
            fragment_shader = std::move(fragment);
        }

        void set_shaders_from_slang(EmbeddedShader::SlangModule& vs_module,
                                    EmbeddedShader::SlangModule& fs_module,
                                    EmbeddedShader::CompilerOption compiler_option = {})
        {
            vertex_shader = compile_slang_to_shader_desc(
                PipelineShaderStage::Vertex, vs_module, compiler_option);
            fragment_shader = compile_slang_to_shader_desc(
                PipelineShaderStage::Fragment, fs_module, compiler_option);
        }

        // 用另一个 desc 的状态字段覆盖当前 desc（不透传 shader）。
        // 公开：EDSL 路径（RasterizerPipeline<void,void>::make_desc）在编译 shader 后用
        // 调用方传入的 RasterizerPipelineDesc 状态覆盖 EDSL 派生的默认状态。
        void apply_state(RasterizerPipelineDesc state)
        {
            rasterizer = std::move(state.rasterizer);
            depth_stencil = std::move(state.depth_stencil);
            blend = std::move(state.blend);
            multisample = std::move(state.multisample);
            multiview_count = state.multiview_count;
            clear_color_target = state.clear_color_target;
            clear_depth_target = state.clear_depth_target;
            debug_name = std::move(state.debug_name);
        }

    private:
        static PipelineShaderDesc compile_slang_to_shader_desc(
            PipelineShaderStage stage,
            EmbeddedShader::SlangModule& module,
            EmbeddedShader::CompilerOption compiler_option = {})
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
                    throw std::invalid_argument("compile_slang_to_shader_desc only supports vertex, fragment, and compute stages.");
                }
            }();
            args.module = &module;
            args.deps = std::move(compiler_option.slangModules);
            args.enableReflection = true;

            EmbeddedShader::SlangCompileResult result = EmbeddedShader::ShaderLanguageConverter::slangCompilerWithModules(args);
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

            return PipelineShaderDesc(stage, EmbeddedShader::ShaderCodeModule(std::move(spirv->second), std::move(resources)));
        }
    };

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

    class PipelineBindingScope
    {
    protected:
        virtual ~PipelineBindingScope() = default;

    private:
        friend class ResourceProxy;

        virtual void bind_push_constant(const BindingSlot& slot, const void* data, size_t size) = 0;
        virtual void bind_buffer(const BindingSlot& slot, const HardwareBuffer& buffer) = 0;
        virtual void bind_image(const BindingSlot& slot, const HardwareImage& image) = 0;
    };

    class ResourceProxy
    {
    public:
        ResourceProxy(PipelineBindingScope& pipeline, BindingSlot slot) noexcept : pipeline_(pipeline), slot_(slot) {}

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
        PipelineBindingScope& pipeline_;
        BindingSlot slot_;
    };

    template <typename Derived>
    struct ReflectedPipelineBindings
    {
        template <ReflectedBindingKey ProxyType>
        [[nodiscard]]
        ResourceProxy operator[](const ProxyType& proxy)
        {
            return ResourceProxy(static_cast<PipelineBindingScope&>(*static_cast<Derived*>(this)), BindingSlot::from(proxy));
        }
    };

    // ================================================================
    // Pipeline Runtime
    // ================================================================

    class ComputePipelineBase : public ResourceHandle, public PipelineBindingScope, public ReflectedPipelineBindings<ComputePipelineBase>
    {
        friend class HardwareExecutor;
    public:
        ComputePipelineBase();
        explicit ComputePipelineBase(ComputePipelineDesc desc, const std::source_location& source_location = std::source_location::current());

        ComputePipelineBase(const ComputePipelineBase& other);
        ComputePipelineBase(ComputePipelineBase&& other) noexcept;
        ~ComputePipelineBase();

        ComputePipelineBase& operator=(const ComputePipelineBase& other);
        ComputePipelineBase& operator=(ComputePipelineBase&& other) noexcept;
        ComputePipelineBase& operator()(uint16_t x, uint16_t y, uint16_t z);
        ComputePipelineBase& set_debug_label(std::string label);
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
    private:
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo condition_info_;
        std::unordered_map<std::string, std::shared_ptr<IResourceRef>> pipeline_pool_;
        std::source_location location_;
        void bind_push_constant(const BindingSlot& slot, const void* data, size_t size) override
        {
            set_push_constant_direct(slot.byte_offset, data, size, slot.bind_type, slot.set, slot.binding);
        }

        void bind_buffer(const BindingSlot& slot, const HardwareBuffer& buffer) override
        {
            set_resource_direct(slot.byte_offset, slot.type_size, buffer, slot.bind_type, slot.set, slot.binding);
        }

        void bind_image(const BindingSlot& slot, const HardwareImage& image) override
        {
            set_resource_direct(slot.byte_offset, slot.type_size, image, slot.bind_type, slot.set, slot.binding);
        }

        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
    };

    class RasterizerPipelineBase : public ResourceHandle, public PipelineBindingScope, public ReflectedPipelineBindings<RasterizerPipelineBase>
    {
        friend class HardwareExecutor;
    public:
        RasterizerPipelineBase();
        explicit RasterizerPipelineBase(RasterizerPipelineDesc desc, const std::source_location& source_location = std::source_location::current());

        RasterizerPipelineBase(const RasterizerPipelineBase& other);
        RasterizerPipelineBase(RasterizerPipelineBase&& other) noexcept;
        ~RasterizerPipelineBase();

        RasterizerPipelineBase& operator=(const RasterizerPipelineBase& other);
        RasterizerPipelineBase& operator=(RasterizerPipelineBase&& other) noexcept;

        RasterizerPipelineBase& operator()(uint16_t width, uint16_t height);
        RasterizerPipelineBase& record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer);
        RasterizerPipelineBase& record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
        RasterizerPipelineBase& record_indirect(const HardwareBuffer& index_buffer,
                                                const HardwareBuffer& vertex_buffer,
                                                const HardwareBuffer& indirect_buffer,
                                                const DrawIndexedIndirectParams& params);
        RasterizerPipelineBase& clear_records();
        RasterizerPipelineBase& bind_render_target(uint32_t location, HardwareImage& image);
        RasterizerPipelineBase& bind_depth_target(HardwareImage& image);
        [[nodiscard]] RasterizerPipelineDesc desc() const;
        [[nodiscard]] CommandBatch command_batch() const;
        // 与 command_batch() 等价，但直接录进 recorder，省掉中间容器与一次批次拷贝。
        void record_into(CommandRecorder& recorder) const;
        [[nodiscard]] explicit operator bool() const noexcept;

        template <typename TargetProxy>
            requires requires(TargetProxy& proxy) { proxy.boundResource_; }
        RasterizerPipelineBase& bind_render_target(uint32_t location, TargetProxy& proxy)
        {
            auto* image = static_cast<HardwareImage*>(proxy.boundResource_);
            if (image == nullptr)
                throw std::logic_error("RasterizerPipeline render target proxy is not bound to a HardwareImage.");

            add_auto_bind_entry({
                &proxy.boundResource_,
                0,
                0,
                static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs),
                location,
            });
            bind_render_target(location, *image);
            return *this;
        }

        void rebuild_pipeline(RasterizerPipelineDesc desc, const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertConditionInfo, const EmbeddedShader::
                              ShaderCodeCompiler::ConditionInfo& fragConditionInfo);
        const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& vertex_condition_info() const;
        const EmbeddedShader::ShaderCodeCompiler::ConditionInfo& fragment_condition_info() const;
    private:
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo vert_condition_info_;
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo frag_condition_info_;
        std::unordered_map<std::string, std::shared_ptr<IResourceRef>> pipeline_pool_;
        std::source_location location_;
        void bind_push_constant(const BindingSlot& slot, const void* data, size_t size) override
        {
            set_push_constant_direct(slot.byte_offset, data, size, slot.bind_type, slot.set, slot.binding);
        }

        void bind_buffer(const BindingSlot& slot, const HardwareBuffer& buffer) override
        {
            set_resource_direct(slot.byte_offset, slot.type_size, buffer, slot.bind_type, slot.set, slot.binding);
        }

        void bind_image(const BindingSlot& slot, const HardwareImage& image) override
        {
            set_resource_direct(slot.byte_offset, slot.type_size, image, slot.bind_type, slot.location, slot.set, slot.binding);
        }

        void set_push_constant_direct(uint64_t byte_offset, const void* data, size_t size, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer& buffer, int32_t bind_type, uint32_t set = 0, uint32_t binding = 0);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage& image, int32_t bind_type, uint32_t location = 0, uint32_t set = 0, uint32_t binding = 0);
        void add_auto_bind_entry(EmbeddedShader::AutoBindEntry entry);
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
            // Compile Slang module inline
            EmbeddedShader::SlangCompileArgs2 args;
            args.sourceLanguage = EmbeddedShader::ShaderLanguage::Slang;
            args.targetLanguages = { EmbeddedShader::ShaderLanguage::SpirV };
            args.stage = EmbeddedShader::ShaderStage::ComputeShader;
            args.module = &CS::slangModule;
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

            for (auto& info : resources.bindInfoPool)
            {
                if (auto pos = info.variateName.find_last_of('.'); pos != std::string::npos)
                    info.variateName = info.variateName.substr(pos + 1);
            }

            return ComputePipelineDesc(
                PipelineShaderDesc(PipelineShaderStage::Compute,
                                  EmbeddedShader::ShaderCodeModule(std::move(spirv->second),
                                                                  std::move(resources))),
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

            desc.apply_state(std::move(state));
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
            desc.set_shaders_from_slang(VS::slangModule, FS::slangModule);
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

    namespace CommandDetail
    {
        template <typename T>
        struct IsSharedPtr : std::false_type
        {
        };

        template <typename T>
        struct IsSharedPtr<std::shared_ptr<T>> : std::true_type
        {
        };

        template <typename T>
        inline constexpr bool is_shared_ptr_v = IsSharedPtr<std::remove_cvref_t<T>>::value;

        template <typename...>
        inline constexpr bool single_shared_ptr_v = false;

        template <typename T>
        inline constexpr bool single_shared_ptr_v<T> = is_shared_ptr_v<T>;

        template <typename Command>
        [[nodiscard]] StreamCommand make_stream_command(Command command)
        {
            return StreamCommand([command = std::move(command)](CommandRecorder& recorder) {
                command.record(recorder);
            });
        }
    }

    struct CopyBufferToImageCommand
    {
        BufferRef src {};
        ImageRef dst {};
        BufferImageCopyRegion region {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.copy_to_image(src, dst, region, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct ShaderDispatchCommand
    {
        DispatchDesc dispatch {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.dispatch(dispatch, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct BeginRenderingCommand
    {
        RenderingDesc rendering {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.begin_rendering(rendering, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct EndRenderingCommand
    {
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.end_rendering(devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct DrawIndexedCommand
    {
        BufferRef index {};
        BufferRef vertex {};
        DrawIndexedDesc draw {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.draw_indexed(index, vertex, draw, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    // 批量 indexed draw。payload 由 shared_ptr 持有：make_stream_command 会按值
    // 拷贝命令对象，而一个批次可能有数万条 draw，直接内嵌 vector 会让每次
    // StreamCommand 拷贝都变成一次深拷贝。
    struct DrawIndexedBatchCommand
    {
        std::shared_ptr<DrawIndexedBatchDesc> batch {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            if (batch)
                recorder.draw_indexed_batch(*batch, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct DrawIndexedIndirectStreamCommand
    {
        BufferRef index {};
        BufferRef vertex {};
        BufferRef indirect {};
        DrawIndexedIndirectDesc draw {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.draw_indexed_indirect(index, vertex, indirect, draw, devices);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    struct PresentCommand
    {
        DisplayerRef displayer {};
        ImageRef image {};
        DeviceId present_device {};
        bool allow_cpu_bridge_fallback { true };

        void record(CommandRecorder& recorder) const
        {
            recorder.present(displayer, image, present_device, allow_cpu_bridge_fallback);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }
    };

    class KeepAliveCommand
    {
    public:
        KeepAliveCommand() = default;

        explicit KeepAliveCommand(std::shared_ptr<void> object)
            : object_(std::move(object))
        {
        }

        void record(CommandRecorder& recorder) const
        {
            recorder.keep_alive(object_);
        }

        [[nodiscard]] StreamCommand stream_command() const
        {
            return CommandDetail::make_stream_command(*this);
        }

        [[nodiscard]] operator StreamCommand() const
        {
            return stream_command();
        }

    private:
        std::shared_ptr<void> object_ {};
    };

    [[nodiscard]] inline CopyBufferToImageCommand copy_to_image(BufferRef src, ImageRef dst, BufferImageCopyRegion region, DeviceMask devices = {})
    {
        return { src, dst, region, devices };
    }

    [[nodiscard]] inline ShaderDispatchCommand dispatch(DispatchDesc desc, DeviceMask devices = {})
    {
        return { std::move(desc), devices };
    }

    [[nodiscard]] inline BeginRenderingCommand begin_rendering(RenderingDesc desc, DeviceMask devices = {})
    {
        return { desc, devices };
    }

    [[nodiscard]] inline EndRenderingCommand end_rendering(DeviceMask devices = {})
    {
        return { devices };
    }

    [[nodiscard]] inline DrawIndexedCommand draw_indexed(BufferRef index, BufferRef vertex, DrawIndexedDesc desc, DeviceMask devices = {})
    {
        return { index, vertex, std::move(desc), devices };
    }

    [[nodiscard]] inline DrawIndexedBatchCommand draw_indexed_batch(DrawIndexedBatchDesc batch, DeviceMask devices = {})
    {
        return { std::make_shared<DrawIndexedBatchDesc>(std::move(batch)), devices };
    }

    [[nodiscard]] inline DrawIndexedIndirectStreamCommand draw_indexed_indirect(BufferRef index,
                                                                               BufferRef vertex,
                                                                               BufferRef indirect,
                                                                               DrawIndexedIndirectDesc desc,
                                                                               DeviceMask devices = {})
    {
        return { index, vertex, indirect, std::move(desc), devices };
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

    [[nodiscard]] inline KeepAliveCommand keep_alive(std::shared_ptr<void> object)
    {
        return KeepAliveCommand(std::move(object));
    }

    template <typename T>
        requires(!std::is_void_v<T>)
    [[nodiscard]] KeepAliveCommand keep_alive(std::shared_ptr<T> object)
    {
        return keep_alive(std::static_pointer_cast<void>(std::move(object)));
    }

    template <typename... Args>
        requires(sizeof...(Args) > 0u &&
                 (std::is_copy_constructible_v<std::remove_cvref_t<Args>> && ...) &&
                 !CommandDetail::single_shared_ptr_v<Args...>)
    [[nodiscard]] KeepAliveCommand keep_alive(Args&&... args)
    {
        using Storage = std::tuple<std::remove_cvref_t<Args>...>;
        return keep_alive(std::static_pointer_cast<void>(
            std::make_shared<Storage>(std::forward<Args>(args)...)));
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
