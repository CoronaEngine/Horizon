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

#ifndef HORIZON_ENABLE_VALIDATION
#define HORIZON_ENABLE_VALIDATION 1
#endif

namespace Corona::Horizon
{
    // ================================================================
    // Forward Declarations
    // ================================================================

    struct HardwareValidationConfig;
    class HardwareBuffer;
    class HardwareImage;
    class HardwareImageLayerSelector;
    //struct HardwarePushConstant;

    struct CopyBufferCommand;
    struct CopyBufferToImageCommand;
    struct CopyImageCommand;
    struct CopyImageToBufferCommand;

    //struct BottomLevelAccelerationStructure;
    //struct TopLevelAccelerationStructure;

    class CommandBatch;

    class PipelineBindingScope;
    class ResourceProxy;

    class ComputePipeline;
    class RasterizerPipeline;
    class RayTracingPipeline;

    class HardwareExecutor;
    class HardwareDisplayer;

    struct BindingSlot;

    // ================================================================
    // Execution / Command Public Types
    // ================================================================

    class Queue;
    class CommandRecorder;
    class ExecutionCompiler;
    struct ExecutionPlan;
    class SubmissionSync;

    enum class QueueCapability
    {
        Graphics,
        Compute,
        Transfer,
        Present
    };

    enum class AccessKind
    {
        Read,
        Write,
        ReadWrite
    };

    enum class CommandOp
    {
        CopyBuffer,
        CopyBufferToImage,
        Dispatch,
        BeginRendering,
        EndRendering,
        DrawIndexed,
        Present,
        HostCallback,
        KeepAlive,
        CopyImage,
        CopyImageToBuffer
    };

    enum class FeatureRequirement
    {
        TimelineSemaphore,
        Synchronization2,
        DeferredHostOperations,
        DeviceGroup
    };

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

    struct QueueId
    {
        DeviceId device {};
        uint32_t family_index { 0 };
        uint32_t queue_index { 0 };
        QueueCapability capability { QueueCapability::Transfer };
    };

    struct BufferRef
    {
        ResourceHandle handle {};
    };

    struct ShaderRef
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

    struct CopyRegion
    {
        uint64_t src_offset { 0 };
        uint64_t dst_offset { 0 };
        uint64_t size { 0 };
    };

    struct BufferImageCopyRegion
    {
        uint64_t buffer_offset { 0 };
        uint32_t image_layer { 0 };
        uint32_t image_mip { 0 };
    };

    struct ImageCopyRegion
    {
        uint32_t src_layer { 0 };
        uint32_t dst_layer { 0 };
        uint32_t src_mip { 0 };
        uint32_t dst_mip { 0 };
    };

    struct ResourceUse
    {
        ResourceHandle handle {};
        AccessKind access { AccessKind::Read };
        uint64_t stages { 0 };
    };

    enum class DispatchBindingKind
    {
        StorageBuffer,
        StorageImage
    };

    struct DispatchResourceBinding
    {
        uint32_t set { 0 };
        uint32_t binding { 0 };
        ResourceHandle resource {};
        DispatchBindingKind kind { DispatchBindingKind::StorageBuffer };
        AccessKind access { AccessKind::ReadWrite };
    };

    struct UniformBufferBindingData
    {
        uint32_t set { 0 };
        uint32_t binding { 0 };
        std::vector<std::byte> data;
    };

    struct DispatchDesc
    {
        uint32_t groups_x { 1 };
        uint32_t groups_y { 1 };
        uint32_t groups_z { 1 };
        std::vector<DispatchResourceBinding> bindings;
        std::vector<ResourceUse> resource_uses;
        std::vector<std::byte> push_constant_data;
        std::vector<UniformBufferBindingData> uniform_buffers;
    };

    enum class DrawBindingKind
    {
        SampledImage
    };

    struct DrawResourceBinding
    {
        uint32_t set { 0 };
        uint32_t binding { 0 };
        ResourceHandle resource {};
        DrawBindingKind kind { DrawBindingKind::SampledImage };
        AccessKind access { AccessKind::Read };
    };

    struct RenderingDesc
    {
        ImageRef color {};
        ImageRef depth {};
        uint32_t width { 0 };
        uint32_t height { 0 };
        bool clear_color { false };
        bool clear_depth { false };
    };

    struct DrawIndexedDesc
    {
        ResourceHandle pipeline {};
        uint32_t index_count { 0 };
        uint32_t instance_count { 1 };
        uint32_t first_index { 0 };
        int32_t vertex_offset { 0 };
        uint32_t first_instance { 0 };
        IndexType index_type { IndexType::Auto };
        bool enable_scissor { false };
        ScissorRect scissor {};
        std::vector<DrawResourceBinding> bindings;
        std::vector<ResourceUse> resource_uses;
        std::vector<std::byte> push_constant_data;
        std::vector<UniformBufferBindingData> uniform_buffers;
    };

    struct PresentDesc
    {
        DisplayerRef displayer {};
        ImageRef image {};
        ImageRef swapchain_image {};
        DeviceId present_device {};
        bool allow_cpu_bridge_fallback { true };
    };

    struct CommandPayload
    {
        CopyRegion copy {};
        ImageCopyRegion image_copy {};
        BufferImageCopyRegion buffer_image_copy {};
        DispatchDesc dispatch {};
        RenderingDesc rendering {};
        DrawIndexedDesc draw_indexed {};
        PresentDesc present {};
    };

    struct CommandIR
    {
        CommandOp op { CommandOp::CopyBuffer };
        DeviceMask devices {};
        QueueCapability queue { QueueCapability::Transfer };
        std::vector<ResourceUse> resources;
        std::vector<FeatureRequirement> features;
        CommandPayload payload {};
        std::function<void()> host_callback {};
        std::shared_ptr<void> keep_alive {};
        uint64_t sequence { 0 };
    };

