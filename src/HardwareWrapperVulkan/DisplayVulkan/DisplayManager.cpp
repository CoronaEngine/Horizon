#include "DisplayManager.h"

#include <algorithm>
#include <optional>

#include "HardwareWrapperVulkan/HardwareVulkan/ResourceAccess.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceCommand.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include <new>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

namespace {
struct PresentImageCommand final : CommandRecordVulkan
{
    ResourceManager::ImageHardwareWrap& source;
    VkImage target;
    ktm::uvec2 extent;

    PresentImageCommand(ResourceManager::ImageHardwareWrap& src, VkImage dst, ktm::uvec2 size)
        : source(src), target(dst), extent(size) {}

    ExecutorType getExecutorType() override { return ExecutorType::Graphics; }

    void resolveResourceUses(ResourceAccessList& uses) override
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
    displayDeviceExecutor.reset();

    // 按照正确的顺序清理资源
    cleanupSyncObjects();
    cleanupCrossDeviceTransfer();
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

    // 清理状态
    presentQueue = nullptr;
    displayDevice.reset();
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

void DisplayManager::cleanupCrossDeviceTransfer()
{
    // A source copy can already be in flight if submission on the display device failed.
    if (sourceCopy)
    {
        VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount = 1;
        wait.pSemaphores = &sourceCopy.queue->timelineSemaphore;
        wait.pValues = &sourceCopy.value;
        vkWaitSemaphores(sourceCopy.queue->deviceManager->getLogicalDevice(), &wait, UINT64_MAX);
        sourceCopy = {};
    }
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
    if (hostBufferPtr)
    {
        ::operator delete(hostBufferPtr, std::align_val_t(hostBufferAlignment));
        hostBufferPtr = nullptr;
    }
    if (displayImage.resourceManager)
    {
        displayImage.resourceManager->destroyImage(displayImage);
        displayImage = {};
    }
}

bool DisplayManager::initDisplayManager(void *surface)
{
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
    // MoltenVK 只有拿到 CAMetalLayer 才能创建窗口表面，否则
    // vkCreateMacOSSurfaceMVK 返回 VK_ERROR_SURFACE_LOST_KHR。
    // GLFW 的 Cocoa view 默认没有 Metal layer，这里补上。
    NSView *metalView = (__bridge NSView *)surface;
    if (![metalView.layer isKindOfClass:[CAMetalLayer class]])
    {
        CAMetalLayer *metalLayer = [CAMetalLayer layer];
        metalLayer.device = MTLCreateSystemDefaultDevice();
        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        [metalView setWantsLayer:YES];
        [metalView setLayer:metalLayer];
    }

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
    const auto renderDevice = getHardwareContext().getMainDevice();
    for (const auto& device : getHardwareContext().getAllDevices())
    {
        const auto* queue = device->deviceManager.findPresentQueue(vkSurface, blitQueueFamily);
        if (!queue) continue;
        displayDevice = device;
        presentQueue = queue;
        if (device == renderDevice) break;
    }

    if (!displayDevice || !presentQueue)
    {
        throw std::runtime_error("Failed to find suitable present device");
    }

    if (displayDevice != renderDevice)
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
    createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

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
    swapChainImages.clear();

    createSwapChain();
    createSyncObjects();
    currentFrame = 0;
}

void DisplayManager::setupCrossDeviceTransfer(const ResourceManager::ImageHardwareWrap &sourceImage)
{
    displayDeviceExecutor->waitForAllDeferredResources();
    cleanupCrossDeviceTransfer();
    displayImage = displayDevice->resourceManager.createImage(sourceImage.imageSize, sourceImage.imageFormat, sourceImage.pixelSize, sourceImage.imageUsage);
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

    hostBufferAlignment = requiredAlign;
    hostBufferPtr = ::operator new(alignedSize, std::align_val_t(hostBufferAlignment));

    srcStaging = getHardwareContext().getMainDevice()->resourceManager.importHostBuffer(hostBufferPtr, alignedSize);
    dstStaging = displayDevice->resourceManager.importHostBuffer(hostBufferPtr, alignedSize);

    /*srcStaging = getHardwareContext().getMainDevice()->resourceManager.createBuffer(imageSizeBytes, 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
    ResourceManager::ExternalMemoryHandle memHandle = getHardwareContext().getMainDevice()->resourceManager.exportBufferMemory(srcStaging);
    dstStaging = displayDevice->resourceManager.importBufferMemory(memHandle, srcStaging.elementCount, srcStaging.elementSize,srcStaging.bufferAllocInfo.size,srcStaging.bufferUsage);*/
}

void DisplayManager::displayFrame(void *surface, const HardwareImage& source, HardwareExecutorVulkan& producer)
{
    try
    {
#ifdef _WIN32
        // Avoid a blocking acquire for minimized or zero-sized windows.
        RECT client{};
        if (!GetClientRect(static_cast<HWND>(surface), &client) || IsIconic(static_cast<HWND>(surface)) ||
            client.right == client.left || client.bottom == client.top) return;
#endif
        // 检查是否需要重新初始化
        auto* handle = ResourceManager::get(source);
        if (this->displaySurface != surface)
        {
            if (vkSurface != VK_NULL_HANDLE || swapChain != VK_NULL_HANDLE)
                cleanUpDisplayManager();

            if (!initDisplayManager(surface)) return;
            this->displaySurface = surface;
        }
        const bool crossDevice = producer.hardwareContext != displayDevice;
        if (crossDevice &&
            (!displayImage.imageHandle || displayImage.imageSize != handle->imageSize ||
             displayImage.imageFormat != handle->imageFormat))
            setupCrossDeviceTransfer(*handle);

#ifdef _WIN32
        // Query WSI again on resize; the steady-state path only reads the native client rect.
        if (client.right - client.left != static_cast<LONG>(displaySize.x) ||
            client.bottom - client.top != static_cast<LONG>(displaySize.y))
#endif
        {
            VkSurfaceCapabilitiesKHR caps{};
            const auto status = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(displayDevice->deviceManager.getPhysicalDevice(), vkSurface, &caps);
            if (status == VK_ERROR_SURFACE_LOST_KHR)
            {
                cleanUpDisplayManager();
                return;
            }
            coronaHardwareCheck(status);
            if (!caps.currentExtent.width || !caps.currentExtent.height) return;
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

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)
        {
            cleanUpDisplayManager();
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        // Graphics work on this device can share the blit's command buffer.
        if (crossDevice || std::ranges::any_of(producer.commandList, [](const auto& command) {
                return command.type != CommandRecordVulkan::ExecutorType::Graphics;
            }))
            producer.commit();

        auto sourceOwner = std::make_shared<HardwareImage>(source);
        if (crossDevice)
        {
            // Both devices import the same memory. Finish the last read before overwriting it.
            CopyImageToBufferCommand copy(*handle, srcStaging, 0, 0, 0, 1);
            producer.wait(*displayDeviceExecutor).record(&copy, std::move(sourceOwner));
            try
            {
                producer.commit();
            }
            catch (...)
            {
                producer.commandList.pop_back();
                throw;
            }
            sourceCopy = producer.getSubmissionPoint();
            displayDeviceExecutor->wait(sourceCopy);
        }

        VkSemaphoreSubmitInfo available{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        available.semaphore = imageAvailableSemaphores[currentFrame];
        available.stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        VkSemaphoreSubmitInfo finished{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        finished.semaphore = renderFinishedSemaphores[imageIndex];
        finished.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        auto& blit = crossDevice ? *displayDeviceExecutor : producer;
        PresentImageCommand presentImage(crossDevice ? displayImage : *handle, swapChainImages[imageIndex], displaySize);
        std::optional<CopyBufferToImageCommand> upload;
        const auto commands = blit.commandList.size();
        const auto waits = blit.waitSemaphores.size(), signals = blit.signalSemaphores.size();
        try
        {
            if (crossDevice)
                blit.record(&upload.emplace(dstStaging, displayImage, 0, 0, 0, 1));
            blit.waitSemaphores.push_back(available);
            blit.signalSemaphores.push_back(finished);
            blit.record(&presentImage, std::move(sourceOwner));
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
        if (crossDevice)
        {
            sourceCopy = {}; // The display submission now covers the source copy as well.
            producer.wait(blit).commit(true);
        }

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
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_ERROR_SURFACE_LOST_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
            (presentResult == VK_SUCCESS && result == VK_SUBOPTIMAL_KHR))
        {
            cleanUpDisplayManager();
            return;
        }
        coronaHardwareCheck(presentResult);
        currentFrame = (currentFrame + 1) % imageAvailableSemaphores.size();
    }
    catch (const std::exception &e)
    {
        CFW_LOG_ERROR("Display frame failed: {}", e.what());
        cleanUpDisplayManager();
        throw;
    }
}
