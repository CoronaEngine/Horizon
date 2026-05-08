#include "DeviceManager.h"

#include <algorithm>
#include <cassert>

DeviceManager::DeviceManager() = default;

DeviceManager::~DeviceManager()
{
    cleanUpDeviceManager();
}

void DeviceManager::initDeviceManager(const CreateCallback &createCallback, const VkInstance &vkInstance, const VkPhysicalDevice &physicalDevice)
{
    this->physicalDevice = physicalDevice;

    createDevices(createCallback, vkInstance);
    createQueueUtils();
    // createCommandBuffers();
    // createTimelineSemaphore();

    // for (auto &queue : graphicsQueues)
    // {
    //     CFW_LOG_INFO("Graphics Queue - Family Index: {}, Queue Address: {}, VkQueue: {}, Timeline Semaphore: {}, Timeline Value Addr: {}, Mutex Addr: {}, Command Pool: {}, Command Buffer: {}, Device Manager: {}",
    //                  queue.queueFamilyIndex,
    //                  reinterpret_cast<std::uintptr_t>(&queue),
    //                  reinterpret_cast<std::uintptr_t>(queue.vkQueue),
    //                  reinterpret_cast<std::uintptr_t>(queue.timelineSemaphore),
    //                  reinterpret_cast<std::uintptr_t>(queue.timelineValue.get()),
    //                  reinterpret_cast<std::uintptr_t>(queue.queueMutex.get()),
    //                  reinterpret_cast<std::uintptr_t>(queue.commandPool),
    //                  reinterpret_cast<std::uintptr_t>(queue.commandBuffer),
    //                  reinterpret_cast<std::uintptr_t>(queue.deviceManager));
    // }

    // for (auto &queue : computeQueues)
    // {
    //     CFW_LOG_INFO("Compute Queue - Family Index: {}, Queue Address: {}, VkQueue: {}, Timeline Semaphore: {}, Timeline Value Addr: {}, Mutex Addr: {}, Command Pool: {}, Command Buffer: {}, Device Manager: {}",
    //                  queue.queueFamilyIndex,
    //                  reinterpret_cast<std::uintptr_t>(&queue),
    //                  reinterpret_cast<std::uintptr_t>(queue.vkQueue),
    //                  reinterpret_cast<std::uintptr_t>(queue.timelineSemaphore),
    //                  reinterpret_cast<std::uintptr_t>(queue.timelineValue.get()),
    //                  reinterpret_cast<std::uintptr_t>(queue.queueMutex.get()),
    //                  reinterpret_cast<std::uintptr_t>(queue.commandPool),
    //                  reinterpret_cast<std::uintptr_t>(queue.commandBuffer),
    //                  reinterpret_cast<std::uintptr_t>(queue.deviceManager));
    // }

    // for (auto &queue : transferQueues)
    // {
    //     CFW_LOG_INFO("Transfer Queue - Family Index: {}, Queue Address: {}, VkQueue: {}, Timeline Semaphore: {}, Timeline Value Addr: {}, Mutex Addr: {}, Command Pool: {}, Command Buffer: {}, Device Manager: {}",
    //                  queue.queueFamilyIndex,
    //                  reinterpret_cast<std::uintptr_t>(&queue),
    //                  reinterpret_cast<std::uintptr_t>(queue.vkQueue),
    //                  reinterpret_cast<std::uintptr_t>(queue.timelineSemaphore),
    //                  reinterpret_cast<std::uintptr_t>(queue.timelineValue.get()),
    //                  reinterpret_cast<std::uintptr_t>(queue.queueMutex.get()),
    //                  reinterpret_cast<std::uintptr_t>(queue.commandPool),
    //                  reinterpret_cast<std::uintptr_t>(queue.commandBuffer),
    //                  reinterpret_cast<std::uintptr_t>(queue.deviceManager));
    // }

    CFW_LOG_DEBUG("Graphics Queue Count: {}", graphicsQueues.size());
    CFW_LOG_DEBUG("Compute Queue Count: {}", computeQueues.size());
    CFW_LOG_DEBUG("Transfer Queue Count: {}", transferQueues.size());
}

