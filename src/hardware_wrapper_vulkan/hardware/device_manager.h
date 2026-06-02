#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include <volk.h>

#include "features_chain.h"
#include "hardware_wrapper_vulkan/hardware/execution.h"

namespace Corona::Horizon
{
    struct QueueFamilyInfo
    {
        uint32_t family_index { 0 };
        uint32_t queue_count { 0 };
        VkQueueFlags flags { 0 };
    };

    class TrackedCommandBuffer
    {
    public:
        TrackedCommandBuffer() = default;
        TrackedCommandBuffer(VkDevice device, uint32_t queue_family_index);
        ~TrackedCommandBuffer();

        TrackedCommandBuffer(const TrackedCommandBuffer&) = delete;
        TrackedCommandBuffer& operator=(const TrackedCommandBuffer&) = delete;
        TrackedCommandBuffer(TrackedCommandBuffer&&) = delete;
        TrackedCommandBuffer& operator=(TrackedCommandBuffer&&) = delete;

        [[nodiscard]] VkCommandBuffer vk() const noexcept { return command_buffer_; }
        [[nodiscard]] VkCommandPool pool() const noexcept { return command_pool_; }
        [[nodiscard]] uint64_t recording_id() const noexcept { return recording_id_; }
        [[nodiscard]] uint64_t submission_id() const noexcept { return submission_id_; }
        [[nodiscard]] const SubmissionKeepAlive& keep_alive() const noexcept { return keep_alive_; }

        void reset_for_recording(uint64_t recording_id);
        void mark_submitted(uint64_t submission_id, SubmissionKeepAlive keep_alive);
        void retire() noexcept;

    private:
        VkDevice device_ { VK_NULL_HANDLE };
        VkCommandPool command_pool_ { VK_NULL_HANDLE };
        VkCommandBuffer command_buffer_ { VK_NULL_HANDLE };
        uint64_t recording_id_ { 0 };
        uint64_t submission_id_ { 0 };
        SubmissionKeepAlive keep_alive_ {};
    };

    class Queue
    {
    public:
        Queue(DeviceId device_id, QueueCapability capability);
        Queue(VkDevice device, VkQueue queue, uint32_t queue_family_index, uint32_t queue_index, DeviceId device_id, QueueCapability capability);
        ~Queue();

        Queue(const Queue&) = delete;
        Queue& operator=(const Queue&) = delete;
        Queue(Queue&&) = delete;
        Queue& operator=(Queue&&) = delete;

        [[nodiscard]] std::shared_ptr<TrackedCommandBuffer> acquire();
        [[nodiscard]] SubmissionToken submit(QueueSubmission& submission, std::span<const SubmitWait> waits, std::span<const SubmitSignal> signals);
        void retire_completed();

        [[nodiscard]] uint64_t completed_value() const;
        [[nodiscard]] uint64_t last_submitted_value() const noexcept;
        [[nodiscard]] QueueId id() const noexcept { return id_; }
        [[nodiscard]] VkDevice device() const noexcept { return device_; }
        [[nodiscard]] VkQueue vk_queue() const noexcept { return queue_; }
        [[nodiscard]] VkSemaphore timeline() const noexcept { return timeline_; }
        [[nodiscard]] bool is_fake() const noexcept { return device_ == VK_NULL_HANDLE; }
        [[nodiscard]] size_t in_flight_count() const;
        [[nodiscard]] size_t pooled_count() const;

        void mark_completed_for_tests(uint64_t value) noexcept;
        void fail_next_submit_for_tests() noexcept;

    private:
        [[nodiscard]] uint64_t query_completed_value() const;
        void create_timeline_semaphore();

        mutable std::mutex mutex_;
        VkDevice device_ { VK_NULL_HANDLE };
        VkQueue queue_ { VK_NULL_HANDLE };
        VkSemaphore timeline_ { VK_NULL_HANDLE };
        QueueId id_ {};

        uint64_t next_recording_id_ { 0 };
        uint64_t last_submitted_value_ { 0 };
        mutable std::atomic<uint64_t> last_completed_value_ { 0 };
        bool fail_next_submit_ { false };

        std::deque<std::shared_ptr<TrackedCommandBuffer>> in_flight_;
        std::deque<std::shared_ptr<TrackedCommandBuffer>> pool_;
    };

    class DeviceManager
    {
    public:
        DeviceManager() = default;
        ~DeviceManager();

        DeviceManager(const DeviceManager&) = delete;
        DeviceManager& operator=(const DeviceManager&) = delete;
        DeviceManager(DeviceManager&&) = delete;
        DeviceManager& operator=(DeviceManager&&) = delete;

        void initialize(const HardwareCreateConfig& config, VkInstance instance, VkPhysicalDevice physical_device);
        void shutdown() noexcept;

        [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return physical_device_; }
        [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
        [[nodiscard]] VkDevice logical_device() const noexcept { return logical_device_; }
        [[nodiscard]] const VkPhysicalDeviceProperties2& properties() const noexcept { return properties_; }
        [[nodiscard]] const DeviceFeaturesChain& enabled_features() const noexcept { return enabled_features_; }
        [[nodiscard]] const std::vector<QueueFamilyInfo>& queue_families() const noexcept { return queue_families_; }

        [[nodiscard]] Queue* queue_for(QueueCapability capability) noexcept;
        [[nodiscard]] const Queue* queue_for(QueueCapability capability) const noexcept;
        [[nodiscard]] const std::vector<Queue*>& queues_for(QueueCapability capability) const noexcept;

    private:
        void create_device(const HardwareCreateConfig& config);
        void create_queues();

        VkInstance instance_ { VK_NULL_HANDLE };
        VkPhysicalDevice physical_device_ { VK_NULL_HANDLE };
        VkDevice logical_device_ { VK_NULL_HANDLE };
        VkPhysicalDeviceProperties2 properties_ {};
        DeviceFeaturesChain enabled_features_ {};

        std::vector<QueueFamilyInfo> queue_families_;
        std::vector<std::unique_ptr<Queue>> queues_;
        std::vector<Queue*> graphics_queues_;
        std::vector<Queue*> compute_queues_;
        std::vector<Queue*> transfer_queues_;
        //std::vector<Queue*> present_queues_;
    };
}
