#pragma once

#include <mutex>
#include <set>
#include <unordered_map>

#include "FeaturesChain.h"
#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"

class DeviceManager
{
  public:
    struct ExternalSemaphoreHandle
    {
#if _WIN32 || _WIN64
        HANDLE handle = nullptr;
#else
        int fd = -1;
#endif
    };

    struct QueueUtils
    {
        std::shared_ptr<std::mutex> queueMutex;
        std::shared_ptr<std::atomic_uint64_t> timelineValue;
        VkSemaphore timelineSemaphore{VK_NULL_HANDLE};
        VkFence queueWaitFence{VK_NULL_HANDLE};
        uint32_t queueFamilyIndex = -1;
        VkQueue vkQueue{VK_NULL_HANDLE};
        VkCommandPool commandPool{VK_NULL_HANDLE};
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        DeviceManager *deviceManager{nullptr};
    };

    struct FeaturesUtils
    {
        std::set<const char *> instanceExtensions;
        std::set<const char *> deviceExtensions;
        VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
        VkPhysicalDeviceProperties2 supportedProperties{};
        DeviceFeaturesChain featuresChain;
    };

    DeviceManager();
    ~DeviceManager();

    DeviceManager(const DeviceManager &) = delete;
    DeviceManager &operator=(const DeviceManager &) = delete;
    DeviceManager(DeviceManager &&) = delete;
    DeviceManager &operator=(DeviceManager &&) = delete;

    void initDeviceManager(const CreateCallback &createCallback, const VkInstance &vkInstance, const VkPhysicalDevice &physicalDevice);
    void cleanUpDeviceManager();

    ExternalSemaphoreHandle exportSemaphore(VkSemaphore &semaphore);

    /// 初始化阶段：预导入所有外部设备的 timeline semaphore。
    /// 必须在所有 DeviceManager 完成 initDeviceManager() 之后调用。
    void importForeignSemaphores(const std::vector<DeviceManager *> &otherDevices);

    /// 运行时查表：获取本设备上可用的 timeline semaphore。
    /// 同设备直接返回原 semaphore；跨设备从预导入缓存查表（初始化阶段已填充）。
    VkSemaphore getOrImportTimelineSemaphore(const QueueUtils &foreignQueue) const;

    std::vector<QueueUtils> pickAvailableQueues(std::function<bool(const QueueUtils &)> predicate) const;

    VkPhysicalDevice getPhysicalDevice() const
    {
        return physicalDevice;
    }
    VkDevice getLogicalDevice() const
    {
        return logicalDevice;
    }
    const FeaturesUtils &getFeaturesUtils() const
    {
        return deviceFeaturesUtils;
    }
    FeaturesUtils &getFeaturesUtils()
    {
        return deviceFeaturesUtils;
    }
    uint16_t getQueueFamilyNumber() const
    {
        return static_cast<uint16_t>(queueFamilies.size());
    }

    bool operator==(const DeviceManager &other) const
    {
        return physicalDevice == other.physicalDevice && logicalDevice == other.logicalDevice;
    }

  private:
    friend class HardwareExecutorVulkan;
    friend struct ResourceManager;  // 允许 ResourceManager 访问队列以进行延迟销毁同步

    void createDevices(const CreateCallback &createInfo, const VkInstance &vkInstance);
    void createQueueUtils();
    //bool createCommandBuffers();
    //void createTimelineSemaphore();

    void destroyQueueResources(std::vector<QueueUtils> &queues);

    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice logicalDevice{VK_NULL_HANDLE};
    FeaturesUtils deviceFeaturesUtils;

    std::atomic_uint16_t currentGraphicsQueueIndex{0};
    std::atomic_uint16_t currentComputeQueueIndex{0};
    std::atomic_uint16_t currentTransferQueueIndex{0};

    std::vector<QueueUtils> graphicsQueues;
    std::vector<QueueUtils> computeQueues;
    std::vector<QueueUtils> transferQueues;
    std::vector<VkQueueFamilyProperties> queueFamilies;

    // 用于追踪已销毁的资源，避免共享队列的重复释放
    std::set<VkCommandPool> destroyedPools;

    // 跨设备 timeline semaphore 缓存：外部 VkSemaphore → 本设备已导入的 VkSemaphore
    std::unordered_map<VkSemaphore, VkSemaphore> importedTimelineSemaphores;
};