void DeviceManager::cleanUpDeviceManager()
{
    if (logicalDevice == VK_NULL_HANDLE)
    {
        graphicsQueues.clear();
        computeQueues.clear();
        transferQueues.clear();
        queueFamilies.clear();
        physicalDevice = VK_NULL_HANDLE;
        currentGraphicsQueueIndex = 0;
        currentComputeQueueIndex = 0;
        currentTransferQueueIndex = 0;
        return;
    }

    vkDeviceWaitIdle(logicalDevice);

    // 清理跨设备导入的 semaphore
    for (auto &[foreignSem, localSem] : importedTimelineSemaphores)
    {
        if (localSem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(logicalDevice, localSem, nullptr);
        }
    }
    importedTimelineSemaphores.clear();

    // 清空已销毁资源追踪集合，准备新一轮清理
    destroyedPools.clear();

    destroyQueueResources(graphicsQueues);
    destroyQueueResources(computeQueues);
    destroyQueueResources(transferQueues);

    // 清理完成后清空追踪集合
    destroyedPools.clear();

    queueFamilies.clear();

    // vkDestroyDevice(logicalDevice, nullptr);
    logicalDevice = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;

    currentGraphicsQueueIndex = 0;
    currentComputeQueueIndex = 0;
    currentTransferQueueIndex = 0;
}

void DeviceManager::destroyQueueResources(std::vector<QueueUtils> &queues)
{
    for (auto &queue : queues)
    {
        // 检查资源是否已被其他队列容器释放（共享队列情况）
        // 通过检查 commandPool 是否已在 destroyedPools 中来避免重复释放
        if (destroyedPools.find(queue.commandPool) != destroyedPools.end())
        {
            // 资源已被释放，只需清理引用
            queue.commandBuffer = VK_NULL_HANDLE;
            queue.commandPool = VK_NULL_HANDLE;
            queue.timelineSemaphore = VK_NULL_HANDLE;
            queue.vkQueue = VK_NULL_HANDLE;
            queue.queueFamilyIndex = static_cast<uint32_t>(-1);
            queue.queueMutex.reset();
            queue.deviceManager = nullptr;
            continue;
        }

        if (queue.commandBuffer != VK_NULL_HANDLE && queue.commandPool != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(logicalDevice, queue.commandPool, 1, &queue.commandBuffer);
            queue.commandBuffer = VK_NULL_HANDLE;
        }

        if (queue.commandPool != VK_NULL_HANDLE)
        {
            destroyedPools.insert(queue.commandPool);
            vkDestroyCommandPool(logicalDevice, queue.commandPool, nullptr);
            queue.commandPool = VK_NULL_HANDLE;
        }

        if (queue.timelineSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(logicalDevice, queue.timelineSemaphore, nullptr);
            queue.timelineSemaphore = VK_NULL_HANDLE;
        }

        queue.vkQueue = VK_NULL_HANDLE;
        queue.queueFamilyIndex = static_cast<uint32_t>(-1);
        queue.queueMutex.reset();
        queue.deviceManager = nullptr;
    }

    queues.clear();
}

// void DeviceManager::createTimelineSemaphore() {
//     auto createTimelineSemaphoreForQueue = [&](QueueUtils& queue) {
//         VkExportSemaphoreCreateInfo exportInfo{};
//         exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
//         exportInfo.pNext = nullptr;
// #if _WIN32 || _WIN64
//         exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
// #endif
//         VkSemaphoreTypeCreateInfo typeCreateInfo{};
//         typeCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
//         typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
//         typeCreateInfo.initialValue = 0;
//         typeCreateInfo.pNext = &exportInfo;
//
//         VkSemaphoreCreateInfo semaphoreInfo{};
//         semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
//         semaphoreInfo.pNext = &typeCreateInfo;
//
//         coronaHardwareCheck(vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &queue.timelineSemaphore));
//     };
//
//     for (auto& queue : graphicsQueues) {
//         createTimelineSemaphoreForQueue(queue);
//     }
//     for (auto& queue : computeQueues) {
//         createTimelineSemaphoreForQueue(queue);
//     }
//     for (auto& queue : transferQueues) {
//         createTimelineSemaphoreForQueue(queue);
//     }
// }

