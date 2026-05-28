#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "features_chain.h"

namespace Corona::Horizon
{
    // command buffer with resource tracking
    class TrackedCommandBuffer
    {
    public:
        // the command buffer itself
        vk::CommandBuffer cmdBuf = vk::CommandBuffer();
        vk::CommandPool cmdPool = vk::CommandPool();

        std::vector<RefCountPtr<IResource>> referencedResources;   // to keep them alive
        std::vector<RefCountPtr<Buffer>> referencedStagingBuffers; // to allow synchronous mapBuffer

        uint64_t recordingID = 0;
        uint64_t submissionID = 0;

        explicit TrackedCommandBuffer(const VulkanContext& context)
            : m_Context(context)
        {
        }

        ~TrackedCommandBuffer();

    private:
        vk::Instance instance;
        vk::PhysicalDevice physicalDevice;
        vk::Device device;
        vk::AllocationCallbacks* allocationCallbacks;
        vk::PipelineCache pipelineCache;
    };

    class TrackedCommandBuffer
    {
    public:
        TrackedCommandBuffer(VkDevice device, uint32_t queue_family_index)
            : device_(device)
        {
            VkCommandPoolCreateInfo pool_info {};
            pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                              VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool_info.queueFamilyIndex = queue_family_index;

            check_vk(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool),
                     "vkCreateCommandPool failed");

            VkCommandBufferAllocateInfo alloc_info {};
            alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc_info.commandPool = command_pool;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandBufferCount = 1;

            check_vk(vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer),
                     "vkAllocateCommandBuffers failed");
        }

        ~TrackedCommandBuffer()
        {
            if (device_ != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device_, command_pool, nullptr);
            }
        }

        TrackedCommandBuffer(const TrackedCommandBuffer&) = delete;
        TrackedCommandBuffer& operator=(const TrackedCommandBuffer&) = delete;

        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        uint64_t recording_id = 0;
        uint64_t submission_id = 0;
        std::vector<std::shared_ptr<void>> keep_alive;

    private:
        VkDevice device_ = VK_NULL_HANDLE;
    };

    // represents a hardware queue
    class Queue
    {
    public:
        vk::Semaphore trackingSemaphore;

        Queue(const VulkanContext& context, CommandQueue queueID, vk::Queue queue, uint32_t queueFamilyIndex);
        ~Queue();

        // creates a command buffer and its synchronization resources
        TrackedCommandBufferPtr createCommandBuffer();

        TrackedCommandBufferPtr getOrCreateCommandBuffer();

        void addWaitSemaphore(vk::Semaphore semaphore, uint64_t value);
        void addSignalSemaphore(vk::Semaphore semaphore, uint64_t value);

        // submits a command buffer to this queue, returns submissionID
        uint64_t submit(ICommandList* const* ppCmd, size_t numCmd);

        void updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, uint32_t numTileMappings);

        // retire any command buffers that have finished execution from the pending execution list
        void retireCommandBuffers();

        TrackedCommandBufferPtr getCommandBufferInFlight(uint64_t submissionID);

        uint64_t updateLastFinishedID();
        uint64_t getLastSubmittedID() const { return m_LastSubmittedID; }
        uint64_t getLastFinishedID() const { return m_LastFinishedID; }
        CommandQueue getQueueID() const { return m_QueueID; }
        vk::Queue getVkQueue() const { return m_Queue; }

        bool pollCommandList(uint64_t commandListID);
        bool waitCommandList(uint64_t commandListID, uint64_t timeout);

    private:
        const VulkanContext& m_Context;

        vk::Queue m_Queue;
        CommandQueue m_QueueID;
        uint32_t m_QueueFamilyIndex = uint32_t(-1);

        std::mutex m_Mutex;
        std::vector<vk::Semaphore> m_WaitSemaphores;
        std::vector<uint64_t> m_WaitSemaphoreValues;
        std::vector<vk::Semaphore> m_SignalSemaphores;
        std::vector<uint64_t> m_SignalSemaphoreValues;

        uint64_t m_LastRecordingID = 0;
        uint64_t m_LastSubmittedID = 0;
        uint64_t m_LastFinishedID = 0;

        // tracks the list of command buffers in flight on this queue
        std::list<TrackedCommandBufferPtr> m_CommandBuffersInFlight;
        std::list<TrackedCommandBufferPtr> m_CommandBuffersPool;
    };

    class DeviceQueues
    {

    };

    class DeviceManager
    {
    public:
        DeviceManager();
        ~DeviceManager();

        void initialize(const HardwareCreateConfig& config, const VkInstance& instance, const VkPhysicalDevice& physical_device);
        void shutdown() noexcept;

    private:
    };

}
