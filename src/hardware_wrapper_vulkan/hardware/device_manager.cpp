#include "device_manager.h"
#include "execution_profile.h"
#include "horizon_profiling.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "hardware_wrapper/diagnostics.h"

namespace Corona::Horizon
{
    // ================================================================
    // Helper
    // ================================================================

    void name_vulkan_object(VkDevice device, VkObjectType object_type, uint64_t object_handle, const std::string& name) noexcept
    {
        if (vkSetDebugUtilsObjectNameEXT == nullptr || device == VK_NULL_HANDLE || object_handle == 0 || name.empty())
        {
            return;
        }

        VkDebugUtilsObjectNameInfoEXT name_info {};
        name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        name_info.objectType = object_type;
        name_info.objectHandle = object_handle;
        name_info.pObjectName = name.c_str();
        (void)vkSetDebugUtilsObjectNameEXT(device, &name_info);
    }

    [[nodiscard]] const char* capability_name(QueueCapability capability) noexcept
    {
        switch (capability)
        {
        case QueueCapability::Graphics:
            return "graphics";
        case QueueCapability::Compute:
            return "compute";
        case QueueCapability::Transfer:
            return "transfer";
        case QueueCapability::Present:
            return "present";
        }

        return "unknown";
    }

    [[nodiscard]] bool trace_timeline_enabled() noexcept
    {
        static const bool enabled = [] {
            const char* value = std::getenv("HORIZON_TRACE_TIMELINE");
            return value != nullptr && value[0] != '\0' && value[0] != '0';
        }();
        return enabled;
    }

    [[nodiscard]] bool trace_timeline_full() noexcept
    {
        static const bool full = [] {
            const char* value = std::getenv("HORIZON_TRACE_TIMELINE");
            return value != nullptr && (std::strcmp(value, "full") == 0 || std::strcmp(value, "2") == 0);
        }();
        return full;
    }

    [[nodiscard]] std::string hex_handle(uint64_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << value;
        return stream.str();
    }

    void trace_timeline_event(const char* event,
                              const QueueId& id,
                              VkDevice device,
                              VkQueue queue,
                              VkSemaphore timeline,
                              uint64_t value,
                              uint64_t last_completed,
                              size_t in_flight = 0,
                              size_t pooled = 0) noexcept
    {
        if (!trace_timeline_enabled())
        {
            return;
        }

        try
        {
            std::ostringstream stream;
            stream << event
                   << " thread=" << std::this_thread::get_id()
                   << " device_id=" << id.device.value
                   << " family=" << id.family_index
                   << " queue_index=" << id.queue_index
                   << " capability=" << capability_name(id.capability)
                   << " device=" << hex_handle(reinterpret_cast<uint64_t>(device))
                   << " queue=" << hex_handle(reinterpret_cast<uint64_t>(queue))
                   << " timeline=" << hex_handle(reinterpret_cast<uint64_t>(timeline))
                   << " value=" << value
                   << " last_completed=" << last_completed
                   << " in_flight=" << in_flight
                   << " pooled=" << pooled;
            Diagnostics::write(Diagnostics::Level::Info, "HORIZON TIMELINE", stream.str());
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] bool should_trace_timeline_query() noexcept
    {
        static std::atomic<uint64_t> query_count { 0 };
        const uint64_t count = query_count.fetch_add(1, std::memory_order_relaxed);
        return trace_timeline_full() || count < 128 || (count % 1024) == 0;
    }

    [[nodiscard]] std::vector<const char*> filter_supported_device_extensions(VkPhysicalDevice physical_device, const std::set<const char*>& requested)
    {
        uint32_t extension_count = 0;
        VkResult result = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr);
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkEnumerateDeviceExtensionProperties(count) failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkEnumerateDeviceExtensionProperties failed. VkResult=" + std::to_string(static_cast<int>(result)));
        }

