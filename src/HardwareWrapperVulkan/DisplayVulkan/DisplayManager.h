#pragma once

#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"

class DisplayManager
{
  public:
    DisplayManager() = default;
    ~DisplayManager();

    // 禁止拷贝和移动
    DisplayManager(const DisplayManager &) = delete;
    DisplayManager &operator=(const DisplayManager &) = delete;
    DisplayManager(DisplayManager &&) = delete;
    DisplayManager &operator=(DisplayManager &&) = delete;

    void displayFrame(void *surface, const HardwareImage& source, HardwareExecutorVulkan& producer);

  private:
    // Vulkan 核心资源
    VkSurfaceKHR vkSurface{VK_NULL_HANDLE};
    VkSwapchainKHR swapChain{VK_NULL_HANDLE};
    VkSurfaceFormatKHR surfaceFormat{};

    // 显示参数
    ktm::uvec2 displaySize{0, 0};
    void *displaySurface{nullptr};
    uint32_t currentFrame{0};

    // 交换链资源
    std::vector<VkImage> swapChainImages;
    ResourceManager::ImageHardwareWrap displayImage{};

    // 同步对象
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<SubmissionPoint> acquireConsumed;

    // 队列和设备
    const DeviceManager::QueueUtils* presentQueue{};
    uint32_t blitQueueFamily{};
    std::shared_ptr<HardwareContext::HardwareUtils> displayDevice;

    // 跨设备传输资源
    size_t hostBufferAlignment = alignof(std::max_align_t);
    void *hostBufferPtr = nullptr;
    ResourceManager::BufferHardwareWrap srcStaging{};
    ResourceManager::BufferHardwareWrap dstStaging{};

    // 执行器
    SubmissionPoint sourceCopy;
    std::unique_ptr<HardwareExecutorVulkan> displayDeviceExecutor;

    // 内部方法
    bool initDisplayManager(void *surface);
    void cleanUpDisplayManager();
    void createVkSurface(void *surface);
    void choosePresentDevice();
    void createSyncObjects();
    void createSwapChain();
    void recreateSwapChain();

    void cleanupSyncObjects();
    void cleanupCrossDeviceTransfer();
    void setupCrossDeviceTransfer(const ResourceManager::ImageHardwareWrap &sourceImage);
};