void DeviceManager::createDevices(const CreateCallback &initInfo, const VkInstance &vkInstance)
{
    std::set<const char *> inputExtensions = initInfo.requiredDeviceExtensions(vkInstance, physicalDevice);
    std::vector<const char *> requiredExtensions = std::vector<const char *>(inputExtensions.begin(), inputExtensions.end());

    deviceFeaturesUtils.supportedProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceFeaturesUtils.rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    deviceFeaturesUtils.accelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;

    deviceFeaturesUtils.rayTracingPipelineProperties.pNext = &deviceFeaturesUtils.accelerationStructureProperties;
    deviceFeaturesUtils.supportedProperties.pNext = &deviceFeaturesUtils.rayTracingPipelineProperties;
    deviceFeaturesUtils.accelerationStructureProperties.pNext = nullptr;

    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceFeaturesUtils.supportedProperties);
    printDeviceInfo(deviceFeaturesUtils.supportedProperties.properties);

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    requiredExtensions.erase(std::remove_if(requiredExtensions.begin(),
                                            requiredExtensions.end(),
                                            [&availableExtensions](const char *required) {
                                                bool supported = std::any_of(availableExtensions.begin(), availableExtensions.end(),
                                                                             [required](const VkExtensionProperties &available) {
                                                                                 return strcmp(required, available.extensionName) == 0;
                                                                             });

                                                if (!supported)
                                                {
                                                    printExtensionWarning(required);
                                                }
                                                return !supported;
                                            }),
                             requiredExtensions.end());

    vkGetPhysicalDeviceFeatures2(physicalDevice, deviceFeaturesUtils.featuresChain.getChainHead());
    deviceFeaturesUtils.featuresChain = deviceFeaturesUtils.featuresChain & initInfo.requiredDeviceFeatures(vkInstance, physicalDevice);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    queueFamilies.resize(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::vector<std::vector<float>> queuePriorities(queueFamilies.size());

    for (int i = 0; i < queueFamilies.size(); i++)
    {
        queuePriorities[i].resize(queueFamilies[i].queueCount, 1.0f);

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = static_cast<uint32_t>(i);
        queueCreateInfo.queueCount = queueFamilies[i].queueCount;
        queueCreateInfo.pQueuePriorities = queuePriorities[i].data();

        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;
    createInfo.pEnabledFeatures = nullptr;
    createInfo.pNext = deviceFeaturesUtils.featuresChain.getChainHead();

    coronaHardwareCheck(vkCreateDevice(physicalDevice, &createInfo, nullptr, &logicalDevice));
}

void DeviceManager::createQueueUtils()
{
    auto createTimelineSemaphoreForQueue = [&](QueueUtils &queue) 
        {
            VkExportSemaphoreCreateInfo exportInfo{};
            exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
            exportInfo.pNext = nullptr;
#if _WIN32 || _WIN64
            exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#elif __linux__
            exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
            VkSemaphoreTypeCreateInfo typeCreateInfo{};
            typeCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            typeCreateInfo.initialValue = 0;
            typeCreateInfo.pNext = &exportInfo;

            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphoreInfo.pNext = &typeCreateInfo;
            semaphoreInfo.flags = 0;

            coronaHardwareCheck(vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &queue.timelineSemaphore));

#ifdef CABBAGE_ENGINE_DEBUG
            uint64_t initialValue = UINT64_MAX;
            vkGetSemaphoreCounterValue(logicalDevice, queue.timelineSemaphore, &initialValue);
            if (initialValue != 0)
            {
                CFW_LOG_ERROR("Timeline semaphore initial value is {}, expected 0!", initialValue);
            }
#endif
        };

    auto createCommandBufferForQueue = [this](QueueUtils &queue) 
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queue.queueFamilyIndex;

        coronaHardwareCheck(vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &queue.commandPool));

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = queue.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        coronaHardwareCheck(vkAllocateCommandBuffers(logicalDevice, &allocInfo, &queue.commandBuffer));
    };

    int queue_count = 0;

    for (int i = 0; i < queueFamilies.size(); i++)
    {
        for (uint32_t queueIndex = 0; queueIndex < queueFamilies[i].queueCount; queueIndex++)
        {
            QueueUtils queueUtils{};
            queueUtils.queueFamilyIndex = static_cast<uint32_t>(i);
            queueUtils.deviceManager = this;
            queueUtils.queueMutex = std::make_shared<std::mutex>();
            queueUtils.timelineValue = std::make_shared<std::atomic_uint64_t>(0);

            createTimelineSemaphoreForQueue(queueUtils);
            createCommandBufferForQueue(queueUtils);

            vkGetDeviceQueue(logicalDevice, static_cast<uint32_t>(i), queueIndex, &queueUtils.vkQueue);

            const VkQueueFlags flags = queueFamilies[i].queueFlags;
            if (flags & VK_QUEUE_GRAPHICS_BIT)
            {
                graphicsQueues.push_back(queueUtils);
                ++queue_count;
            }
            else if (flags & VK_QUEUE_COMPUTE_BIT)
            {
                computeQueues.push_back(queueUtils);
                ++queue_count;
            }
            else if (flags & VK_QUEUE_TRANSFER_BIT)
            {
                transferQueues.push_back(queueUtils);
                ++queue_count;
            }
            else
            {
                CFW_LOG_WARNING("Queue Family Index {} does not support graphics, compute, or transfer operations.", i);
            }
        }
    }

}

