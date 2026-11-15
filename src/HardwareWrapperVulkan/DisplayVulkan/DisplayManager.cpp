#include "DisplayManager.h"

#include <algorithm>
#include <optional>

#include "HardwareWrapperVulkan/HardwareVulkan/ResourceAccess.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include <new>

namespace {
struct PresentImageCommand final : CommandRecordVulkan
{
    ResourceManager::ImageHardwareWrap& source;
    VkImage target;
    ktm::uvec2 extent;

    PresentImageCommand(ResourceManager::ImageHardwareWrap& src, VkImage dst, ktm::uvec2 size)
        : source(src), target(dst), extent(size) {}

    ExecutorType getExecutorType() override { return ExecutorType::Graphics; }

    void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList& uses) override
    {
        const auto descriptors = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        uses.image(source, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            (source.imageUsage & descriptors) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            {source.aspectMask, source.baseMipLevel, 1, source.baseArrayLayer, 1});
    }

    void commitCommand(HardwareExecutorVulkan& executor) override
    {
        const VkCommandBuffer cb = executor.currentRecordQueue->commandBuffer;
        // The blit overwrites the entire acquired image. Chain its first transition to the acquire wait.
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cb, &dependency);

        VkImageBlit region{};
        region.srcSubresource = {source.aspectMask, source.baseMipLevel, source.baseArrayLayer, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {static_cast<int32_t>(source.imageSize.x), static_cast<int32_t>(source.imageSize.y), 1};
        region.dstOffsets[1] = {static_cast<int32_t>(extent.x), static_cast<int32_t>(extent.y), 1};
        vkCmdBlitImage(cb, source.imageHandle, source.imageLayout, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region, source.imageSize == extent ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);

        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(cb, &dependency);
    }
};
}

DisplayManager::DisplayManager(PresentDevicePreference preference) : devicePreference(preference) {}

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
    mainDeviceExecutor.reset();
    displayDeviceExecutor.reset();

    // 按照正确的顺序清理资源
    cleanupSyncObjects();
    cleanupDisplayImage();
    cleanupStagingBuffers();
    swapChainImages.clear();

    // 清理交换链
    if (swapChain != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }

    // 清理 Surface（使用实例销毁）
    if (vkSurface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(getHardwareContext().getVulkanInstance(), vkSurface, nullptr);
        vkSurface = VK_NULL_HANDLE;
    }

    // 清理宿主内存
    if (hostBufferPtr != nullptr)
    {
        ::operator delete(hostBufferPtr, std::align_val_t(hostBufferAlignment));
        hostBufferPtr = nullptr;
    }

    // 清理状态
    presentQueue = nullptr;
    waitedSubmission = {};
    displaySurface = nullptr;
    displaySize = {0, 0};
    currentFrame = 0;
}

