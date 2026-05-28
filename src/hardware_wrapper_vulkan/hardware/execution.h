#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
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
        Transfer
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
        Dispatch
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
    };

    struct CommandIR
    {
        CommandOp op { CommandOp::CopyBuffer };
        DeviceMask devices {};
        QueueCapability queue { QueueCapability::Transfer };
        std::vector<ResourceUse> resources;
        std::vector<FeatureRequirement> features;
        CommandPayload payload {};
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

    struct CompiledSubmission
    {
        DeviceId device {};
        QueueCapability queue { QueueCapability::Transfer };
        std::vector<CommandIR> commands;
        std::vector<ResourceBarrier> barriers;
        SubmissionKeepAlive keep_alive;
        std::shared_ptr<TrackedCommandBuffer> command_buffer;
    };

    struct ExecutionPlan
    {
        std::vector<CompiledSubmission> submissions;

        [[nodiscard]] bool empty() const noexcept { return submissions.empty(); }
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

    class CommandRecorder
    {
    public:
        void copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices = {});
        void dispatch(ShaderRef shader, DispatchDesc desc, DeviceMask devices = {});
        void require_feature(FeatureRequirement feature);
        [[nodiscard]] RecordedTask close();

    private:
        void ensure_open() const;
        void mark_requirement(QueueCapability capability);
        void mark_requirement(FeatureRequirement feature);

        bool closed_ { false };
        std::vector<CommandIR> commands_;
        RequirementSet requirements_ {};
    };

    class ExecutionCompiler
    {
    public:
        [[nodiscard]] ExecutionPlan compile(const RecordedTask& task) const;

    private:
        static void collect_keep_alive(CompiledSubmission& submission, const CommandIR& command);
        static void collect_barriers(CompiledSubmission& submission, const CommandIR& command);
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

        [[nodiscard]] ExecutionPlan compile(const RecordedTask& task) const;
        [[nodiscard]] std::vector<SubmissionToken> submit(ExecutionPlan& plan) const;

    private:
        ExecutionCompiler compiler_ {};
        QueueResolver queue_resolver_ {};
    };
}
