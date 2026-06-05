#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <volk.h>

#include "horizon.h"

namespace Corona::Horizon
{
    class TrackedCommandBuffer;

    class SubmissionSync
    {
    public:
        [[nodiscard]] VkSemaphore timeline() const noexcept { return timeline_; }

    private:
        friend class Queue;

        explicit SubmissionSync(VkSemaphore timeline) noexcept;
        [[nodiscard]] static std::shared_ptr<const SubmissionSync> make(VkSemaphore timeline);

        VkSemaphore timeline_ { VK_NULL_HANDLE };
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
        VulkanCommandEncoder() = default;
        explicit VulkanCommandEncoder(VkDevice device);

        void encode(CompiledSubmission& submission) const;

    private:
        VkDevice device_ { VK_NULL_HANDLE };
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
}

#include "hardware_wrapper_vulkan/hardware/command.h"