void DisplayManager::cleanupSyncObjects()
{
    VkDevice device = displayDevice ? displayDevice->deviceManager.getLogicalDevice() : VK_NULL_HANDLE;
    if (device == VK_NULL_HANDLE)
        return;

    acquireConsumed.clear();

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

        if (!displayDevice || !presentQueue)
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
    acquireConsumed.resize(imageCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkDevice device = displayDevice->deviceManager.getLogicalDevice();

    for (auto& semaphore : imageAvailableSemaphores)
        coronaHardwareCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
    for (auto& semaphore : renderFinishedSemaphores)
        coronaHardwareCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
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

    auto instance = getHardwareContext().getVulkanInstance();
    auto createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (!createSurface) throw std::runtime_error("VK_KHR_win32_surface is unavailable");
    coronaHardwareCheck(createSurface(instance,
                                                &createInfo,
                                                nullptr,
                                                &vkSurface));

#elif __APPLE__
    VkMacOSSurfaceCreateInfoMVK createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
    createInfo.pView = surface;

    auto instance = getHardwareContext().getVulkanInstance();
    auto createSurface = reinterpret_cast<PFN_vkCreateMacOSSurfaceMVK>(
        vkGetInstanceProcAddr(instance, "vkCreateMacOSSurfaceMVK"));
    if (!createSurface) throw std::runtime_error("VK_MVK_macos_surface is unavailable");
    coronaHardwareCheck(createSurface(instance,
                                                &createInfo,
                                                nullptr,
                                                &vkSurface));
#elif __linux__
    // TODO: Linux surface creation
    throw std::runtime_error("Linux surface creation not implemented");
#endif
}

void DisplayManager::choosePresentDevice()
{
    const auto& renderDevice = getHardwareContext().getMainDevice();
    for (const auto& device : getHardwareContext().getAllDevices())
    {
        const auto* queue = device->deviceManager.findPresentQueue(vkSurface, blitQueueFamily);
        if (!queue) continue;
        displayDevice = device;
        presentQueue = queue;
        if ((device == renderDevice) == (devicePreference == PresentDevicePreference::RenderDevice)) break;
    }

    if (!displayDevice || !presentQueue)
    {
        throw std::runtime_error("Failed to find suitable present device");
    }

    mainDeviceExecutor = std::make_unique<HardwareExecutorVulkan>(getHardwareContext().getMainDevice());
    displayDeviceExecutor = std::make_unique<HardwareExecutorVulkan>(displayDevice);
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
    createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if ((capabilities.supportedUsageFlags & createInfo.imageUsage) != createInfo.imageUsage)
    {
        throw std::runtime_error("Swapchain does not support required image usage flags");
    }

    const uint32_t queueFamilies[] = {blitQueueFamily, presentQueue->queueFamilyIndex};
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (queueFamilies[0] != queueFamilies[1])
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilies;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = swapChain;

    VkSwapchainKHR replacement = VK_NULL_HANDLE;
    result = vkCreateSwapchainKHR(displayDevice->deviceManager.getLogicalDevice(),
                                  &createInfo, nullptr, &replacement);
    coronaHardwareCheck(result);
    if (swapChain) vkDestroySwapchainKHR(displayDevice->deviceManager.getLogicalDevice(), swapChain, nullptr);
    swapChain = replacement;

    // 获取交换链图像
    const VkDevice device = displayDevice->deviceManager.getLogicalDevice();
    coronaHardwareCheck(vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr));
    swapChainImages.resize(imageCount);
    coronaHardwareCheck(vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data()));
}

void DisplayManager::recreateSwapChain()
{
    VkDevice device = displayDevice->deviceManager.getLogicalDevice();
    vkDeviceWaitIdle(device);

    cleanupSyncObjects();
    //displayDeviceExecutors.clear();
    swapChainImages.clear();

    createSwapChain();
    createSyncObjects();
    currentFrame = 0;
}

void DisplayManager::setupCrossDeviceTransfer(const ResourceManager::ImageHardwareWrap &sourceImage)
{
    VkDeviceSize imageSizeBytes = static_cast<VkDeviceSize>(sourceImage.imageSize.x) *
                                  sourceImage.imageSize.y * sourceImage.pixelSize;

    // 计算对齐要求
    uint64_t requiredAlign = alignof(std::max_align_t);
    {
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT hostProps{};
        hostProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &hostProps;

        vkGetPhysicalDeviceProperties2(getHardwareContext().getMainDevice()->deviceManager.getPhysicalDevice(), &props2);
        requiredAlign = std::max(requiredAlign, hostProps.minImportedHostPointerAlignment);

        vkGetPhysicalDeviceProperties2(displayDevice->deviceManager.getPhysicalDevice(), &props2);
        requiredAlign = std::max(requiredAlign, hostProps.minImportedHostPointerAlignment);
    }

    // P0 修复：将缓冲区大小向上对齐到 minImportedHostPointerAlignment
    // Vulkan spec 要求 VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT 导入的
    // 宿主内存区域大小必须是 minImportedHostPointerAlignment 的整数倍
    VkDeviceSize alignedSize = (imageSizeBytes + requiredAlign - 1) & ~(requiredAlign - 1);

    // Release imported buffers before freeing their backing host allocation.
    cleanupStagingBuffers();
    if (hostBufferPtr != nullptr)
    {
        ::operator delete(hostBufferPtr, std::align_val_t(hostBufferAlignment));
        hostBufferPtr = nullptr;
    }
    hostBufferAlignment = requiredAlign;
    hostBufferPtr = ::operator new(alignedSize, std::align_val_t(hostBufferAlignment));

    srcStaging = getHardwareContext().getMainDevice()->resourceManager.importHostBuffer(hostBufferPtr, alignedSize);
    dstStaging = displayDevice->resourceManager.importHostBuffer(hostBufferPtr, alignedSize);

    /*srcStaging = getHardwareContext().getMainDevice()->resourceManager.createBuffer(imageSizeBytes, 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
    ResourceManager::ExternalMemoryHandle memHandle = getHardwareContext().getMainDevice()->resourceManager.exportBufferMemory(srcStaging);
    dstStaging = displayDevice->resourceManager.importBufferMemory(memHandle, srcStaging.elementCount, srcStaging.elementSize,srcStaging.bufferAllocInfo.size,srcStaging.bufferUsage);*/
}