// bool DeviceManager::createCommandBuffers() {
//     auto createCommandBuffer = [this](QueueUtils& queue) {
//         VkCommandPoolCreateInfo poolInfo{};
//         poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
//         poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
//         poolInfo.queueFamilyIndex = queue.queueFamilyIndex;
//
//         coronaHardwareCheck(vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &queue.commandPool));
//
//         VkCommandBufferAllocateInfo allocInfo{};
//         allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
//         allocInfo.commandPool = queue.commandPool;
//         allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//         allocInfo.commandBufferCount = 1;
//
//         coronaHardwareCheck(vkAllocateCommandBuffers(logicalDevice, &allocInfo, &queue.commandBuffer));
//     };
//
//     for (auto& queue : graphicsQueues) createCommandBuffer(queue);
//     for (auto& queue : computeQueues) createCommandBuffer(queue);
//     for (auto& queue : transferQueues) createCommandBuffer(queue);
//
//     return true;
// }

std::vector<DeviceManager::QueueUtils> DeviceManager::pickAvailableQueues(std::function<bool(const QueueUtils &)> predicate) const
{
    std::vector<QueueUtils> result;

    auto addMatchingQueues = [&](const std::vector<QueueUtils> &queues) {
        for (const auto &queue : queues)
        {
            if (predicate(queue))
            {
                result.push_back(queue);
            }
        }
    };

    addMatchingQueues(graphicsQueues);
    addMatchingQueues(computeQueues);
    addMatchingQueues(transferQueues);

    return result;
}