    struct RequirementSet
    {
        bool graphics { false };
        bool compute { false };
        bool transfer { false };
        bool timeline_semaphore { true };
        bool synchronization_2 { true };
        bool deferred_host_operations { false };
        bool device_group { false };
    };

    struct RecordedTask
    {
        std::vector<CommandIR> commands;
        RequirementSet requirements {};

        [[nodiscard]] bool empty() const noexcept { return commands.empty(); }
    };

    struct ResourceBarrier
    {
        std::uintptr_t resource_id { 0 };
        AccessKind before { AccessKind::Read };
        AccessKind after { AccessKind::Read };
    };

    enum class PresentStatus
    {
        None,
        Presented,
        Suboptimal,
        OutOfDate,
        Skipped
    };

    struct PresentResult
    {
        PresentStatus status { PresentStatus::None };
        DisplayerRef displayer {};
        ImageRef image {};
        std::string message;
    };

    struct CrossDeviceDependency
    {
        std::uintptr_t resource_id { 0 };
        DeviceId producer {};
        DeviceId consumer {};
        bool present_cpu_bridge_fallback { false };
        bool imported_timeline_required { false };
    };

    struct SubmissionDependency
    {
        size_t producer { 0 };
        size_t consumer { 0 };
        std::uintptr_t resource_id { 0 };
    };

    class SubmissionKeepAlive
    {
    public:
        void add_resource(std::shared_ptr<const IResourceRef> resource);
        void add_object(std::shared_ptr<void> object);
        void merge(SubmissionKeepAlive&& other);
        void clear() noexcept;

        [[nodiscard]] size_t resource_count() const noexcept { return resources_.size(); }
        [[nodiscard]] size_t object_count() const noexcept { return objects_.size(); }
        [[nodiscard]] bool empty() const noexcept { return resources_.empty() && objects_.empty(); }

    private:
        std::vector<std::shared_ptr<const IResourceRef>> resources_;
        std::vector<std::shared_ptr<void>> objects_;
    };

    struct SubmissionToken
    {
        DeviceId device {};
        QueueId queue {};
        uint64_t value { 0 };
        std::shared_ptr<const SubmissionSync> sync {};

        [[nodiscard]] bool has_sync() const noexcept { return sync != nullptr; }
    };

    struct QueueSubmission
    {
        std::shared_ptr<class TrackedCommandBuffer> command_buffer;
        SubmissionKeepAlive keep_alive;
    };

    struct SubmitReceipt
    {
        uint64_t serial { 0 };
        std::vector<SubmissionToken> tokens;
        std::vector<PresentResult> presents;

        [[nodiscard]] bool empty() const noexcept
        {
            return tokens.empty() && presents.empty();
        }
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
        [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }

    private:
        std::vector<StreamCommand> commands_;
    };

    class CommandRecorder
    {
    public:
        CommandRecorder() = default;

        void copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices = {});
        void copy_image(ImageRef src, ImageRef dst, ImageCopyRegion region, DeviceMask devices = {});
        void copy_to_image(BufferRef src, ImageRef dst, BufferImageCopyRegion region, DeviceMask devices = {});
        void copy_to_buffer(ImageRef src, BufferRef dst, BufferImageCopyRegion region, DeviceMask devices = {});
        void dispatch(ShaderRef shader, DispatchDesc desc, DeviceMask devices = {});
        void begin_rendering(RenderingDesc desc, DeviceMask devices = {});
        void end_rendering(DeviceMask devices = {});
        void draw_indexed(BufferRef index, BufferRef vertex, DrawIndexedDesc desc, DeviceMask devices = {});
        void present(DisplayerRef displayer, ImageRef image, DeviceId present_device = {}, bool allow_cpu_bridge_fallback = true);
        void host_callback(std::function<void()> callback);
        void keep_alive(std::shared_ptr<void> object);
        void require_feature(FeatureRequirement feature);
        [[nodiscard]] RecordedTask close();

    private:
        void ensure_open() const;
        void mark_requirement(QueueCapability capability);
        void mark_requirement(FeatureRequirement feature);
        [[nodiscard]] uint64_t next_sequence() noexcept;
        void mark_device_requirements(DeviceMask devices);

        bool closed_ { false };
        uint64_t next_sequence_ { 0 };
        std::vector<CommandIR> commands_;
        RequirementSet requirements_ {};
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
        [[nodiscard]] std::uintptr_t get_displayer_id() const noexcept { return displayer_.id; }

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
        [[nodiscard]] SubmitReceipt operator<<(CommitCommand command);

        [[nodiscard]] SubmitReceipt commit();
        [[nodiscard]] RecordedTask close_for_tests();

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

        template <typename RecordFn>
        [[nodiscard]] RecordedTask record(RecordFn&& fn) const
        {
            CommandRecorder recorder;
            std::forward<RecordFn>(fn)(recorder);
            return recorder.close();
        }

        [[nodiscard]] HardwareStream stream();
        [[nodiscard]] ExecutionPlan compile(const RecordedTask& task) const;
        [[nodiscard]] std::vector<SubmissionToken> submit(ExecutionPlan& plan, std::vector<PresentResult>* present_results = nullptr) const;
        [[nodiscard]] SubmitReceipt commit(const RecordedTask& task);
        HardwareExecutor& wait(const SubmitReceipt& receipt);
        HardwareExecutor& wait(const HardwareExecutor& producer);
        [[nodiscard]] SubmitReceipt last_receipt() const;

    private:
        [[nodiscard]] std::vector<SubmissionToken> submit(ExecutionPlan& plan,
                                                          std::vector<PresentResult>* present_results,
                                                          std::span<const SubmissionToken> wait_tokens) const;
        [[nodiscard]] std::vector<SubmissionToken> consume_pending_waits();
        void remember_receipt(const SubmitReceipt& receipt);

