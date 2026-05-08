#include "DisplayManager.h"

#include <algorithm>
#include <optional>

#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/ResourcePool.h"
#include "corona/kernel/memory/cache_aligned_allocator.h"

//#define USE_SAME_DEVICE

DisplayManager::DisplayManager() = default;

DisplayManager::~DisplayManager()
{
    cleanUpDisplayManager();
}

void DisplayManager::cleanUpDisplayManager()
{
    VkDevice device = (displayDevice ? displayDevice->deviceManager.getLogicalDevice() : VK_NULL_HANDLE);

    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);
    }

    // 按照正确的顺序清理资源
    cleanupSyncObjects();
    cleanupDisplayImage();
    cleanupStagingBuffers();
    cleanupSwapChainImages();

    // 清理交换链
    if (swapChain != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }

    // 清理 Surface（使用实例销毁）
    if (vkSurface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(globalHardwareContext.getVulkanInstance(), vkSurface, nullptr);
        vkSurface = VK_NULL_HANDLE;
    }

    // 清理宿主内存
    if (hostBufferPtr != nullptr)
    {
        Corona::Kernal::Memory::aligned_free(hostBufferPtr);
        hostBufferPtr = nullptr;
    }

    // 清理状态
    presentQueues.clear();
    mainDeviceExecutor.reset();
    //displayDeviceExecutors.clear();
    displayDeviceExecutor.reset();
    waitedExecutor.reset();
    displaySurface = nullptr;
    displaySize = {0, 0};
    currentFrame = 0;
}

void DisplayManager::cleanupSyncObjects()
{
    VkDevice device = displayDevice ? displayDevice->deviceManager.getLogicalDevice() : VK_NULL_HANDLE;
    if (device == VK_NULL_HANDLE)
        return;

    for (auto &fence : acquireFences)
    {
        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
    }
    acquireFences.clear();

    // for (auto& fence : copyFences) {
    //     if (fence != VK_NULL_HANDLE) {
    //         vkDestroyFence(device, fence, nullptr);
    //         fence = VK_NULL_HANDLE;
    //     }
    // }
    // copyFences.clear();

    for (auto &sem : imageAvailableSemaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
        }
    }
    imageAvailableSemaphores.clear();

    for (auto &sem : renderFinishedSemaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
        }
    }
    renderFinishedSemaphores.clear();
}

void DisplayManager::cleanupSwapChainImages()
{
    VkDevice device = displayDevice ? displayDevice->deviceManager.getLogicalDevice() : VK_NULL_HANDLE;
    if (device == VK_NULL_HANDLE)
        return;

    for (auto &image : swapChainImages)
    {
        if (image.imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, image.imageView, nullptr);
            image.imageView = VK_NULL_HANDLE;
        }
        // 交换链图像由交换链管理，不需要手动销毁
        image.imageHandle = VK_NULL_HANDLE;
        image.imageAlloc = VK_NULL_HANDLE;
    }
    swapChainImages.clear();
}

void DisplayManager::cleanupStagingBuffers()
{
    if (srcStaging.bufferHandle != VK_NULL_HANDLE && srcStaging.resourceManager)
    {
        srcStaging.resourceManager->destroyBuffer(srcStaging);
        srcStaging = {};
    }

    if (dstStaging.bufferHandle != VK_NULL_HANDLE && dstStaging.resourceManager)
    {
        dstStaging.resourceManager->destroyBuffer(dstStaging);
        dstStaging = {};
    }
}

void DisplayManager::cleanupDisplayImage()
{
    if (displayImage.imageHandle != VK_NULL_HANDLE &&
        displayImage.imageAlloc != VK_NULL_HANDLE &&
        displayDevice)
    {
        // displayDevice->resourceManager.destroyImage(displayImage);
        displayImage = {};
    }
}

bool DisplayManager::initDisplayManager(void *surface)
{
    if (surface == nullptr)
    {
        return false;
    }

    try
    {
        createVkSurface(surface);
        choosePresentDevice();

        if (!displayDevice || presentQueues.empty())
        {
            throw std::runtime_error("Failed to find suitable present device/queue");
        }

        createSwapChain();
        createSyncObjects();
        return true;
    }
    catch (const std::exception &e)
    {
        // 初始化失败时清理
        cleanUpDisplayManager();
        return false;
    }
}

