#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <volk.h>

#include "horizon.h"
#include "execution_profile.h"

namespace Corona::Horizon
{
    class TrackedCommandBuffer;

    class SubmissionSync
    {
    public:
        [[nodiscard]] VkSemaphore timeline() const noexcept { return timeline_.load(std::memory_order_acquire); }

    private:
        friend class Queue;

        explicit SubmissionSync(VkSemaphore timeline) noexcept;
        [[nodiscard]] static std::shared_ptr<SubmissionSync> make(VkSemaphore timeline);
        void invalidate() noexcept { timeline_.store(VK_NULL_HANDLE, std::memory_order_release); }

        std::atomic<VkSemaphore> timeline_ { VK_NULL_HANDLE };
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
        SubmissionKeepAlive keep_alive;
        std::shared_ptr<TrackedCommandBuffer> command_buffer;
    };

    struct ExecutionPlan
    {
        std::vector<CompiledSubmission> submissions;
        std::vector<SubmissionDependency> dependencies;
        std::vector<CrossDeviceDependency> cross_device_dependencies;

    };

    class ExecutionCompiler
    {
    public:
        [[nodiscard]] ExecutionPlan compile(RecordedTask&& task, ExecutionCommitProfileSample* profile = nullptr) const;

    private:
        [[nodiscard]] ExecutionPlan compile_owned(RecordedTask& task, ExecutionCommitProfileSample* profile) const;
        static void collect_keep_alive(CompiledSubmission& submission, const CommandIR& command);
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

}

#include "hardware_wrapper_vulkan/hardware/command.h"