DeviceManager::ExternalSemaphoreHandle DeviceManager::exportSemaphore(VkSemaphore &semaphore)
{
    ExternalSemaphoreHandle handleInfo{};

#if _WIN32 || _WIN64
    VkSemaphoreGetWin32HandleInfoKHR getHandleInfo{};
    getHandleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    getHandleInfo.pNext = nullptr;
    getHandleInfo.semaphore = semaphore;
    getHandleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    HANDLE handle = nullptr;
    coronaHardwareCheck(vkGetSemaphoreWin32HandleKHR(logicalDevice, &getHandleInfo, &handle));

    if (handle == nullptr)
    {
        return handleInfo;
    }

    handleInfo.handle = handle;
#elif __linux__
    VkSemaphoreGetFdInfoKHR getFdInfo{};
    getFdInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    getFdInfo.pNext = nullptr;
    getFdInfo.semaphore = semaphore;
    getFdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

    int fd = -1;
    coronaHardwareCheck(vkGetSemaphoreFdKHR(logicalDevice, &getFdInfo, &fd));

    if (fd < 0)
    {
        throw std::runtime_error("Failed to export semaphore: fd is invalid");
    }

    handleInfo.fd = fd;
#else
    throw std::runtime_error("Semaphore export not implemented for this platform");
#endif

    return handleInfo;
}

VkSemaphore DeviceManager::getOrImportTimelineSemaphore(const QueueUtils &foreignQueue) const
{
    // 同设备：直接返回原始 semaphore，零开销
    if (foreignQueue.deviceManager == this ||
        foreignQueue.deviceManager->logicalDevice == logicalDevice)
    {
        return foreignQueue.timelineSemaphore;
    }

    // 跨设备：从预导入缓存查表
    auto it = importedTimelineSemaphores.find(foreignQueue.timelineSemaphore);
    if (it != importedTimelineSemaphores.end())
    {
        return it->second;
    }

    //// 跨设备 semaphore 未预导入：可能是不兼容的设备对（如不同架构 GPU），
    //// 调用方应检查返回值并回退到 CPU 侧同步
    //CFW_LOG_WARNING("[DeviceManager] Cross-device semaphore not pre-imported (foreign={} on device={}). "
    //                "Devices may not support opaque handle sharing. Caller should fall back to CPU-side sync.",
    //                reinterpret_cast<uintptr_t>(foreignQueue.timelineSemaphore),
    //                reinterpret_cast<uintptr_t>(logicalDevice));
    return VK_NULL_HANDLE;
}

