#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volk.h>

#include "resource.h"

namespace Corona::Horizon
{
    class Queue;
    class TrackedCommandBuffer;

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
        Dispatch,
        BeginRendering,
        EndRendering,
        DrawIndexed,
        Present,
        HostCallback,
        KeepAlive
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

    struct DispatchDesc
    {
        uint32_t groups_x { 1 };
        uint32_t groups_y { 1 };
        uint32_t groups_z { 1 };
    };

    struct RenderingDesc
    {
        ImageRef color {};
        ImageRef depth {};
        uint32_t width { 0 };
        uint32_t height { 0 };
    };

    struct DrawIndexedDesc
    {
        uint32_t index_count { 0 };
        uint32_t instance_count { 1 };
        uint32_t first_index { 0 };
        int32_t vertex_offset { 0 };
        uint32_t first_instance { 0 };
    };

    struct PresentDesc
    {
        DisplayerRef displayer {};
        ImageRef image {};
        DeviceId present_device {};
        bool allow_cpu_bridge_fallback { true };
    };

    struct ResourceUse
    {
        ResourceHandle handle {};
        AccessKind access { AccessKind::Read };
        uint64_t stages { 0 };
    };

    struct CommandPayload
    {
        CopyRegion copy {};
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

    struct SubmitWait
    {
        VkSemaphore semaphore { VK_NULL_HANDLE };
        uint64_t value { 0 };
        VkPipelineStageFlags2 stages { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
    };

    struct SubmitSignal
    {
        VkSemaphore semaphore { VK_NULL_HANDLE };
        uint64_t value { 0 };
        VkPipelineStageFlags2 stages { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
    };

    struct CompiledSubmission
    {
        DeviceId device {};
        QueueCapability queue { QueueCapability::Transfer };
        std::vector<CommandIR> commands;
        std::vector<ResourceBarrier> barriers;
        std::vector<SubmitWait> waits;
        std::vector<SubmitSignal> signals;
        std::vector<PresentDesc> presents;
        std::vector<std::function<void()>> host_callbacks;
        SubmissionKeepAlive keep_alive;
        std::shared_ptr<TrackedCommandBuffer> command_buffer;
    };

    struct ExecutionPlan
    {
        std::vector<CompiledSubmission> submissions;
        std::vector<SubmissionDependency> dependencies;
        std::vector<CrossDeviceDependency> cross_device_dependencies;

        [[nodiscard]] bool empty() const noexcept { return submissions.empty(); }
    };

    struct SubmissionToken
    {
        DeviceId device {};
        QueueId queue {};
        uint64_t value { 0 };
        VkSemaphore timeline { VK_NULL_HANDLE };
    };

    struct QueueSubmission
    {
        std::shared_ptr<TrackedCommandBuffer> command_buffer;
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
        explicit StreamCommand(std::function<void(class CommandRecorder&)> recorder);

        void record(class CommandRecorder& recorder) const;
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(recorder_); }

    private:
        std::function<void(class CommandRecorder&)> recorder_ {};
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
        void copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices = {});
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

    [[nodiscard]] StreamCommand copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices = {});
    [[nodiscard]] StreamCommand dispatch(ShaderRef shader, DispatchDesc desc, DeviceMask devices = {});
    [[nodiscard]] StreamCommand begin_rendering(RenderingDesc desc, DeviceMask devices = {});
    [[nodiscard]] StreamCommand end_rendering(DeviceMask devices = {});
    [[nodiscard]] StreamCommand draw_indexed(BufferRef index, BufferRef vertex, DrawIndexedDesc desc, DeviceMask devices = {});
    [[nodiscard]] StreamCommand present(DisplayerRef displayer, ImageRef image, DeviceId present_device = {}, bool allow_cpu_bridge_fallback = true);
    [[nodiscard]] StreamCommand host_callback(std::function<void()> callback);
    [[nodiscard]] StreamCommand keep_alive(std::shared_ptr<void> object);

    template <typename T>
    [[nodiscard]] StreamCommand keep_alive(std::shared_ptr<T> object)
    {
        return keep_alive(std::static_pointer_cast<void>(std::move(object)));
    }

    class ExecutionCompiler
    {
    public:
        [[nodiscard]] ExecutionPlan compile(const RecordedTask& task) const;

    private:
        static void collect_keep_alive(CompiledSubmission& submission, const CommandIR& command);
        static void collect_barriers(CompiledSubmission& submission, const CommandIR& command);
    };

    class VulkanCommandEncoder
    {
    public:
        void encode(CompiledSubmission& submission) const;
    };

    class CrossDeviceSync
    {
    public:
        void remember_imported_timeline(DeviceId local_device, VkSemaphore foreign, VkSemaphore imported);
        [[nodiscard]] VkSemaphore resolve_imported_timeline(DeviceId local_device, VkSemaphore foreign) const noexcept;

    private:
        struct ImportedTimeline
        {
            DeviceId local_device {};
            VkSemaphore foreign { VK_NULL_HANDLE };
            VkSemaphore imported { VK_NULL_HANDLE };
        };

        std::vector<ImportedTimeline> imported_timelines_;
    };

    class HardwareStream
    {
    public:
        explicit HardwareStream(class HardwareExecutor& executor);

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

        class HardwareExecutor* executor_ {};
        CommandRecorder recorder_ {};
        bool committed_ { false };
    };

    class HardwareExecutor
    {
    public:
        using QueueResolver = std::function<Queue&(DeviceId device, QueueCapability capability)>;

        HardwareExecutor() = default;
        explicit HardwareExecutor(QueueResolver queue_resolver);

        template <typename RecordFn>
        [[nodiscard]] RecordedTask record(RecordFn&& fn) const
        {
            CommandRecorder recorder;
            std::forward<RecordFn>(fn)(recorder);
            return recorder.close();
        }

        [[nodiscard]] HardwareStream stream();
        [[nodiscard]] ExecutionPlan compile(const RecordedTask& task) const;
        [[nodiscard]] std::vector<SubmissionToken> submit(ExecutionPlan& plan) const;
        [[nodiscard]] SubmitReceipt commit(const RecordedTask& task);

    private:
        ExecutionCompiler compiler_ {};
        QueueResolver queue_resolver_ {};
        uint64_t next_submit_serial_ { 0 };
    };
}