bool DisplayManager::waitExecutor(const HardwareExecutorVulkan &executor)
{
    waitedSubmission = executor.getSubmissionPoint();
    return true;
}

bool DisplayManager::displayFrame(void *surface, const HardwareImage& displayImage, HardwareExecutorVulkan* producer)
{
    if (surface == nullptr || !displayImage)
    {
        return false;
    }

    try
    {
#ifdef _WIN32
        // Avoid a blocking acquire for minimized or zero-sized windows.
        RECT client{};
        if (!GetClientRect(static_cast<HWND>(surface), &client) || IsIconic(static_cast<HWND>(surface)) ||
            client.right == client.left || client.bottom == client.top) return false;
#endif
        // 检查是否需要重新初始化
        auto* handle = legacyImageFromId(displayImage.getImageID());
        if (this->displaySurface != surface)
        {
            if (vkSurface != VK_NULL_HANDLE || swapChain != VK_NULL_HANDLE)
            {
                auto submission = waitedSubmission;
                cleanUpDisplayManager();
                waitedSubmission = submission;
            }

            if (!initDisplayManager(surface))
            {
                return false;
            }
            this->displaySurface = surface;
        }
        const bool crossDevice = getHardwareContext().getMainDevice() != displayDevice;
        if (crossDevice &&
            (!this->displayImage.imageHandle || this->displayImage.imageSize != handle->imageSize ||
             this->displayImage.imageFormat != handle->imageFormat))
        {
            mainDeviceExecutor->waitForAllDeferredResources();
            displayDeviceExecutor->waitForAllDeferredResources();
            cleanupDisplayImage();
            this->displayImage = displayDevice->resourceManager.createImage(handle->imageSize, handle->imageFormat, handle->pixelSize, handle->imageUsage);
            setupCrossDeviceTransfer(*handle);
        }

#ifdef _WIN32
        // Query WSI again on resize; the steady-state path only reads the native client rect.
        if (client.right - client.left != static_cast<LONG>(displaySize.x) ||
            client.bottom - client.top != static_cast<LONG>(displaySize.y))
#endif
        {
            VkSurfaceCapabilitiesKHR caps{};
            coronaHardwareCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(displayDevice->deviceManager.getPhysicalDevice(), vkSurface, &caps));
            if (!caps.currentExtent.width || !caps.currentExtent.height) return false;
            const ktm::uvec2 extent{std::clamp(caps.currentExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width),
                                    std::clamp(caps.currentExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height)};
            if (extent != displaySize) recreateSwapChain();
        }
        VkDevice device = displayDevice->deviceManager.getLogicalDevice();
        // Reuse an acquire semaphore only after the GPU has consumed the previous wait on it.
        if (const auto& previous = acquireConsumed[currentFrame]; previous)
        {
            uint64_t completed;
            coronaHardwareCheck(vkGetSemaphoreCounterValue(device, previous.queue->timelineSemaphore, &completed));
            if (completed < previous.value)
            {
                VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
                wait.semaphoreCount = 1;
                wait.pSemaphores = &previous.queue->timelineSemaphore;
                wait.pValues = &previous.value;
                coronaHardwareCheck(vkWaitSemaphores(device, &wait, UINT64_MAX));
            }
        }

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
            return false;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        // 跨设备传输（如果需要）
        // copyCmd2 必须在 commit() 之前保持存活，提升到外层作用域
        std::optional<CopyBufferToImageCommand> copyCmd2;
        const bool fuse = producer && !crossDevice && producer->hardwareContext == displayDevice && !producer->commandList.empty() &&
            std::all_of(producer->commandList.begin(), producer->commandList.end(), [](const auto& cmd) {
                return cmd.type == CommandRecordVulkan::ExecutorType::Graphics;
            });
        auto& blit = fuse ? *producer : *displayDeviceExecutor;
        auto& reader = crossDevice ? *mainDeviceExecutor : blit;
        reader.wait(waitedSubmission);
        if (producer && !fuse)
        {
            producer->commit();
            reader.wait(*producer);
        }
        if (crossDevice)
        {
            // Both devices import the same staging allocation. Finish the previous read before overwriting it.
            mainDeviceExecutor->wait(*displayDeviceExecutor);
            CopyImageToBufferCommand copyCmd(*handle, srcStaging, 0, 0, 0, 1);
            // Read on the render queue so the next frame cannot overwrite the source during this copy.
            mainDeviceExecutor->record(&copyCmd);
            mainDeviceExecutor->commandList.back().type = CommandRecordVulkan::ExecutorType::Graphics;
            mainDeviceExecutor->commit();
            // Publish the source read before the display-device copy or present can fail.
            if (producer) producer->wait(*mainDeviceExecutor);

            // GPU B 等待 GPU A 的拷贝完成（通过 imported timeline semaphore，纯 GPU 端同步）
            displayDeviceExecutor->wait(*mainDeviceExecutor);

            copyCmd2.emplace(dstStaging, this->displayImage, 0, 0, 0, 1);
            (*displayDeviceExecutor) << &copyCmd2.value();
        }

        VkSemaphoreSubmitInfo available{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        available.semaphore = imageAvailableSemaphores[currentFrame];
        available.stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        VkSemaphoreSubmitInfo finished{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        finished.semaphore = renderFinishedSemaphores[imageIndex];
        finished.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        auto& source = crossDevice ? this->displayImage : *handle;
        PresentImageCommand presentImage(source, swapChainImages[imageIndex], displaySize);

        const auto commands = blit.commandList.size();
        const auto waits = blit.waitSemaphores.size(), signals = blit.signalSemaphores.size();
        try
        {
            blit.waitSemaphores.push_back(available);
            blit.signalSemaphores.push_back(finished);
            blit << &presentImage;
            blit.commit();
        }
        catch (...)
        {
            // Leave a caller-owned executor free of borrowed WSI commands and semaphores on retry.
            blit.commandList.resize(commands);
            blit.waitSemaphores.resize(waits);
            blit.signalSemaphores.resize(signals);
            throw;
        }
        acquireConsumed[currentFrame] = blit.getSubmissionPoint();
        if (producer && !crossDevice && !fuse) producer->wait(blit);

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphores[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapChain;
        presentInfo.pImageIndices = &imageIndex;
        VkResult presentResult;
        {
            std::lock_guard lock(*presentQueue->queueMutex);
            presentResult = vkQueuePresentKHR(presentQueue->vkQueue, &presentInfo);
        }
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
            (presentResult == VK_SUCCESS && result == VK_SUBOPTIMAL_KHR))
        {
            recreateSwapChain();
            return presentResult != VK_ERROR_OUT_OF_DATE_KHR;
        }
        coronaHardwareCheck(presentResult);
        currentFrame = (currentFrame + 1) % imageAvailableSemaphores.size();
        return true;
    }
    catch (const std::exception &e)
    {
        CFW_LOG_ERROR("Display frame failed: {}", e.what());
        cleanUpDisplayManager();
        return false;
    }
}