void DisplayManager::createSyncObjects()
{
    const size_t imageCount = swapChainImages.size();
    if (imageCount == 0)
    {
        throw std::runtime_error("Cannot create sync objects: no swapchain images");
    }

    imageAvailableSemaphores.resize(imageCount);
    renderFinishedSemaphores.resize(imageCount);
    acquireFences.resize(imageCount);  // 用于 vkAcquireNextImageKHR 同步

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // 首帧无需等待

    VkDevice device = displayDevice->deviceManager.getLogicalDevice();

    for (size_t i = 0; i < imageCount; i++)
    {
        coronaHardwareCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]));
        coronaHardwareCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]));
        coronaHardwareCheck(vkCreateFence(device, &fenceInfo, nullptr, &acquireFences[i]));
    }

    // CFW_LOG_INFO("Using Acquire Fence for present synchronization (VK_EXT_swapchain_maintenance1 removed)");

    /*displayDeviceExecutors.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++)
    {
        displayDeviceExecutors[i] = std::make_shared<HardwareExecutorVulkan>(displayDevice);
    }*/
}

void DisplayManager::createVkSurface(void *surface)
{
    if (surface == nullptr)
    {
        throw std::runtime_error("Surface pointer is null");
    }

#if _WIN32 || _WIN64
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = static_cast<HWND>(surface);
    createInfo.hinstance = GetModuleHandle(nullptr);

    coronaHardwareCheck(vkCreateWin32SurfaceKHR(globalHardwareContext.getVulkanInstance(),
                                                &createInfo,
                                                nullptr,
                                                &vkSurface));

#elif __APPLE__
    VkMacOSSurfaceCreateInfoMVK createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
    createInfo.pView = surface;

    coronaHardwareCheck(vkCreateMacOSSurfaceMVK(globalHardwareContext.getVulkanInstance(),
                                                &createInfo,
                                                nullptr,
                                                &vkSurface););
#elif __linux__
    // TODO: Linux surface creation
    throw std::runtime_error("Linux surface creation not implemented");
#endif
}

void DisplayManager::choosePresentDevice()
{
#ifdef USE_SAME_DEVICE
    displayDevice = globalHardwareContext.getMainDevice();

    auto pickQueuesRoles = [&](const DeviceManager::QueueUtils &queues) -> bool {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(displayDevice->deviceManager.getPhysicalDevice(),
                                             queues.queueFamilyIndex,
                                             vkSurface,
                                             &presentSupport);
        return presentSupport;
    };

    presentQueues = displayDevice->deviceManager.pickAvailableQueues(pickQueuesRoles);
#else
    // 优先选择与主设备不同的设备（如果支持）
    for (const auto &device : globalHardwareContext.getAllDevices())
    {
        auto pickQueuesRoles = [&](const DeviceManager::QueueUtils &queues) -> bool {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device->deviceManager.getPhysicalDevice(),
                                                 queues.queueFamilyIndex,
                                                 vkSurface,
                                                 &presentSupport);
            return presentSupport;
        };

        auto queues = device->deviceManager.pickAvailableQueues(pickQueuesRoles);
        if (!queues.empty())
        {
            displayDevice = device;
            presentQueues = std::move(queues);

            // 如果找到与主设备不同的设备，优先使用
            if (displayDevice != globalHardwareContext.getMainDevice())
            {
                break;
            }
        }
    }
#endif

    if (!displayDevice || presentQueues.empty())
    {
        throw std::runtime_error("Failed to find suitable present device");
    }

    mainDeviceExecutor = std::make_shared<HardwareExecutorVulkan>(globalHardwareContext.getMainDevice());
    displayDeviceExecutor = std::make_shared<HardwareExecutorVulkan>(displayDevice);
}