        std::shared_ptr<ExecutionCompiler> compiler_;
        QueueResolver queue_resolver_ {};
        mutable std::mutex mutex_;
        uint64_t next_submit_serial_ { 0 };
        SubmitReceipt last_receipt_ {};
        std::vector<SubmissionToken> pending_waits_;
    };

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

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBufferDesc uniform(std::string name = {})
        {
            return typed<T>(1, BufferUsageFlags::TransferDst | BufferUsageFlags::Uniform, std::move(name));
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBufferDesc storage(uint64_t count, std::string name = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferSrc | BufferUsageFlags::TransferDst | BufferUsageFlags::Storage, std::move(name));
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

        [[nodiscard]] std::uintptr_t get_buffer_id() const noexcept { return resource_id(); }
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
        [[nodiscard]] std::span<std::byte> get_mapped_bytes() const
        {
            auto* data = static_cast<std::byte*>(get_mapped_data());
            if (data == nullptr)
                return {};

            const uint64_t mapped_size = get_byte_size();
            if (mapped_size > std::numeric_limits<size_t>::max())
                return {};

            return { data, static_cast<size_t>(mapped_size) };
        }

        [[nodiscard]] bool write_bytes(std::span<const std::byte> data, uint64_t byte_offset = 0) const;
        [[nodiscard]] bool read_bytes(std::span<std::byte> output, uint64_t byte_offset = 0) const;

        template <HardwareTransferable T>
        [[nodiscard]] bool write(std::span<const T> data, uint64_t byte_offset = 0) const
        {
            return write_bytes(std::as_bytes(data), byte_offset);
        }

        template <HardwareTransferable T>
        [[nodiscard]] bool write_elements(std::span<const T> data, uint64_t first_element = 0) const
        {
            if (first_element > std::numeric_limits<uint64_t>::max() / sizeof(T))
                return false;

            return write(data, first_element * sizeof(T));
        }

        template <HardwareTransferable T>
        [[nodiscard]] bool write_value(const T& value, uint64_t byte_offset = 0) const
        {
            return write(std::span<const T>(&value, 1), byte_offset);
        }

        template <HardwareTransferable T>
        [[nodiscard]] bool write_element(const T& value, uint64_t element_index = 0) const
        {
            return write_elements(std::span<const T>(&value, 1), element_index);
        }

        template <HardwareTransferable T>
            requires(!std::is_const_v<T>)
        [[nodiscard]] bool read(std::span<T> output, uint64_t byte_offset = 0) const
        {
            return read_bytes(std::as_writable_bytes(output), byte_offset);
        }

        template <HardwareTransferable T>
            requires(!std::is_const_v<T>)
        [[nodiscard]] bool read_elements(std::span<T> output, uint64_t first_element = 0) const
        {
            if (first_element > std::numeric_limits<uint64_t>::max() / sizeof(T))
                return false;

            return read(output, first_element * sizeof(T));
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

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBuffer uniform(const T& value, std::string name = {})
        {
            return HardwareBuffer(HardwareBufferDesc::uniform<T>(std::move(name)), std::as_bytes(std::span<const T>(&value, 1)));
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBuffer storage(std::span<const T> data, std::string name = {})
        {
            return HardwareBuffer(HardwareBufferDesc::storage<T>(data.size(), std::move(name)), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
            requires std::ranges::sized_range<Range> && HardwareTransferable<std::ranges::range_value_t<Range>>
        [[nodiscard]] static HardwareBuffer storage(const Range& data, std::string name = {})
        {
            return storage<std::ranges::range_value_t<Range>>(
                std::span<const std::ranges::range_value_t<Range>>(std::ranges::data(data), std::ranges::size(data)),
                std::move(name));
        }

        [[nodiscard]] CommandBatch upload(std::span<const std::byte> data, uint64_t dst_offset = 0) const;
        [[nodiscard]] CopyBufferCommand copy_to(const HardwareBuffer& dst, BufferRange src = BufferRange::entire(), uint64_t dst_offset = 0) const;
        [[nodiscard]] CopyBufferToImageCommand copy_to(const HardwareImage& dst, uint64_t buffer_offset = 0, uint32_t image_layer = 0, uint32_t image_mip = 0) const;
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

        static HardwareImageDesc texture_3d(uint32_t width,
                                            uint32_t height,
                                            uint32_t depth,
                                            Format format,
                                            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                            std::string name = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Image3D;
            desc.extent = { width, height, depth };
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

        static HardwareImageDesc cube_array(uint32_t size,
                                            uint32_t cube_count,
                                            Format format,
                                            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                            std::string name = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::CubeArray;
            desc.extent = { size, size, 1 };
            desc.array_layers = cube_count * 6;
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc;
        }

        static HardwareImageDesc color_attachment(uint32_t width,
                                                  uint32_t height,
                                                  Format format,
                                                  std::string name = {})
        {
            return texture_2d(width,
                              height,
                              format,
                              ImageUsageFlags::ColorAttachment | ImageUsageFlags::Sampled | ImageUsageFlags::TransferSrc | ImageUsageFlags::TransferDst,
                              std::move(name));
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
        [[nodiscard]] std::uintptr_t get_image_id() const noexcept { return resource_id(); }
        [[nodiscard]] HardwareImageLayerSelector operator[](uint32_t layer) const;
        [[nodiscard]] HardwareImage whole() const;
        [[nodiscard]] HardwareImage layer(uint32_t layer_index) const;
        [[nodiscard]] HardwareImage mip(uint32_t mip_index) const;
        [[nodiscard]] HardwareImage subresource(uint32_t layer_index, uint32_t mip_index) const;
        [[nodiscard]] uint32_t subresource_index(uint32_t layer_index, uint32_t mip_index) const;
        [[nodiscard]] uint32_t subresource_count() const noexcept;
        [[nodiscard]] ImageExtent mip_extent(uint32_t mip_index) const;
        [[nodiscard]] ImageExtent extent() const;
        bool write_subresource_bytes(uint32_t layer_index, uint32_t mip_index, std::span<const std::byte> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;
        bool read_subresource_bytes(uint32_t layer_index, uint32_t mip_index, std::span<std::byte> output, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;
        bool write_bytes(std::span<const std::byte> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;
        bool read_bytes(std::span<std::byte> output, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;

        template <HardwareTransferable T>
        bool write_subresource(uint32_t layer_index, uint32_t mip_index, std::span<const T> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return write_subresource_bytes(layer_index, mip_index, std::as_bytes(data), row_pitch, slice_pitch);
        }

        template <HardwareTransferable T>
            requires(!std::is_const_v<T>)
        bool read_subresource(uint32_t layer_index, uint32_t mip_index, std::span<T> output, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return read_subresource_bytes(layer_index, mip_index, std::as_writable_bytes(output), row_pitch, slice_pitch);
        }

        template <HardwareTransferable T>
        bool write(std::span<const T> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return write_bytes(std::as_bytes(data), row_pitch, slice_pitch);
        }

        template <HardwareTransferable T>
            requires(!std::is_const_v<T>)
        bool read(std::span<T> output, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return read_bytes(std::as_writable_bytes(output), row_pitch, slice_pitch);
        }

        void set_clear_color(float r, float g, float b, float a);
        void set_clear_depth(float depth, uint32_t stencil = 0);

        [[nodiscard]] CommandBatch upload(std::span<const std::byte> data, uint32_t image_layer = 0, uint32_t image_mip = 0) const;

        template <HardwareTransferable T>
        [[nodiscard]] CommandBatch upload(std::span<const T> data, uint32_t image_layer = 0, uint32_t image_mip = 0) const
        {
            return upload(std::as_bytes(data), image_layer, image_mip);
        }

        [[nodiscard]] CopyImageCommand copy_to(const HardwareImage& dst, uint32_t src_layer = 0, uint32_t dst_layer = 0, uint32_t src_mip = 0, uint32_t dst_mip = 0) const;
        [[nodiscard]] CopyImageToBufferCommand copy_to(const HardwareBuffer& dst, uint32_t image_layer = 0, uint32_t image_mip = 0, uint64_t buffer_offset = 0) const;
        [[nodiscard]] CopyBufferToImageCommand copy_from(const HardwareBuffer& src, uint64_t buffer_offset = 0, uint32_t image_layer = 0, uint32_t image_mip = 0) const;
        [[nodiscard]] uint32_t store_descriptor() const;
        [[nodiscard]] uint32_t store_sampled_descriptor() const;
        [[nodiscard]] uint32_t store_storage_descriptor() const;
        static HardwareImage import_external(const ExternalMemoryHandle& handle, const HardwareImageDesc& desc, uint64_t allocation_size = 0);
        [[nodiscard]] ExternalMemoryHandle export_external() const;

    private:
        ImageSubresourceRange range_ = ImageSubresourceRange::whole();

        friend class HardwareImageLayerSelector;
        friend class HardwareBuffer;
    };

    class HardwareImageLayerSelector
    {
    public:
        HardwareImageLayerSelector(const HardwareImage& image, uint32_t layer_index) : image_(image), layer_(layer_index) {}
        [[nodiscard]] HardwareImage operator[](uint32_t mip_index) const;

    private:
        HardwareImage image_;
        uint32_t layer_ = 0;
    };

    inline HardwareImageLayerSelector HardwareImage::operator[](uint32_t layer_index) const
    {
        return HardwareImageLayerSelector(*this, layer_index);
    }

    // ================================================================
    // HardwarePushConstant
    // ================================================================

    /*inline constexpr uint32_t kPortablePushConstantByteSize = 128;

    template <typename T>
    concept HardwarePushConstantValue = std::is_trivially_copyable_v<std::remove_cvref_t<T>> && 
                                        !std::is_pointer_v<std::remove_cvref_t<T>> &&
                                        !std::is_same_v<std::remove_cvref_t<T>, HardwarePushConstant>;

    struct HardwarePushConstantDesc
    {
        uint64_t byte_size = 0;
        std::string debug_name;
    };

    struct HardwarePushConstant
    {
    public:
        static constexpr uint64_t max_byte_size = kPortablePushConstantByteSize;
        static constexpr uint64_t whole_size = std::numeric_limits<uint64_t>::max();

        HardwarePushConstant() = default;
        explicit HardwarePushConstant(HardwarePushConstantDesc desc);
        explicit HardwarePushConstant(uint64_t byte_size);
        ~HardwarePushConstant();

        template <HardwarePushConstantValue T>
        explicit HardwarePushConstant(const T& value)
        {
            assign(value);
        }

        template <HardwarePushConstantValue T>
        HardwarePushConstant& operator=(const T& value)
        {
            assign(value);
            return *this;
        }

        bool write_bytes(std::span<const std::byte> data, uint64_t offset = 0) noexcept;
        bool read_bytes(std::span<std::byte> output, uint64_t offset = 0) const noexcept;

        [[nodiscard]] std::vector<std::byte> snapshot_bytes(uint64_t offset = 0, uint64_t byte_size = whole_size) const;

        template <HardwarePushConstantValue T>
        bool write_value(uint64_t offset, const T &value) noexcept
        {
            return write_bytes(&value, sizeof(T), offset);
        }

        template <HardwarePushConstantValue T>
        bool assign(const T& value)
        {
            if (!reset(sizeof(T)))
                return false;

            return write_value(0, value);
        }

        template <HardwarePushConstantValue T>
        bool assign(const T &value) noexcept
        {
            if (!reset(sizeof(T)))
                return false;

            return write_value(0, value);
        }

    private:
        HardwarePushConstantDesc desc_;
        std::array<std::byte, max_byte_size> storage_{};
    };*/

    // ================================================================
    // Pipeline Descriptors
    // ================================================================

    struct PipelineShaderDesc
    {
        PipelineShaderStage stage;
        EmbeddedShader::ShaderCodeModule module;

        PipelineShaderDesc(PipelineShaderStage stage, EmbeddedShader::ShaderCodeModule module) : stage(stage), module(std::move(module)) {}

        static PipelineShaderDesc from_spirv(PipelineShaderStage stage, std::vector<uint32_t> spirv)
        {
            auto reflection = EmbeddedShader::ShaderLanguageConverter::spirvCrossReflectedBindInfo(spirv, EmbeddedShader::ShaderLanguage::HLSL);
            return PipelineShaderDesc(stage, EmbeddedShader::ShaderCodeModule(std::move(spirv), std::move(reflection)));
        }

        static PipelineShaderDesc from_slang_module(PipelineShaderStage stage,
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
                    throw std::invalid_argument("PipelineShaderDesc::from_slang_module only supports vertex, fragment, and compute stages.");
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
            auto spirv_reflection = EmbeddedShader::ShaderLanguageConverter::spirvCrossReflectedBindInfo(spirv->second, EmbeddedShader::ShaderLanguage::HLSL);
            spirv_reflection.entryPointInfoPool = std::move(resources.entryPointInfoPool);

            return PipelineShaderDesc(stage, EmbeddedShader::ShaderCodeModule(std::move(spirv->second), std::move(spirv_reflection)));
        }

        [[nodiscard]] bool is_compute() const noexcept
        {
            return stage == PipelineShaderStage::Compute;
        }

        [[nodiscard]] bool is_vertex() const noexcept
        {
            return stage == PipelineShaderStage::Vertex;
        }

        [[nodiscard]] bool is_fragment() const noexcept
        {
            return stage == PipelineShaderStage::Fragment;
        }

        [[nodiscard]] bool is_ray_tracing() const noexcept
        {
            switch (stage)
            {
            case PipelineShaderStage::RayGeneration:
            case PipelineShaderStage::Miss:
            case PipelineShaderStage::ClosestHit:
            case PipelineShaderStage::AnyHit:
            case PipelineShaderStage::Intersection:
            case PipelineShaderStage::Callable:
                return true;
            default:
                return false;
            }
        }
    };

    struct EdslPipelineOptions
    {
        EmbeddedShader::CompilerOption compiler;
        bool auto_bind = true;
    };

    struct ComputePipelineDesc
    {
        PipelineShaderDesc compute_shader;
        ktm::uvec3 thread_group_size = { 1, 1, 1 };
        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries;
        std::string debug_name;

        ComputePipelineDesc(PipelineShaderDesc shader, ktm::uvec3 numthreads = { 1, 1, 1 }) : compute_shader(std::move(shader)), thread_group_size(numthreads)
        {
            if (compute_shader.stage != PipelineShaderStage::Compute)
                throw std::invalid_argument("ComputePipelineDesc requires a compute shader.");
        }

        template <typename F>
        static ComputePipelineDesc from_edsl(F&& compute_shader_code, ktm::uvec3 numthreads = { 1, 1, 1 }, EdslPipelineOptions options = {}, std::source_location source_location = std::source_location::current())
        {
            auto object = EmbeddedShader::ComputePipelineObject::compile(std::forward<F>(compute_shader_code), numthreads, options.compiler, source_location);

            ComputePipelineDesc desc(
                PipelineShaderDesc {
                    PipelineShaderStage::Compute,
                    object.compute->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, options.compiler.enableBindless) },
                numthreads);

            if (options.auto_bind)
                desc.auto_bind_entries = std::move(object.autoBindEntries);
            return desc;
        }

        static ComputePipelineDesc from_spirv(std::vector<uint32_t> spirv)
        {
            return ComputePipelineDesc(PipelineShaderDesc::from_spirv(PipelineShaderStage::Compute, std::move(spirv)));
        }

        static ComputePipelineDesc from_source(std::string source,
                                               EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
                                               EmbeddedShader::CompilerOption compiler_option = {},
                                               std::source_location source_location = std::source_location::current())
        {
            EmbeddedShader::ShaderCodeCompiler compiler(source,
                                                        EmbeddedShader::ShaderStage::ComputeShader,
                                                        language,
                                                        compiler_option,
                                                        source_location);

            return ComputePipelineDesc(
                PipelineShaderDesc {
                    PipelineShaderStage::Compute,
                    compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, compiler_option.enableBindless) });
        }

        ComputePipelineDesc& set_thread_group_size(ktm::uvec3 value) noexcept
        {
            thread_group_size = value;
            return *this;
        }

        ComputePipelineDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
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

        BlendStateDesc& set_attachment(uint32_t index, BlendAttachmentDesc desc)
        {
            if (attachments.size() <= index)
                attachments.resize(size_t(index) + 1, alpha_blend_attachment());

            attachments[index] = std::move(desc);
            return *this;
        }

        BlendStateDesc& set_attachment_count(uint32_t count)
        {
            attachments.resize(count, alpha_blend_attachment());
            return *this;
        }

        BlendStateDesc& set_opaque()
        {
            for (auto& attachment : attachments)
                attachment = opaque_attachment();

            return *this;
        }

        BlendStateDesc& set_alpha_blend()
        {
            for (auto& attachment : attachments)
                attachment = alpha_blend_attachment();

            return *this;
        }
    };

    struct RenderTargetLayoutDesc
    {
        std::vector<Format> color_formats;
        Format depth_stencil_format = Format::UNKNOWN;
        uint32_t multiview_count = 1;

        RenderTargetLayoutDesc& add_color_format(Format format)
        {
            color_formats.push_back(format);
            return *this;
        }

        RenderTargetLayoutDesc& set_color_format(uint32_t index, Format format)
        {
            if (color_formats.size() <= index)
                color_formats.resize(size_t(index) + 1, Format::UNKNOWN);

            color_formats[index] = format;
            return *this;
        }

        RenderTargetLayoutDesc& set_depth_stencil_format(Format format) noexcept
        {
            depth_stencil_format = format;
            return *this;
        }

        RenderTargetLayoutDesc& set_multiview_count(uint32_t value) noexcept
        {
            multiview_count = value;
            return *this;
        }
    };

    struct DepthAttachmentDesc
    {
        bool enabled = false;
        Format format = Format::UNKNOWN;
        std::string debug_name;

        static DepthAttachmentDesc with_format(Format value, std::string name = {})
        {
            DepthAttachmentDesc desc;
            desc.enabled = value != Format::UNKNOWN;
            desc.format = value;
            desc.debug_name = std::move(name);
            return desc;
        }

        DepthAttachmentDesc& set_enabled(bool value) noexcept
        {
            enabled = value;
            return *this;
        }

        DepthAttachmentDesc& set_format(Format value) noexcept
        {
            format = value;
            enabled = value != Format::UNKNOWN;
            return *this;
        }

        DepthAttachmentDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
        }
    };

    struct RasterizerPipelineDesc
    {
        PipelineShaderDesc vertex_shader;
        PipelineShaderDesc fragment_shader;

        RasterizerStateDesc rasterizer;
        DepthStencilStateDesc depth_stencil;
        BlendStateDesc blend;
        MultisampleStateDesc multisample;
        DepthAttachmentDesc depth_attachment;

        uint32_t multiview_count = 1;

        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries;
        std::string debug_name;

        RasterizerPipelineDesc(PipelineShaderDesc vertex, PipelineShaderDesc fragment) : vertex_shader(std::move(vertex)), fragment_shader(std::move(fragment))
        {
            if (vertex_shader.stage != PipelineShaderStage::Vertex)
                throw std::invalid_argument("RasterizerPipelineDesc requires a vertex shader.");

            if (fragment_shader.stage != PipelineShaderStage::Fragment)
                throw std::invalid_argument("RasterizerPipelineDesc requires a fragment shader.");
        }

        template <typename VS, typename FS>
        static RasterizerPipelineDesc from_edsl(VS&& vertex_shader_code,
                                                FS&& fragment_shader_code,
                                                EdslPipelineOptions options = {},
                                                std::source_location source_location = std::source_location::current())
        {
            auto object = EmbeddedShader::RasterizedPipelineObject::compile(std::forward<VS>(vertex_shader_code),
                                                                            std::forward<FS>(fragment_shader_code),
                                                                            options.compiler,
                                                                            source_location);

            RasterizerPipelineDesc desc(
                PipelineShaderDesc {
                    PipelineShaderStage::Vertex,
                    object.vertex->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,
                                                 options.compiler.enableBindless) },
                PipelineShaderDesc {
                    PipelineShaderStage::Fragment,
                    object.fragment->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,
                                                   options.compiler.enableBindless) });

            if (options.auto_bind)
                desc.auto_bind_entries = std::move(object.autoBindEntries);

            return desc;
        }

        static RasterizerPipelineDesc from_spirv(std::vector<uint32_t> vertex_spirv, std::vector<uint32_t> fragment_spirv)
        {
            return RasterizerPipelineDesc(PipelineShaderDesc::from_spirv(PipelineShaderStage::Vertex, std::move(vertex_spirv)),
                                          PipelineShaderDesc::from_spirv(PipelineShaderStage::Fragment, std::move(fragment_spirv)));
        }

        static RasterizerPipelineDesc from_source(std::string vertex_source,
                                                  std::string fragment_source,
                                                  EmbeddedShader::ShaderLanguage vertex_language = EmbeddedShader::ShaderLanguage::GLSL,
                                                  EmbeddedShader::ShaderLanguage fragment_language = EmbeddedShader::ShaderLanguage::GLSL,
                                                  EmbeddedShader::CompilerOption compiler_option = {},
                                                  std::source_location source_location = std::source_location::current())
        {
            EmbeddedShader::ShaderCodeCompiler vertex_compiler(vertex_source,
                                                               EmbeddedShader::ShaderStage::VertexShader,
                                                               vertex_language,
                                                               compiler_option,
                                                               source_location);

            EmbeddedShader::ShaderCodeCompiler fragment_compiler(fragment_source,
                                                                 EmbeddedShader::ShaderStage::FragmentShader,
                                                                 fragment_language,
                                                                 compiler_option,
                                                                 source_location);

            return RasterizerPipelineDesc(
                PipelineShaderDesc {
                    PipelineShaderStage::Vertex,
                    vertex_compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, compiler_option.enableBindless) },
                PipelineShaderDesc {
                    PipelineShaderStage::Fragment,
                    fragment_compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, compiler_option.enableBindless) });
        }

        RasterizerPipelineDesc& set_rasterizer(RasterizerStateDesc value) noexcept
        {
            rasterizer = value;
            return *this;
        }

        RasterizerPipelineDesc& set_depth_stencil(DepthStencilStateDesc value) noexcept
        {
            depth_stencil = value;
            if (!value.depth_test_enabled && !value.stencil_test_enabled)
                depth_attachment.enabled = false;
            return *this;
        }

        RasterizerPipelineDesc& set_blend(BlendStateDesc value)
        {
            blend = std::move(value);
            return *this;
        }

        RasterizerPipelineDesc& set_multisample(MultisampleStateDesc value) noexcept
        {
            multisample = value;
            return *this;
        }

        RasterizerPipelineDesc& set_depth_attachment(DepthAttachmentDesc value) noexcept
        {
            depth_attachment = value;
            return *this;
        }

        RasterizerPipelineDesc& set_multiview_count(uint32_t value) noexcept
        {
            multiview_count = std::max(1u, value);
            return *this;
        }

        RasterizerPipelineDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
        }
    };

    struct RayTracingHitGroupDesc
    {
        RayTracingHitGroupKind kind = RayTracingHitGroupKind::Triangles;
        int32_t closest_hit_shader = -1;
        int32_t any_hit_shader = -1;
        int32_t intersection_shader = -1;
        std::string debug_name;

        static RayTracingHitGroupDesc triangles(int32_t closest_hit_shader,
                                                int32_t any_hit_shader = -1)
        {
            RayTracingHitGroupDesc desc;
            desc.kind = RayTracingHitGroupKind::Triangles;
            desc.closest_hit_shader = closest_hit_shader;
            desc.any_hit_shader = any_hit_shader;
            return desc;
        }

        static RayTracingHitGroupDesc procedural(int32_t intersection_shader,
                                                 int32_t closest_hit_shader = -1,
                                                 int32_t any_hit_shader = -1)
        {
            RayTracingHitGroupDesc desc;
            desc.kind = RayTracingHitGroupKind::Procedural;
            desc.intersection_shader = intersection_shader;
            desc.closest_hit_shader = closest_hit_shader;
            desc.any_hit_shader = any_hit_shader;
            return desc;
        }
    };

    struct RayTracingPipelineDesc
    {
        std::vector<PipelineShaderDesc> shaders;
        std::vector<RayTracingHitGroupDesc> hit_groups;

        uint32_t max_recursion_depth = 1;
        uint32_t max_payload_size = 0;
        uint32_t max_attribute_size = 8;

        std::string debug_name;

        uint32_t add_shader(PipelineShaderDesc shader)
        {
            if (!shader.is_ray_tracing())
                throw std::invalid_argument("RayTracingPipelineDesc only accepts ray tracing shader stages.");

            shaders.push_back(std::move(shader));
            return static_cast<uint32_t>(shaders.size() - 1);
        }

        uint32_t add_ray_generation_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::RayGeneration, std::move(spirv)));
        }

        uint32_t add_miss_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Miss, std::move(spirv)));
        }

        uint32_t add_closest_hit_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::ClosestHit, std::move(spirv)));
        }

        uint32_t add_any_hit_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::AnyHit, std::move(spirv)));
        }

        uint32_t add_intersection_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Intersection, std::move(spirv)));
        }

        uint32_t add_callable_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Callable, std::move(spirv)));
        }

        RayTracingPipelineDesc& add_hit_group(RayTracingHitGroupDesc hit_group)
        {
            hit_groups.push_back(std::move(hit_group));
            return *this;
        }

        RayTracingPipelineDesc& set_max_recursion_depth(uint32_t value) noexcept
        {
            max_recursion_depth = value == 0 ? 1 : value;
            return *this;
        }

        RayTracingPipelineDesc& set_max_payload_size(uint32_t value) noexcept
        {
            max_payload_size = value;
            return *this;
        }

        RayTracingPipelineDesc& set_max_attribute_size(uint32_t value) noexcept
        {
            max_attribute_size = value;
            return *this;
        }

        RayTracingPipelineDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
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
        /*virtual void bindResource(BindingSlot &, const TopLevelAccelerationStructure &)
        {
            throw std::runtime_error("This pipeline does not support acceleration structure binding.");
        }*/
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
            /*else if constexpr (std::same_as<std::remove_cvref_t<T>, TopLevelAccelerationStructure>)
            {
                pipeline_.bindResource(slot_, value);
            }*/
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

    class ComputePipeline : public ResourceHandle, public PipelineBindingScope, public ReflectedPipelineBindings<ComputePipeline>
    {
    public:
        ComputePipeline();
        explicit ComputePipeline(ComputePipelineDesc desc, const std::source_location& source_location = std::source_location::current());

        ComputePipeline(const ComputePipeline& other);
        ComputePipeline(ComputePipeline&& other) noexcept;
        ~ComputePipeline();

        ComputePipeline& operator=(const ComputePipeline& other);
        ComputePipeline& operator=(ComputePipeline&& other) noexcept;
        ComputePipeline& operator()(uint16_t x, uint16_t y, uint16_t z);
        ComputePipeline& bind_storage_buffer(uint32_t binding, const HardwareBuffer& buffer);
        ComputePipeline& bind_storage_image(uint32_t binding, const HardwareImage& image);
        [[nodiscard]] CommandBatch command_batch() const;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] std::uintptr_t get_compute_pipeline_id() const noexcept { return resource_id(); }

    private:
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

    class RasterizerPipeline : public ResourceHandle, public PipelineBindingScope, public ReflectedPipelineBindings<RasterizerPipeline>
    {
    public:
        RasterizerPipeline();
        explicit RasterizerPipeline(RasterizerPipelineDesc desc, const std::source_location& source_location = std::source_location::current());

        RasterizerPipeline(const RasterizerPipeline& other);
        RasterizerPipeline(RasterizerPipeline&& other) noexcept;
        ~RasterizerPipeline();

        RasterizerPipeline& operator=(const RasterizerPipeline& other);
        RasterizerPipeline& operator=(RasterizerPipeline&& other) noexcept;

        RasterizerPipeline& operator()(uint16_t width, uint16_t height);
        RasterizerPipeline& record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer);
        RasterizerPipeline& record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
        RasterizerPipeline& clear_records();
        RasterizerPipeline& bind_render_target(uint32_t location, HardwareImage& image);
        RasterizerPipeline& bind_depth_target(HardwareImage& image);
        [[nodiscard]] CommandBatch command_batch() const;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] std::uintptr_t get_rasterizer_pipeline_id() const noexcept { return resource_id(); }

        template <typename TargetProxy>
            requires requires(TargetProxy& proxy) { proxy.boundResource_; }
        RasterizerPipeline& bind_render_target(uint32_t location, TargetProxy& proxy)
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

        template <typename... TargetProxies>
        RasterizerPipeline& bind_output_targets(TargetProxies&... targets)
        {
            uint32_t location = 0;
            (bind_render_target(location++, targets), ...);
            return *this;
        }

    private:
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

    class RayTracingPipeline : public PipelineBindingScope, public ReflectedPipelineBindings<RayTracingPipeline>
    {
    };

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

    struct CopyBufferCommand
    {
        BufferRef src {};
        BufferRef dst {};
        CopyRegion region {};
        DeviceMask devices {};

        [[nodiscard]] BufferRef source() const noexcept { return src; }
        [[nodiscard]] BufferRef destination() const noexcept { return dst; }
        [[nodiscard]] CopyRegion copy_region() const noexcept { return region; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.copy(src, dst, region, devices);
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

    struct CopyImageCommand
    {
        ImageRef src {};
        ImageRef dst {};
        ImageCopyRegion region {};
        DeviceMask devices {};

        [[nodiscard]] ImageRef source() const noexcept { return src; }
        [[nodiscard]] ImageRef destination() const noexcept { return dst; }
        [[nodiscard]] ImageCopyRegion copy_region() const noexcept { return region; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.copy_image(src, dst, region, devices);
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

    struct CopyBufferToImageCommand
    {
        BufferRef src {};
        ImageRef dst {};
        BufferImageCopyRegion region {};
        DeviceMask devices {};

        [[nodiscard]] BufferRef source() const noexcept { return src; }
        [[nodiscard]] ImageRef destination() const noexcept { return dst; }
        [[nodiscard]] BufferImageCopyRegion copy_region() const noexcept { return region; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

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

    struct CopyImageToBufferCommand
    {
        ImageRef src {};
        BufferRef dst {};
        BufferImageCopyRegion region {};
        DeviceMask devices {};

        [[nodiscard]] ImageRef source() const noexcept { return src; }
        [[nodiscard]] BufferRef destination() const noexcept { return dst; }
        [[nodiscard]] BufferImageCopyRegion copy_region() const noexcept { return region; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.copy_to_buffer(src, dst, region, devices);
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
        ShaderRef shader {};
        DispatchDesc dispatch {};
        DeviceMask devices {};

        [[nodiscard]] ShaderRef shader_ref() const noexcept { return shader; }
        [[nodiscard]] DispatchDesc dispatch_desc() const noexcept { return dispatch; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

        void record(CommandRecorder& recorder) const
        {
            recorder.dispatch(shader, dispatch, devices);
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

        [[nodiscard]] RenderingDesc rendering_desc() const noexcept { return rendering; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

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

        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

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

        [[nodiscard]] BufferRef index_buffer() const noexcept { return index; }
        [[nodiscard]] BufferRef vertex_buffer() const noexcept { return vertex; }
        [[nodiscard]] DrawIndexedDesc draw_desc() const noexcept { return draw; }
        [[nodiscard]] DeviceMask device_mask() const noexcept { return devices; }

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

    struct PresentCommand
    {
        DisplayerRef displayer {};
        ImageRef image {};
        DeviceId present_device {};
        bool allow_cpu_bridge_fallback { true };

        [[nodiscard]] DisplayerRef displayer_ref() const noexcept { return displayer; }
        [[nodiscard]] ImageRef image_ref() const noexcept { return image; }
        [[nodiscard]] DeviceId device() const noexcept { return present_device; }
        [[nodiscard]] bool allow_fallback() const noexcept { return allow_cpu_bridge_fallback; }

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

    class HostFunctionCommand
    {
    public:
        HostFunctionCommand() = default;

        explicit HostFunctionCommand(std::function<void()> callback)
            : callback_(std::move(callback))
        {
        }

        [[nodiscard]] const std::function<void()>& callback() const noexcept { return callback_; }

        void record(CommandRecorder& recorder) const
        {
            recorder.host_callback(callback_);
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
        std::function<void()> callback_ {};
    };

    class KeepAliveCommand
    {
    public:
        KeepAliveCommand() = default;

        explicit KeepAliveCommand(std::shared_ptr<void> object)
            : object_(std::move(object))
        {
        }

        [[nodiscard]] const std::shared_ptr<void>& object() const noexcept { return object_; }

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

    [[nodiscard]] inline CopyBufferCommand copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices = {})
    {
        return { src, dst, region, devices };
    }

    [[nodiscard]] inline CopyImageCommand copy_image(ImageRef src, ImageRef dst, ImageCopyRegion region, DeviceMask devices = {})
    {
        return { src, dst, region, devices };
    }

    [[nodiscard]] inline CopyBufferToImageCommand copy_to_image(BufferRef src, ImageRef dst, BufferImageCopyRegion region, DeviceMask devices = {})
    {
        return { src, dst, region, devices };
    }

    [[nodiscard]] inline CopyImageToBufferCommand copy_to_buffer(ImageRef src, BufferRef dst, BufferImageCopyRegion region, DeviceMask devices = {})
    {
        return { src, dst, region, devices };
    }

    [[nodiscard]] inline ShaderDispatchCommand dispatch(ShaderRef shader, DispatchDesc desc, DeviceMask devices = {})
    {
        return { shader, std::move(desc), devices };
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

    [[nodiscard]] inline HostFunctionCommand host_callback(std::function<void()> callback)
    {
        return HostFunctionCommand(std::move(callback));
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
    Corona::Horizon::BindingSlot slot;
    slot.byte_offset = byteOffset;
    slot.type_size = typeSize;
    slot.bind_type = bindType;
    slot.location = location;
    slot.binding = location;

    Corona::Horizon::ResourceProxy proxy(*pipeline_, slot);
    proxy = value;
    return *this;
}