void DeviceManager::importForeignSemaphores(const std::vector<DeviceManager *> &otherDevices)
{
    auto importFromQueues = [&](const std::vector<QueueUtils> &foreignQueues, DeviceManager *foreignDevice) {
        for (const auto &foreignQueue : foreignQueues)
        {
            if (importedTimelineSemaphores.count(foreignQueue.timelineSemaphore))
            {
                continue;
            }

            VkSemaphore foreignSem = foreignQueue.timelineSemaphore;
            ExternalSemaphoreHandle handle = foreignDevice->exportSemaphore(foreignSem);
            if(handle.handle == nullptr)
            {
                CFW_LOG_ERROR("[DeviceManager] Failed to export timeline semaphore from foreign device {}. Skipping import.",
                              reinterpret_cast<uintptr_t>(foreignDevice->logicalDevice));
                return;
            }

            VkSemaphoreTypeCreateInfo typeInfo{};
            typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            typeInfo.initialValue = 0;
            typeInfo.pNext = nullptr;

            VkSemaphoreCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            createInfo.pNext = &typeInfo;
            createInfo.flags = 0;

            VkSemaphore localSemaphore = VK_NULL_HANDLE;
            coronaHardwareCheck(vkCreateSemaphore(logicalDevice, &createInfo, nullptr, &localSemaphore));

#if _WIN32 || _WIN64
            VkImportSemaphoreWin32HandleInfoKHR importInfo{};
            importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
            importInfo.pNext = nullptr;
            importInfo.semaphore = localSemaphore;
            importInfo.flags = 0;
            importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            importInfo.handle = handle.handle;
            importInfo.name = nullptr;

            VkResult result = vkImportSemaphoreWin32HandleKHR(logicalDevice, &importInfo);
            if (result != VK_SUCCESS)
            {
                vkDestroySemaphore(logicalDevice, localSemaphore, nullptr);
                CloseHandle(handle.handle);
                CFW_LOG_ERROR("[DeviceManager] Failed to import foreign timeline semaphore: VkResult={}",
                              static_cast<int>(result));
                return;
            }

            CloseHandle(handle.handle);
#elif __linux__
            VkImportSemaphoreFdInfoKHR importInfo{};
            importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
            importInfo.pNext = nullptr;
            importInfo.semaphore = localSemaphore;
            importInfo.flags = 0;
            importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
            importInfo.fd = handle.fd;

            VkResult result = vkImportSemaphoreFdKHR(logicalDevice, &importInfo);
            if (result != VK_SUCCESS)
            {
                vkDestroySemaphore(logicalDevice, localSemaphore, nullptr);
                close(handle.fd);
                CFW_LOG_ERROR("[DeviceManager] Failed to import foreign timeline semaphore fd");
                continue;
            }
#endif

            importedTimelineSemaphores[foreignQueue.timelineSemaphore] = localSemaphore;

            CFW_LOG_DEBUG("[DeviceManager] Pre-imported cross-device timeline semaphore: foreign={} -> local={} on device={}",
                          reinterpret_cast<uintptr_t>(foreignQueue.timelineSemaphore),
                          reinterpret_cast<uintptr_t>(localSemaphore),
                          reinterpret_cast<uintptr_t>(logicalDevice));
        }
    };

    for (DeviceManager *other : otherDevices)
    {
        if (other == this || other->logicalDevice == logicalDevice)
        {
            continue;
        }

        VkSemaphoreTypeCreateInfo timelineTypeInfo{};
        timelineTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineTypeInfo.pNext = nullptr;
        timelineTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineTypeInfo.initialValue = 0; 

        VkPhysicalDeviceExternalSemaphoreInfo localExternalSemInfo{};
        localExternalSemInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO;
        localExternalSemInfo.pNext = &timelineTypeInfo;

#if _WIN32 || _WIN64
        localExternalSemInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#elif __linux__
        localExternalSemInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

        VkExternalSemaphoreProperties localExternalSemProps{};
        localExternalSemProps.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
        localExternalSemProps.pNext = nullptr;
        vkGetPhysicalDeviceExternalSemaphoreProperties(physicalDevice, &localExternalSemInfo, &localExternalSemProps);

        bool localCanImport = (localExternalSemProps.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) != 0;

        VkExternalSemaphoreProperties foreignExternalSemProps{};
        foreignExternalSemProps.sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
        foreignExternalSemProps.pNext = nullptr;
        vkGetPhysicalDeviceExternalSemaphoreProperties(other->physicalDevice, &localExternalSemInfo, &foreignExternalSemProps);

        bool foreignCanExport = (foreignExternalSemProps.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) != 0;

        if (!localCanImport || !foreignCanExport)
        {
            //CFW_LOG_WARNING("[DeviceManager] Cross-device semaphore sharing not supported between "
            //                "local device={} (import={}) and foreign device={} (export={}). "
            //                "Likely different GPU architectures (e.g. GTX 1080 + RTX 2080). "
            //                "Cross-device synchronization will fall back to CPU-side wait.",
            //                reinterpret_cast<uintptr_t>(physicalDevice), localCanImport,
            //                reinterpret_cast<uintptr_t>(other->physicalDevice), foreignCanExport);
            continue;
        }

        importFromQueues(other->graphicsQueues, other);
        importFromQueues(other->computeQueues, other);
        importFromQueues(other->transferQueues, other);
    }

    //CFW_LOG_INFO("[DeviceManager] Pre-imported {} foreign timeline semaphores on device {}",
    //             importedTimelineSemaphores.size(),
    //             reinterpret_cast<uintptr_t>(logicalDevice));
}