void DisplayManager::createSwapChain()
{
    VkSurfaceCapabilitiesKHR capabilities;
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        displayDevice->deviceManager.getPhysicalDevice(), vkSurface, &capabilities);

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to get surface capabilities");
    }

    displaySize = ktm::uvec2{std::clamp(capabilities.currentExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                             std::clamp(capabilities.currentExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};

    // 选择 Surface Format
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(displayDevice->deviceManager.getPhysicalDevice(),
                                         vkSurface, &formatCount, nullptr);

    if (formatCount == 0)
    {
        throw std::runtime_error("No surface formats available");
    }

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(displayDevice->deviceManager.getPhysicalDevice(),
                                         vkSurface, &formatCount, formats.data());

    surfaceFormat = formats[0];
    for (const auto &availableFormat : formats)
    {
        if (availableFormat.format == VK_FORMAT_R8G8B8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = availableFormat;
            break;
        }
    }

    // 选择 Present Mode
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(displayDevice->deviceManager.getPhysicalDevice(),
                                              vkSurface, &presentModeCount, nullptr);

    if (presentModeCount > 0)
    {
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(displayDevice->deviceManager.getPhysicalDevice(),
                                                  vkSurface, &presentModeCount, presentModes.data());

        for (const auto &mode : presentModes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = mode;
                break;
            }
        }
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vkSurface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = {displaySize.x, displaySize.y};
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if ((capabilities.supportedUsageFlags & createInfo.imageUsage) != createInfo.imageUsage)
    {
        throw std::runtime_error("Swapchain does not support required image usage flags");
    }

    std::vector<uint32_t> queueFamilies(displayDevice->deviceManager.getQueueFamilyNumber());
    std::iota(queueFamilies.begin(), queueFamilies.end(), 0);

    if (queueFamilies.size() > 1)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilies.size());
        createInfo.pQueueFamilyIndices = queueFamilies.data();
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = swapChain;

    VkSwapchainKHR oldSwapChain = swapChain;
    result = vkCreateSwapchainKHR(displayDevice->deviceManager.getLogicalDevice(),
                                  &createInfo, nullptr, &swapChain);

    // 清理旧的交换链
    if (oldSwapChain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(displayDevice->deviceManager.getLogicalDevice(), oldSwapChain, nullptr);
    }

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swap chain");
    }

    // 获取交换链图像
    vkGetSwapchainImagesKHR(displayDevice->deviceManager.getLogicalDevice(),
                            swapChain, &imageCount, nullptr);

    std::vector<VkImage> swapChainVkImages(imageCount);
    vkGetSwapchainImagesKHR(displayDevice->deviceManager.getLogicalDevice(),
                            swapChain, &imageCount, swapChainVkImages.data());

    swapChainImages.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        swapChainImages[i].imageHandle = swapChainVkImages[i];
        swapChainImages[i].imageSize = displaySize;
        swapChainImages[i].imageFormat = surfaceFormat.format;
        swapChainImages[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapChainImages[i].arrayLayers = 1;
        swapChainImages[i].mipLevels = 1;
        swapChainImages[i].device = &displayDevice->deviceManager;
        swapChainImages[i].resourceManager = &displayDevice->resourceManager;
        swapChainImages[i].pixelSize = 4; // RGBA8

        swapChainImages[i].imageView = displayDevice->resourceManager.createImageView(swapChainImages[i]);
    }
}

void DisplayManager::recreateSwapChain()
{
    VkDevice device = displayDevice->deviceManager.getLogicalDevice();
    vkDeviceWaitIdle(device);

    cleanupSyncObjects();
    //displayDeviceExecutors.clear();
    cleanupSwapChainImages();

    createSwapChain();
    createSyncObjects();
}

bool DisplayManager::needsSwapChainRecreation(const ktm::uvec2 &newSize) const
{
    return newSize != displaySize;
}