        std::vector<VkExtensionProperties> available(extension_count);
        result = vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, available.data());
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkEnumerateDeviceExtensionProperties(data) failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkEnumerateDeviceExtensionProperties failed. VkResult=" + std::to_string(static_cast<int>(result)));
        }

        const auto contains_extension = [&available](const char* name) {
            return std::any_of(available.begin(), available.end(), [name](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        };

        std::vector<const char*> enabled;
        enabled.reserve(requested.size());

        for (const char* extension : requested)
        {
            if (contains_extension(extension))
            {
                enabled.push_back(extension);
            }
        }

        return enabled;
    }

    [[nodiscard]] VkQueueFlags capability_flag(QueueCapability capability) noexcept
    {
        switch (capability)
        {
        case QueueCapability::Graphics:
        case QueueCapability::Present:
            return VK_QUEUE_GRAPHICS_BIT;
        case QueueCapability::Compute:
            return VK_QUEUE_COMPUTE_BIT;
        case QueueCapability::Transfer:
            return VK_QUEUE_TRANSFER_BIT;
        }

        return 0;
    }

    void add_queue_if_supported(std::vector<Queue*>& queues, Queue& queue, VkQueueFlags flags, QueueCapability capability)
    {
        if ((flags & capability_flag(capability)) != 0)
        {
            queues.push_back(&queue);
        }
    }

    [[nodiscard]] uint64_t thread_queue_seed() noexcept
    {
        static std::atomic<uint64_t> next_seed { 0 };
        thread_local const uint64_t seed = next_seed.fetch_add(1, std::memory_order_relaxed);
        return seed;
    }

    [[nodiscard]] Queue* pick_thread_queue(const std::vector<Queue*>& queues) noexcept
    {
        if (queues.empty())
        {
            return nullptr;
        }

        const uint32_t family_index = queues.front()->id().family_index;
        const auto family_end = std::find_if(queues.begin(), queues.end(), [family_index](const Queue* queue) {
            return queue == nullptr || queue->id().family_index != family_index;
        });
        const size_t family_queue_count = static_cast<size_t>(std::distance(queues.begin(), family_end));
        if (family_queue_count == 0)
        {
            return nullptr;
        }

        return queues[static_cast<size_t>(thread_queue_seed() % family_queue_count)];
    }

    [[nodiscard]] uint32_t requested_queue_count(const QueueFamilyInfo& family) noexcept
    {
        constexpr uint32_t max_queues_per_family = 4;
        return std::max(1u, std::min(family.queue_count, max_queues_per_family));
    }

    // ================================================================
    // TrackedCommandBuffer
    // ================================================================

    TrackedCommandBuffer::TrackedCommandBuffer(VkDevice device, uint32_t queue_family_index) : device_(device)
    {
        if (device_ == VK_NULL_HANDLE)
        {
            return;
        }

        VkCommandPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = queue_family_index;

        VkResult result = vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_);
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkCreateCommandPool failed. queue_family_index=" +
                                   std::to_string(queue_family_index) +
                                   ", VkResult=" +
                                   std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkCreateCommandPool failed. queue_family_index=" +
                                     std::to_string(queue_family_index) +
                                     ", VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }

        VkCommandBufferAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer_);
        if (result != VK_SUCCESS)
        {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;

            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkAllocateCommandBuffers failed. queue_family_index=" +
                                   std::to_string(queue_family_index) +
                                   ", VkResult=" +
                                   std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkAllocateCommandBuffers failed. queue_family_index=" +
                                     std::to_string(queue_family_index) +
                                     ", VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }

        const std::string name_prefix = "horizon.command.family." + std::to_string(queue_family_index);
        name_vulkan_object(device_,
                           VK_OBJECT_TYPE_COMMAND_POOL,
                           reinterpret_cast<uint64_t>(command_pool_),
                           name_prefix + ".pool");
        name_vulkan_object(device_,
                           VK_OBJECT_TYPE_COMMAND_BUFFER,
                           reinterpret_cast<uint64_t>(command_buffer_),
                           name_prefix + ".buffer");
    }

    TrackedCommandBuffer::~TrackedCommandBuffer()
    {
        keep_alive_.clear();

        if (device_ != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
        }
    }

    void TrackedCommandBuffer::reset_for_recording(uint64_t recording_id)
    {
        keep_alive_.clear();
        debug_summary_.clear();
        recording_id_ = recording_id;
        submission_id_ = 0;

        if (command_buffer_ != VK_NULL_HANDLE)
        {
            VkResult result = vkResetCommandBuffer(command_buffer_, 0);
            if (result != VK_SUCCESS)
            {
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   "vkResetCommandBuffer failed. recording_id=" +
                                       std::to_string(recording_id_) +
                                       ", VkResult=" +
                                       std::to_string(static_cast<int>(result)));
                throw std::runtime_error("vkResetCommandBuffer failed. recording_id=" +
                                         std::to_string(recording_id_) +
                                         ", VkResult=" +
                                         std::to_string(static_cast<int>(result)));
            }
        }
    }

    void TrackedCommandBuffer::mark_submitted(uint64_t submission_id, SubmissionKeepAlive keep_alive, std::string debug_summary)
    {
        submission_id_ = submission_id;
        keep_alive_ = std::move(keep_alive);
        debug_summary_ = std::move(debug_summary);
    }

    void TrackedCommandBuffer::retire() noexcept
    {
        HORIZON_PROFILE_SCOPE_N("cmdbuf::retire_keep_alive");
        keep_alive_.clear();
        debug_summary_.clear();
        recording_id_ = 0;
        submission_id_ = 0;
    }

    // ================================================================
    // Queue
    // ================================================================

    Queue::Queue(DeviceId device_id, QueueCapability capability)
    {
        id_.device = device_id;
        id_.capability = capability;
    }

    Queue::Queue(VkDevice device, VkQueue queue, uint32_t queue_family_index, uint32_t queue_index, DeviceId device_id, QueueCapability capability)
        : device_(device), queue_(queue)
    {
        id_.device = device_id;
        id_.family_index = queue_family_index;
        id_.queue_index = queue_index;
        id_.capability = capability;
        name_vulkan_object(device_,
                           VK_OBJECT_TYPE_QUEUE,
                           reinterpret_cast<uint64_t>(queue_),
                           "horizon.queue.device." + std::to_string(device_id.value) +
                               ".family." + std::to_string(queue_family_index) +
                               ".index." + std::to_string(queue_index) +
                               "." + capability_name(capability));
        create_timeline_semaphore();
    }

    Queue::~Queue()
    {
        std::deque<std::shared_ptr<TrackedCommandBuffer>> in_flight;
        std::deque<std::shared_ptr<TrackedCommandBuffer>> pool;
        VkSemaphore timeline_to_destroy = VK_NULL_HANDLE;
        {
            std::lock_guard lock(mutex_);
            trace_timeline_event("queue.destroy.begin",
                                 id_,
                                 device_,
                                 queue_,
                                 timeline_,
                                 last_submitted_value_,
                                 last_completed_value_.load(std::memory_order_acquire),
                                 in_flight_.size(),
                                 pool_.size());
            in_flight.swap(in_flight_);
            pool.swap(pool_);
            timeline_to_destroy = timeline_;
            timeline_ = VK_NULL_HANDLE;
            if (timeline_sync_)
            {
                timeline_sync_->invalidate();
            }
        }

        for (std::shared_ptr<TrackedCommandBuffer>& command_buffer : in_flight)
        {
            if (command_buffer)
            {
                command_buffer->retire();
            }
        }

        if (device_ != VK_NULL_HANDLE && timeline_to_destroy != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device_, timeline_to_destroy, nullptr);
            trace_timeline_event("queue.destroy.timeline-destroyed",
                                 id_,
                                 device_,
                                 queue_,
                                 timeline_to_destroy,
                                 last_submitted_value_,
                                 last_completed_value_.load(std::memory_order_acquire));
        }
    }

    void Queue::create_timeline_semaphore()
    {
        if (device_ == VK_NULL_HANDLE)
        {
            return;
        }

        VkSemaphoreTypeCreateInfo type_info {};
        type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type_info.initialValue = 0;

        VkSemaphoreCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        create_info.pNext = &type_info;

        VkResult result = vkCreateSemaphore(device_, &create_info, nullptr, &timeline_);
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkCreateSemaphore(timeline) failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkCreateSemaphore failed. VkResult=" + std::to_string(static_cast<int>(result)));
        }

        name_vulkan_object(device_,
                           VK_OBJECT_TYPE_SEMAPHORE,
                           reinterpret_cast<uint64_t>(timeline_),
                           "horizon.queue.timeline.device." + std::to_string(id_.device.value) +
                               ".family." + std::to_string(id_.family_index) +
                               ".index." + std::to_string(id_.queue_index));
        timeline_sync_ = SubmissionSync::make(timeline_);
        trace_timeline_event("queue.timeline.created",
                             id_,
                             device_,
                             queue_,
                             timeline_,
                             0,
                             last_completed_value_.load(std::memory_order_acquire));
    }

    void Queue::mark_device_lost_unlocked(const char* reason, VkResult result) const noexcept
    {
        if (device_lost_)
        {
            return;
        }

        device_lost_ = true;

        try
        {
            std::ostringstream stream;
            stream << reason
                   << ". family_index=" << id_.family_index
                   << ", queue_index=" << id_.queue_index
                   << ", capability=" << capability_name(id_.capability)
                   << ", VkResult=" << static_cast<int>(result)
                   << ", last_submitted=" << last_submitted_value_
                   << ", last_completed=" << last_completed_value_.load(std::memory_order_acquire)
                   << ", in_flight=" << in_flight_.size()
                   << ", pooled=" << pool_.size();
            for (const auto& command_buffer : in_flight_)
            {
                if (command_buffer && !command_buffer->debug_summary().empty())
                {
                    stream << "\n[in-flight submission_id=" << command_buffer->submission_id()
                           << ", recording_id=" << command_buffer->recording_id() << "]\n"
                           << command_buffer->debug_summary();
                }
            }
            Diagnostics::write(Diagnostics::Level::Error, "VK_ERROR", stream.str());
        }
        catch (...)
        {
        }
    }

    std::shared_ptr<TrackedCommandBuffer> Queue::acquire(ExecutionCommitProfileSample* profile)
    {
        HORIZON_PROFILE_SCOPE_N("queue::acquire");
        {
            std::lock_guard lock(mutex_);
            if (device_lost_)
            {
                throw std::runtime_error("Queue acquire skipped because the Vulkan device is lost. family_index=" +
                                         std::to_string(id_.family_index) +
                                         ", queue_index=" +
                                         std::to_string(id_.queue_index));
            }
        }

        retire_completed(profile);

        const auto lock_start = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        if (profile != nullptr)
        {
            profile->queue_lock_wait_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - lock_start).count();
        }
        if (device_lost_)
        {
            throw std::runtime_error("Queue acquire skipped because the Vulkan device is lost. family_index=" +
                                     std::to_string(id_.family_index) +
                                     ", queue_index=" +
                                     std::to_string(id_.queue_index));
        }

        ++next_recording_id_;

        std::shared_ptr<TrackedCommandBuffer> command_buffer;
        if (!pool_.empty())
        {
            command_buffer = std::move(pool_.front());
            pool_.pop_front();
        }
        else
        {
            command_buffer = std::make_shared<TrackedCommandBuffer>(device_, id_.family_index);
        }

        command_buffer->reset_for_recording(next_recording_id_);
        return command_buffer;
    }

    SubmissionToken Queue::submit(QueueSubmission& submission,
                                  std::span<const SubmitWait> waits,
                                  std::span<const SubmitSignal> signals,
                                  ExecutionCommitProfileSample* profile)
    {
        HORIZON_PROFILE_SCOPE_N("Horizon::queue_submit");
        std::vector<VkSemaphoreSubmitInfo> wait_infos;
        wait_infos.reserve(waits.size());
        for (const SubmitWait& wait : waits)
        {
            if (wait.semaphore == VK_NULL_HANDLE)
            {
                continue;
            }

            VkSemaphoreSubmitInfo info {};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = wait.semaphore;
            info.value = wait.value;
            info.stageMask = wait.stages;
            wait_infos.push_back(info);
        }

        std::vector<VkSemaphoreSubmitInfo> signal_infos;
        signal_infos.reserve(signals.size() + 1);
        for (const SubmitSignal& signal : signals)
        {
            if (signal.semaphore == VK_NULL_HANDLE)
            {
                continue;
            }

            VkSemaphoreSubmitInfo info {};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = signal.semaphore;
            info.value = signal.value;
            info.stageMask = signal.stages;
            signal_infos.push_back(info);
        }

        retire_completed(profile);

        const auto lock_start = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        if (profile != nullptr)
        {
            profile->queue_lock_wait_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - lock_start).count();
        }
        if (device_lost_)
        {
            throw std::runtime_error("Queue submit skipped because the Vulkan device is lost. family_index=" +
                                     std::to_string(id_.family_index) +
                                     ", queue_index=" +
                                     std::to_string(id_.queue_index));
        }

        if (!submission.command_buffer)
        {
            submission.command_buffer = std::make_shared<TrackedCommandBuffer>(device_, id_.family_index);
            submission.command_buffer->reset_for_recording(++next_recording_id_);
        }

        const uint64_t signal_value = last_submitted_value_ + 1;

        if (device_ != VK_NULL_HANDLE && queue_ != VK_NULL_HANDLE && submission.command_buffer->vk() != VK_NULL_HANDLE)
        {
            if (timeline_ != VK_NULL_HANDLE)
            {
                VkSemaphoreSubmitInfo timeline_signal {};
                timeline_signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                timeline_signal.semaphore = timeline_;
                timeline_signal.value = signal_value;
                timeline_signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                signal_infos.push_back(timeline_signal);
            }

            VkCommandBufferSubmitInfo command_info {};
            command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            command_info.commandBuffer = submission.command_buffer->vk();

            VkSubmitInfo2 submit_info {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount = static_cast<uint32_t>(wait_infos.size());
            submit_info.pWaitSemaphoreInfos = wait_infos.empty() ? nullptr : wait_infos.data();
            submit_info.signalSemaphoreInfoCount = static_cast<uint32_t>(signal_infos.size());
            submit_info.pSignalSemaphoreInfos = signal_infos.empty() ? nullptr : signal_infos.data();
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &command_info;

            const auto submit_start = std::chrono::steady_clock::now();
            VkResult result = vkQueueSubmit2(queue_, 1, &submit_info, VK_NULL_HANDLE);
            if (profile != nullptr)
            {
                profile->queue_submit_ms += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - submit_start).count();
            }
            if (result != VK_SUCCESS)
            {
                if (result == VK_ERROR_DEVICE_LOST)
                {
                    mark_device_lost_unlocked("vkQueueSubmit2 reported VK_ERROR_DEVICE_LOST", result);
                }
                std::string message = "vkQueueSubmit2 failed. family_index=" +
                    std::to_string(id_.family_index) +
                    ", queue_index=" +
                    std::to_string(id_.queue_index) +
                    ", VkResult=" +
                    std::to_string(static_cast<int>(result));
                if (!submission.debug_summary.empty())
                {
                    message += "\n[pending submission]\n";
                    message += submission.debug_summary;
                }
                Diagnostics::write(Diagnostics::Level::Error, "VK_ERROR", message);
                throw std::runtime_error(message);
            }
        }

        last_submitted_value_ = signal_value;
        submission.command_buffer->mark_submitted(signal_value,
                                                  std::move(submission.keep_alive),
                                                  std::move(submission.debug_summary));
        in_flight_.push_back(std::move(submission.command_buffer));
        trace_timeline_event("queue.submit.signal",
                             id_,
                             device_,
                             queue_,
                             timeline_,
                             signal_value,
                             last_completed_value_.load(std::memory_order_acquire),
                             in_flight_.size(),
                             pool_.size());

        return { id_.device, id_, signal_value, timeline_sync_ };
    }

    SubmissionToken Queue::signal_timeline_locked()
    {
        const uint64_t signal_value = last_submitted_value_ + 1;

        if (device_ != VK_NULL_HANDLE && queue_ != VK_NULL_HANDLE && timeline_ != VK_NULL_HANDLE)
        {
            VkSemaphoreSubmitInfo timeline_signal {};
            timeline_signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            timeline_signal.semaphore = timeline_;
            timeline_signal.value = signal_value;
            timeline_signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submit_info {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.signalSemaphoreInfoCount = 1;
            submit_info.pSignalSemaphoreInfos = &timeline_signal;

            const VkResult result = vkQueueSubmit2(queue_, 1, &submit_info, VK_NULL_HANDLE);
            if (result != VK_SUCCESS)
            {
                if (result == VK_ERROR_DEVICE_LOST)
                {
                    mark_device_lost_unlocked("vkQueueSubmit2(present timeline signal) reported VK_ERROR_DEVICE_LOST", result);
                }
                Diagnostics::write(Diagnostics::Level::Error,
                                   "VK_ERROR",
                                   "vkQueueSubmit2(present timeline signal) failed. family_index=" +
                                       std::to_string(id_.family_index) +
                                       ", queue_index=" +
                                       std::to_string(id_.queue_index) +
                                       ", VkResult=" +
                                       std::to_string(static_cast<int>(result)));
                throw std::runtime_error("vkQueueSubmit2(present timeline signal) failed. family_index=" +
                                         std::to_string(id_.family_index) +
                                         ", queue_index=" +
                                         std::to_string(id_.queue_index) +
                                         ", VkResult=" +
                                         std::to_string(static_cast<int>(result)));
            }
        }

        last_submitted_value_ = signal_value;
        trace_timeline_event("queue.present.signal",
                             id_,
                             device_,
                             queue_,
                             timeline_,
                             signal_value,
                             last_completed_value_.load(std::memory_order_acquire),
                             in_flight_.size(),
                             pool_.size());
        return { id_.device, id_, signal_value, timeline_sync_ };
    }

    QueuePresentResult Queue::present(const VkPresentInfoKHR& present_info)
    {
        QueuePresentResult present_result;
        {
            std::lock_guard lock(mutex_);

            if (queue_ == VK_NULL_HANDLE || device_lost_)
            {
                if (device_lost_)
                {
                    present_result.result = VK_ERROR_DEVICE_LOST;
                }
                return present_result;
            }

            present_result.result = vkQueuePresentKHR(queue_, &present_info);
            if (present_result.result == VK_ERROR_DEVICE_LOST)
            {
                mark_device_lost_unlocked("vkQueuePresentKHR reported VK_ERROR_DEVICE_LOST", present_result.result);
            }
            if (present_result.result == VK_SUCCESS || present_result.result == VK_SUBOPTIMAL_KHR)
            {
                present_result.completion = signal_timeline_locked();
            }
        }
        if (present_result.result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkQueuePresentKHR failed. family_index=" +
                                   std::to_string(id_.family_index) +
                                   ", queue_index=" +
                                   std::to_string(id_.queue_index) +
                                   ", VkResult=" +
                                   std::to_string(static_cast<int>(present_result.result)));
        }

        return present_result;
    }

    void Queue::wait_idle()
    {
        VkResult result = VK_SUCCESS;
        {
            std::lock_guard lock(mutex_);
            if (queue_ == VK_NULL_HANDLE || device_lost_)
            {
                if (device_lost_)
                {
                    result = VK_ERROR_DEVICE_LOST;
                }
                return;
            }

            result = vkQueueWaitIdle(queue_);
            if (result == VK_ERROR_DEVICE_LOST)
            {
                mark_device_lost_unlocked("vkQueueWaitIdle reported VK_ERROR_DEVICE_LOST", result);
            }
        }

        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkQueueWaitIdle failed. family_index=" +
                                   std::to_string(id_.family_index) +
                                   ", queue_index=" +
                                   std::to_string(id_.queue_index) +
                                   ", VkResult=" +
                                   std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkQueueWaitIdle failed. family_index=" +
                                     std::to_string(id_.family_index) +
                                     ", queue_index=" +
                                     std::to_string(id_.queue_index) +
                                     ", VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }

        retire_completed();
    }

    void Queue::wait_for(const SubmissionToken& token) const
    {
        if (token.value == 0)
        {
            return;
        }

        {
            std::lock_guard lock(mutex_);
            if (device_lost_)
            {
                throw std::runtime_error("vkWaitSemaphores skipped because the Vulkan device is lost.");
            }
        }

        const VkSemaphore timeline = token.sync ? token.sync->timeline() : VK_NULL_HANDLE;
        if (device_ == VK_NULL_HANDLE || timeline == VK_NULL_HANDLE)
        {
            const_cast<Queue*>(this)->mark_completed_for_tests(token.value);
            return;
        }

        VkSemaphoreWaitInfo wait_info {};
        wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores = &timeline;
        wait_info.pValues = &token.value;

        const VkResult result = vkWaitSemaphores(device_, &wait_info, UINT64_MAX);
        if (result != VK_SUCCESS)
        {
            if (result == VK_ERROR_DEVICE_LOST)
            {
                std::lock_guard lock(mutex_);
                mark_device_lost_unlocked("vkWaitSemaphores reported VK_ERROR_DEVICE_LOST", result);
            }
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkWaitSemaphores failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkWaitSemaphores failed. VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }
    }

    uint64_t Queue::query_completed_value() const
    {
        if (device_ == VK_NULL_HANDLE || timeline_ == VK_NULL_HANDLE)
        {
            return last_completed_value_.load(std::memory_order_acquire);
        }
        if (device_lost_)
        {
            throw std::runtime_error("vkGetSemaphoreCounterValue skipped because the Vulkan device is lost. family_index=" +
                                     std::to_string(id_.family_index) +
                                     ", queue_index=" +
                                     std::to_string(id_.queue_index));
        }

        uint64_t value = 0;
        const bool trace_query = should_trace_timeline_query();
        if (trace_query)
        {
            trace_timeline_event("queue.query.begin",
                                 id_,
                                 device_,
                                 queue_,
                                 timeline_,
                                 0,
                                 last_completed_value_.load(std::memory_order_acquire));
        }
        VkResult result = vkGetSemaphoreCounterValue(device_, timeline_, &value);
        if (result != VK_SUCCESS)
        {
            if (result == VK_ERROR_DEVICE_LOST)
            {
                mark_device_lost_unlocked("vkGetSemaphoreCounterValue reported VK_ERROR_DEVICE_LOST", result);
            }
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkGetSemaphoreCounterValue failed. family_index=" +
                                   std::to_string(id_.family_index) +
                                   ", queue_index=" +
                                   std::to_string(id_.queue_index) +
                                   ", VkResult=" +
                                   std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkGetSemaphoreCounterValue failed. family_index=" +
                                     std::to_string(id_.family_index) +
                                     ", queue_index=" +
                                     std::to_string(id_.queue_index) +
                                     ", VkResult=" +
                                     std::to_string(static_cast<int>(result)));
        }
        if (value == std::numeric_limits<uint64_t>::max())
        {
            mark_device_lost_unlocked("vkGetSemaphoreCounterValue returned UINT64_MAX; refusing to retire all in-flight command buffers", VK_ERROR_DEVICE_LOST);
            throw std::runtime_error("vkGetSemaphoreCounterValue returned UINT64_MAX. family_index=" +
                                     std::to_string(id_.family_index) +
                                     ", queue_index=" +
                                     std::to_string(id_.queue_index));
        }

        last_completed_value_.store(value, std::memory_order_release);
        if (trace_query)
        {
            trace_timeline_event("queue.query.end",
                                 id_,
                                 device_,
                                 queue_,
                                 timeline_,
                                 value,
                                 value);
        }
        return value;
    }

    void Queue::retire_completed(ExecutionCommitProfileSample* profile)
    {
        HORIZON_PROFILE_SCOPE_N("queue::retire_completed");
        const auto retire_start = std::chrono::steady_clock::now();
        std::deque<std::shared_ptr<TrackedCommandBuffer>> completed;
        {
            std::lock_guard lock(mutex_);
            collect_completed_unlocked(completed);
        }

        for (std::shared_ptr<TrackedCommandBuffer>& command_buffer : completed)
        {
            if (command_buffer)
            {
                command_buffer->retire();
            }
        }

        if (completed.empty())
        {
            if (profile != nullptr)
            {
                profile->queue_retire_ms += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - retire_start).count();
            }
            return;
        }

        std::lock_guard lock(mutex_);
        while (!completed.empty())
        {
            pool_.push_back(std::move(completed.front()));
            completed.pop_front();
        }
        if (profile != nullptr)
        {
            profile->queue_retire_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - retire_start).count();
        }
    }

    void Queue::collect_completed_unlocked(std::deque<std::shared_ptr<TrackedCommandBuffer>>& completed)
    {
        const uint64_t completed_value = query_completed_value();

        std::deque<std::shared_ptr<TrackedCommandBuffer>> still_in_flight;
        while (!in_flight_.empty())
        {
            std::shared_ptr<TrackedCommandBuffer> command_buffer = std::move(in_flight_.front());
            in_flight_.pop_front();

            if (command_buffer->submission_id() <= completed_value)
            {
                completed.push_back(std::move(command_buffer));
            }
            else
            {
                still_in_flight.push_back(std::move(command_buffer));
            }
        }

        in_flight_ = std::move(still_in_flight);
    }

    uint64_t Queue::completed_value() const
    {
        std::lock_guard lock(mutex_);
        return query_completed_value();
    }

    uint64_t Queue::last_submitted_value() const noexcept
    {
        std::lock_guard lock(mutex_);
        return last_submitted_value_;
    }

    bool Queue::device_lost() const noexcept
    {
        std::lock_guard lock(mutex_);
        return device_lost_;
    }

    size_t Queue::in_flight_count() const
    {
        std::lock_guard lock(mutex_);
        return in_flight_.size();
    }

    size_t Queue::pooled_count() const
    {
        std::lock_guard lock(mutex_);
        return pool_.size();
    }

    void Queue::mark_completed_for_tests(uint64_t value) noexcept
    {
        last_completed_value_.store(value, std::memory_order_release);
    }

    // ================================================================
    // DeviceManager
    // ================================================================

    DeviceManager::~DeviceManager()
    {
        shutdown();
    }

    void DeviceManager::initialize(const HardwareCreateConfig& config, VkInstance instance, VkPhysicalDevice physical_device)
    {
        shutdown();

        instance_ = instance;
        physical_device_ = physical_device;
        properties_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(physical_device_, &properties_);

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, properties.data());

        queue_families_.clear();
        queue_families_.reserve(properties.size());
        for (uint32_t index = 0; index < properties.size(); ++index)
        {
            if (properties[index].queueCount == 0)
            {
                continue;
            }

            queue_families_.push_back({ index, properties[index].queueCount, properties[index].queueFlags });
        }

        if (queue_families_.empty())
        {
            throw std::runtime_error("Vulkan physical device exposes no queue families.");
        }

        create_device(config);
        create_queues();
    }

    void DeviceManager::shutdown() noexcept
    {
        graphics_queues_.clear();
        compute_queues_.clear();
        transfer_queues_.clear();
        //present_queues_.clear();

        if (logical_device_ != VK_NULL_HANDLE)
        {
            (void)vkDeviceWaitIdle(logical_device_);
        }

        queues_.clear();

        if (logical_device_ != VK_NULL_HANDLE)
        {
            vkDestroyDevice(logical_device_, nullptr);
            logical_device_ = VK_NULL_HANDLE;
        }

        instance_ = VK_NULL_HANDLE;
        physical_device_ = VK_NULL_HANDLE;
        properties_ = {};
        enabled_features_ = {};
        queue_families_.clear();
    }

    void DeviceManager::create_device(const HardwareCreateConfig& config)
    {
        std::vector<const char*> enabled_extensions = filter_supported_device_extensions(physical_device_, config.get_device_extensions(instance_, physical_device_));

        DeviceFeaturesChain supported_features;
        vkGetPhysicalDeviceFeatures2(physical_device_, supported_features.chain_head());
        enabled_features_ = supported_features & config.get_device_features(instance_, physical_device_);

        std::vector<std::vector<float>> queue_priorities;
        queue_priorities.reserve(queue_families_.size());
        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        queue_infos.reserve(queue_families_.size());

        for (const QueueFamilyInfo& family : queue_families_)
        {
            const uint32_t queue_count = requested_queue_count(family);
            queue_priorities.emplace_back(queue_count, 1.0f);

            VkDeviceQueueCreateInfo queue_info {};
            queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info.queueFamilyIndex = family.family_index;
            queue_info.queueCount = queue_count;
            queue_info.pQueuePriorities = queue_priorities.back().data();
            queue_infos.push_back(queue_info);
        }

        VkDeviceCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
        create_info.pQueueCreateInfos = queue_infos.data();
        create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        create_info.ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data();
        create_info.pNext = enabled_features_.chain_head();

        VkResult result = vkCreateDevice(physical_device_, &create_info, nullptr, &logical_device_);
        if (result != VK_SUCCESS)
        {
            Diagnostics::write(Diagnostics::Level::Error,
                               "VK_ERROR",
                               "vkCreateDevice failed. VkResult=" + std::to_string(static_cast<int>(result)));
            throw std::runtime_error("vkCreateDevice failed. VkResult=" + std::to_string(static_cast<int>(result)));
        }

        // this is shit
        //volkLoadDevice(logical_device_);
    }

    void DeviceManager::create_queues()
    {
        size_t total_queue_count = 0;
        for (const QueueFamilyInfo& family : queue_families_)
        {
            total_queue_count += requested_queue_count(family);
        }

        queues_.reserve(total_queue_count);
        const DeviceId device_id { 0 };

        for (const QueueFamilyInfo& family : queue_families_)
        {
            QueueCapability primary_capability = QueueCapability::Transfer;
            if ((family.flags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                primary_capability = QueueCapability::Graphics;
            }
            else if ((family.flags & VK_QUEUE_COMPUTE_BIT) != 0)
            {
                primary_capability = QueueCapability::Compute;
            }

            const uint32_t queue_count = requested_queue_count(family);
            for (uint32_t queue_index = 0; queue_index < queue_count; ++queue_index)
            {
                VkQueue queue = VK_NULL_HANDLE;
                vkGetDeviceQueue(logical_device_, family.family_index, queue_index, &queue);

                auto owned_queue = std::make_unique<Queue>(logical_device_, queue, family.family_index, queue_index, device_id, primary_capability);
                Queue& queue_ref = *owned_queue;

                add_queue_if_supported(graphics_queues_, queue_ref, family.flags, QueueCapability::Graphics);
                add_queue_if_supported(compute_queues_, queue_ref, family.flags, QueueCapability::Compute);
                add_queue_if_supported(transfer_queues_, queue_ref, family.flags, QueueCapability::Transfer);
                //add_queue_if_supported(present_queues_, queue_ref, family.flags, QueueCapability::Present);

                queues_.push_back(std::move(owned_queue));
            }
        }
    }

    Queue* DeviceManager::queue_for(QueueCapability capability) noexcept
    {
        const std::vector<Queue*>& queues = queues_for(capability);
        switch (capability)
        {
        case QueueCapability::Graphics:
            return pick_thread_queue(queues);
        case QueueCapability::Compute:
            return pick_thread_queue(queues);
        case QueueCapability::Transfer:
            return pick_thread_queue(queues);
        case QueueCapability::Present:
            return pick_thread_queue(queues);
        }

        return nullptr;
    }

    const Queue* DeviceManager::queue_for(QueueCapability capability) const noexcept
    {
        const std::vector<Queue*>& queues = queues_for(capability);
        switch (capability)
        {
        case QueueCapability::Graphics:
            return pick_thread_queue(queues);
        case QueueCapability::Compute:
            return pick_thread_queue(queues);
        case QueueCapability::Transfer:
            return pick_thread_queue(queues);
        case QueueCapability::Present:
            return pick_thread_queue(queues);
        }

        return nullptr;
    }

    Queue* DeviceManager::queue_by_id(const QueueId& id) noexcept
    {
        const auto found = std::find_if(queues_.begin(), queues_.end(), [&id](const std::unique_ptr<Queue>& queue) {
            if (!queue)
            {
                return false;
            }

            const QueueId queue_id = queue->id();
            return queue_id.device == id.device &&
                   queue_id.family_index == id.family_index &&
                   queue_id.queue_index == id.queue_index;
        });

        return found == queues_.end() ? nullptr : found->get();
    }

    const Queue* DeviceManager::queue_by_id(const QueueId& id) const noexcept
    {
        const auto found = std::find_if(queues_.begin(), queues_.end(), [&id](const std::unique_ptr<Queue>& queue) {
            if (!queue)
            {
                return false;
            }

            const QueueId queue_id = queue->id();
            return queue_id.device == id.device &&
                   queue_id.family_index == id.family_index &&
                   queue_id.queue_index == id.queue_index;
        });

        return found == queues_.end() ? nullptr : found->get();
    }

    const std::vector<Queue*>& DeviceManager::queues_for(QueueCapability capability) const noexcept
    {
        switch (capability)
        {
        case QueueCapability::Graphics:
            return graphics_queues_;
        case QueueCapability::Present:
            return graphics_queues_;
        case QueueCapability::Compute:
            return compute_queues_.empty() ? graphics_queues_ : compute_queues_;
        case QueueCapability::Transfer:
            if (!transfer_queues_.empty())
            {
                return transfer_queues_;
            }
            if (!compute_queues_.empty())
            {
                return compute_queues_;
            }
            return graphics_queues_;
        }

        return graphics_queues_;
    }
}
