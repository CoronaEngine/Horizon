#include "device_manager.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace Corona::Horizon
{
    namespace
    {
        void check_vk(VkResult result, const char* message)
        {
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error(std::string(message) + " VkResult=" + std::to_string(static_cast<int>(result)));
            }
        }

        [[nodiscard]] bool has_extension(const std::vector<VkExtensionProperties>& extensions, const char* name)
        {
            return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        }

        [[nodiscard]] std::vector<const char*> filter_supported_device_extensions(VkPhysicalDevice physical_device,
                                                                                  const std::set<const char*>& requested)
        {
            uint32_t extension_count = 0;
            check_vk(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr),
                     "vkEnumerateDeviceExtensionProperties failed.");

            std::vector<VkExtensionProperties> available(extension_count);
            check_vk(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, available.data()),
                     "vkEnumerateDeviceExtensionProperties failed.");

            std::vector<const char*> enabled;
            enabled.reserve(requested.size());
            for (const char* extension : requested)
            {
                if (has_extension(available, extension))
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
    }

    TrackedCommandBuffer::TrackedCommandBuffer(VkDevice device, uint32_t queue_family_index)
        : device_(device)
    {
        if (device_ == VK_NULL_HANDLE)
        {
            return;
        }

        VkCommandPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = queue_family_index;

        check_vk(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool failed.");

        VkCommandBufferAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        check_vk(vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer_), "vkAllocateCommandBuffers failed.");
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
        recording_id_ = recording_id;
        submission_id_ = 0;

        if (command_buffer_ != VK_NULL_HANDLE)
        {
            check_vk(vkResetCommandBuffer(command_buffer_, 0), "vkResetCommandBuffer failed.");
        }
    }

    void TrackedCommandBuffer::mark_submitted(uint64_t submission_id, SubmissionKeepAlive keep_alive)
    {
        submission_id_ = submission_id;
        keep_alive_ = std::move(keep_alive);
    }

    void TrackedCommandBuffer::retire() noexcept
    {
        keep_alive_.clear();
        recording_id_ = 0;
        submission_id_ = 0;
    }

    Queue::Queue(DeviceId device_id, QueueCapability capability)
    {
        id_.device = device_id;
        id_.capability = capability;
    }

    Queue::Queue(VkDevice device,
                 VkQueue queue,
                 uint32_t queue_family_index,
                 uint32_t queue_index,
                 DeviceId device_id,
                 QueueCapability capability)
        : device_(device), queue_(queue)
    {
        id_.device = device_id;
        id_.family_index = queue_family_index;
        id_.queue_index = queue_index;
        id_.capability = capability;
        create_timeline_semaphore();
    }

    Queue::~Queue()
    {
        {
            std::lock_guard lock(mutex_);
            in_flight_.clear();
            pool_.clear();
        }

        if (device_ != VK_NULL_HANDLE && timeline_ != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device_, timeline_, nullptr);
            timeline_ = VK_NULL_HANDLE;
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

        check_vk(vkCreateSemaphore(device_, &create_info, nullptr, &timeline_), "vkCreateSemaphore failed.");
    }

    std::shared_ptr<TrackedCommandBuffer> Queue::acquire()
    {
        std::lock_guard lock(mutex_);
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
                                  std::span<const SubmitSignal> signals)
    {
        std::lock_guard lock(mutex_);

        if (!submission.command_buffer)
        {
            submission.command_buffer = std::make_shared<TrackedCommandBuffer>(device_, id_.family_index);
            submission.command_buffer->reset_for_recording(++next_recording_id_);
        }

        const uint64_t signal_value = last_submitted_value_ + 1;

        if (fail_next_submit_)
        {
            fail_next_submit_ = false;
            throw std::runtime_error("Injected queue submit failure.");
        }

        if (device_ != VK_NULL_HANDLE && queue_ != VK_NULL_HANDLE && submission.command_buffer->vk() != VK_NULL_HANDLE)
        {
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

            check_vk(vkQueueSubmit2(queue_, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit2 failed.");
        }

        last_submitted_value_ = signal_value;
        submission.command_buffer->mark_submitted(signal_value, std::move(submission.keep_alive));
        in_flight_.push_back(std::move(submission.command_buffer));

        return { id_.device, id_, signal_value, timeline_ };
    }

    uint64_t Queue::query_completed_value() const
    {
        if (device_ == VK_NULL_HANDLE || timeline_ == VK_NULL_HANDLE)
        {
            return last_completed_value_.load(std::memory_order_acquire);
        }

        uint64_t value = 0;
        check_vk(vkGetSemaphoreCounterValue(device_, timeline_, &value), "vkGetSemaphoreCounterValue failed.");
        last_completed_value_.store(value, std::memory_order_release);
        return value;
    }

    void Queue::retire_completed()
    {
        std::lock_guard lock(mutex_);
        const uint64_t completed = query_completed_value();

        std::deque<std::shared_ptr<TrackedCommandBuffer>> still_in_flight;
        while (!in_flight_.empty())
        {
            std::shared_ptr<TrackedCommandBuffer> command_buffer = std::move(in_flight_.front());
            in_flight_.pop_front();

            if (command_buffer->submission_id() <= completed)
            {
                command_buffer->retire();
                pool_.push_back(std::move(command_buffer));
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
        return query_completed_value();
    }

    uint64_t Queue::last_submitted_value() const noexcept
    {
        std::lock_guard lock(mutex_);
        return last_submitted_value_;
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

    void Queue::fail_next_submit_for_tests() noexcept
    {
        std::lock_guard lock(mutex_);
        fail_next_submit_ = true;
    }

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
        present_queues_.clear();
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
        std::vector<const char*> enabled_extensions =
            filter_supported_device_extensions(physical_device_, config.get_device_extensions(instance_, physical_device_));

        DeviceFeaturesChain supported_features;
        vkGetPhysicalDeviceFeatures2(physical_device_, supported_features.chain_head());
        enabled_features_ = supported_features & config.get_device_features(instance_, physical_device_);

        std::vector<float> priorities(queue_families_.size(), 1.0f);
        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        queue_infos.reserve(queue_families_.size());

        for (size_t index = 0; index < queue_families_.size(); ++index)
        {
            VkDeviceQueueCreateInfo queue_info {};
            queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info.queueFamilyIndex = queue_families_[index].family_index;
            queue_info.queueCount = 1;
            queue_info.pQueuePriorities = &priorities[index];
            queue_infos.push_back(queue_info);
        }

        VkDeviceCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
        create_info.pQueueCreateInfos = queue_infos.data();
        create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        create_info.ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data();
        create_info.pNext = enabled_features_.chain_head();

        check_vk(vkCreateDevice(physical_device_, &create_info, nullptr, &logical_device_), "vkCreateDevice failed.");
        volkLoadDevice(logical_device_);
    }

    void DeviceManager::create_queues()
    {
        queues_.reserve(queue_families_.size());
        const DeviceId device_id { 0 };

        for (const QueueFamilyInfo& family : queue_families_)
        {
            VkQueue queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(logical_device_, family.family_index, 0, &queue);

            QueueCapability primary_capability = QueueCapability::Transfer;
            if ((family.flags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                primary_capability = QueueCapability::Graphics;
            }
            else if ((family.flags & VK_QUEUE_COMPUTE_BIT) != 0)
            {
                primary_capability = QueueCapability::Compute;
            }

            auto owned_queue = std::make_unique<Queue>(logical_device_, queue, family.family_index, 0, device_id, primary_capability);
            Queue& queue_ref = *owned_queue;

            add_queue_if_supported(graphics_queues_, queue_ref, family.flags, QueueCapability::Graphics);
            add_queue_if_supported(compute_queues_, queue_ref, family.flags, QueueCapability::Compute);
            add_queue_if_supported(transfer_queues_, queue_ref, family.flags, QueueCapability::Transfer);
            add_queue_if_supported(present_queues_, queue_ref, family.flags, QueueCapability::Present);

            queues_.push_back(std::move(owned_queue));
        }
    }

    Queue* DeviceManager::queue_for(QueueCapability capability) noexcept
    {
        const std::vector<Queue*>& queues = queues_for(capability);
        return queues.empty() ? nullptr : queues.front();
    }

    const Queue* DeviceManager::queue_for(QueueCapability capability) const noexcept
    {
        const std::vector<Queue*>& queues = queues_for(capability);
        return queues.empty() ? nullptr : queues.front();
    }

    const std::vector<Queue*>& DeviceManager::queues_for(QueueCapability capability) const noexcept
    {
        switch (capability)
        {
        case QueueCapability::Graphics:
            return graphics_queues_;
        case QueueCapability::Present:
            return present_queues_.empty() ? graphics_queues_ : present_queues_;
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