void DisplayManager::setupCrossDeviceTransfer(const ResourceManager::ImageHardwareWrap &sourceImage)
{
    VkDeviceSize imageSizeBytes = static_cast<VkDeviceSize>(sourceImage.imageSize.x) *
                                  sourceImage.imageSize.y * sourceImage.pixelSize;

    // 计算对齐要求
    uint64_t requiredAlign = 1;
    {
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT hostProps{};
        hostProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &hostProps;

        vkGetPhysicalDeviceProperties2(globalHardwareContext.getMainDevice()->deviceManager.getPhysicalDevice(), &props2);
        requiredAlign = std::max(requiredAlign, hostProps.minImportedHostPointerAlignment);

        vkGetPhysicalDeviceProperties2(displayDevice->deviceManager.getPhysicalDevice(), &props2);
        requiredAlign = std::max(requiredAlign, hostProps.minImportedHostPointerAlignment);
    }

    // P0 修复：将缓冲区大小向上对齐到 minImportedHostPointerAlignment
    // Vulkan spec 要求 VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT 导入的
    // 宿主内存区域大小必须是 minImportedHostPointerAlignment 的整数倍
    VkDeviceSize alignedSize = (imageSizeBytes + requiredAlign - 1) & ~(requiredAlign - 1);

    // 分配宿主内存
    if (hostBufferPtr != nullptr)
    {
        Corona::Kernal::Memory::aligned_free(hostBufferPtr);
    }
    hostBufferPtr = Corona::Kernal::Memory::aligned_malloc(alignedSize, requiredAlign);

    if (hostBufferPtr == nullptr)
    {
        throw std::runtime_error("Failed to allocate host buffer");
    }

    // 创建源和目标暂存缓冲区
    cleanupStagingBuffers();

    srcStaging = globalHardwareContext.getMainDevice()->resourceManager.importHostBuffer(hostBufferPtr, alignedSize);
    dstStaging = displayDevice->resourceManager.importHostBuffer(hostBufferPtr, alignedSize);

    /*srcStaging = globalHardwareContext.getMainDevice()->resourceManager.createBuffer(imageSizeBytes, 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
    ResourceManager::ExternalMemoryHandle memHandle = globalHardwareContext.getMainDevice()->resourceManager.exportBufferMemory(srcStaging);
    dstStaging = displayDevice->resourceManager.importBufferMemory(memHandle, srcStaging.elementCount, srcStaging.elementSize,srcStaging.bufferAllocInfo.size,srcStaging.bufferUsage);*/
}

bool DisplayManager::waitExecutor(HardwareExecutorVulkan &executor)
{
    waitedExecutor = std::make_shared<HardwareExecutorVulkan>(executor);
    return true;
}

bool DisplayManager::displayFrame(void *surface, HardwareImage displayImage)
{
    if (surface == nullptr)
    {
        return false;
    }

    try
    {
        // 检查是否需要重新初始化
        if (auto const handle = globalImageStorages.acquire_read(displayImage.getImageID());
            this->displaySurface != surface)
        {
            this->displaySurface = surface;

            if (vkSurface != VK_NULL_HANDLE || swapChain != VK_NULL_HANDLE)
            {
                cleanUpDisplayManager();
            }

            if (!initDisplayManager(surface))
            {
                return false;
            }

            // 设置跨设备传输
            if (globalHardwareContext.getMainDevice() != displayDevice)
            // if (true)
            {
                this->displayImage = displayDevice->resourceManager.createImage(handle->imageSize, handle->imageFormat, handle->pixelSize, handle->imageUsage);

                setupCrossDeviceTransfer(*handle);
            }
            else
            {
                this->displayImage = *handle;
            }
        }

        // 等待之前的执行器
        if (waitedExecutor)
        {
            mainDeviceExecutor->wait(*waitedExecutor);
            //displayDeviceExecutors[currentFrame]->wait(*waitedExecutor);
            displayDeviceExecutor->wait(*waitedExecutor);
        }

        // 检查交换链是否需要重建
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(displayDevice->deviceManager.getPhysicalDevice(), vkSurface, &capabilities);

        ktm::uvec2 newSize{std::clamp(capabilities.currentExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                           std::clamp(capabilities.currentExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};

        if (newSize.x == 0u || newSize.y == 0u)
        {
            return false;
        }

        if (needsSwapChainRecreation(newSize))
        {
            recreateSwapChain();
        }

        // 等待 Acquire Fence (替代 Present Fence，用于确保图像可用)
        VkDevice device = displayDevice->deviceManager.getLogicalDevice();
        
        VkResult fenceResult = vkWaitForFences(device, 1, &acquireFences[currentFrame], VK_TRUE, UINT64_MAX);
        if (fenceResult != VK_SUCCESS)
        {
            CFW_LOG_ERROR("Failed to wait for acquire fence: {}", static_cast<int>(fenceResult));
            return false;
        }
        vkResetFences(device, 1, &acquireFences[currentFrame]);

        // 获取交换链图像（fence 不再传给 acquire，改为绑定到 queue submit 以保证 binary semaphore 的 wait 完成）
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device,
                                                swapChain,
                                                UINT64_MAX,
                                                imageAvailableSemaphores[currentFrame],
                                                VK_NULL_HANDLE,
                                                &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain();
            return true;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        // 跨设备传输（如果需要）
        // copyCmd2 必须在 commit() 之前保持存活，提升到外层作用域
        std::optional<CopyBufferToImageCommand> copyCmd2;
        if (auto const handle = globalImageStorages.acquire_write(displayImage.getImageID());
            globalHardwareContext.getMainDevice() != displayDevice)
        // if (true)
        {
            CopyImageToBufferCommand copyCmd(*handle, srcStaging);
            (*mainDeviceExecutor) << &copyCmd << mainDeviceExecutor->commit();

            // GPU B 等待 GPU A 的拷贝完成（通过 imported timeline semaphore，纯 GPU 端同步）
            displayDeviceExecutor->wait(*mainDeviceExecutor);

            copyCmd2.emplace(dstStaging, this->displayImage);
            (*displayDeviceExecutor) << &copyCmd2.value();
        }

        std::vector<VkSemaphoreSubmitInfo> waitSemaphoreInfos;
        {
            VkSemaphoreSubmitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfo.pNext = nullptr;
            waitInfo.semaphore = imageAvailableSemaphores[currentFrame];
            waitInfo.value = 0;
            waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            waitSemaphoreInfos.push_back(waitInfo);
        }

        std::vector<VkSemaphoreSubmitInfo> signalSemaphoreInfos;
        {
            VkSemaphoreSubmitInfo signalInfo{};
            signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfo.pNext = nullptr;
            signalInfo.semaphore = renderFinishedSemaphores[imageIndex];
            signalInfo.value = 0;
            signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalSemaphoreInfos.push_back(signalInfo);
        }

        // 执行 Blit 和转换布局
        BlitImageCommand blitCmd(this->displayImage, swapChainImages[imageIndex]);
        TransitionImageLayoutCommand transitionCmd(swapChainImages[imageIndex],
                                                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                   VK_ACCESS_2_NONE);

        /**displayDeviceExecutors[currentFrame] << &blitCmd << &transitionCmd
                                              << displayDeviceExecutors[currentFrame]->wait(waitSemaphoreInfos, signalSemaphoreInfos)
                                              << displayDeviceExecutors[currentFrame]->commit();*/

        displayDeviceExecutor->waitFence = acquireFences[currentFrame];
        *displayDeviceExecutor << &blitCmd << &transitionCmd
                               << displayDeviceExecutor->wait(waitSemaphoreInfos, signalSemaphoreInfos)
                               << displayDeviceExecutor->commit();

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphores[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pNext = nullptr;  // 不再使用 VkSwapchainPresentFenceInfoKHR

        // 移除: VK_EXT_swapchain_maintenance1 相关代码
        // VkSwapchainPresentFenceInfoKHR presentFenceInfo{};
        // presentFenceInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR;
        // presentFenceInfo.swapchainCount = 1;
        // presentFenceInfo.pFences = &presentFences[currentFrame];
        // presentFenceInfo.pNext = const_cast<void *>(presentInfo.pNext);
        // presentInfo.pNext = &presentFenceInfo;

        auto commitToQueue = [&](DeviceManager::QueueUtils *currentRecordQueue) -> bool {
            // 让 pickQueueAndCommit 后续提交使用正确的 present 队列
            displayDeviceExecutor->currentRecordQueue = currentRecordQueue;

            // 1. 先执行 Present（只依赖 binary semaphore）
            VkResult presentResult = vkQueuePresentKHR(currentRecordQueue->vkQueue, &presentInfo);

            if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
            {
                CFW_LOG_ERROR("vkQueuePresentKHR failed: {}", static_cast<int>(presentResult));
                return false;
            }

            // 2. 提交一个空的命令缓冲区来推进 timeline
            // 这确保 pickQueueAndCommit 增加的 timelineValue 能被 GPU signal
            vkResetCommandBuffer(currentRecordQueue->commandBuffer, 0);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(currentRecordQueue->commandBuffer, &beginInfo);
            vkEndCommandBuffer(currentRecordQueue->commandBuffer); // 空命令

            return true;
        };

        displayDeviceExecutor->pickQueueAndCommit(currentQueueIndex, presentQueues, commitToQueue);
        currentFrame = (currentFrame + 1) % swapChainImages.size();
        return true;
    }
    catch (const std::exception &e)
    {
        return false;
    }
}