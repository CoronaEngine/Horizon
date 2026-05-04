#pragma once

#include <chrono>
#include <numeric>
#include <unordered_map>

#include "Horizon.h"
#include "HardwareWrapperVulkan/HardwareUtilsVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"

class DisplayManager
{
  public:
    DisplayManager();
    ~DisplayManager();

    // 禁止拷贝和移动
    DisplayManager(const DisplayManager &) = delete;
    DisplayManager &operator=(const DisplayManager &) = delete;
    DisplayManager(DisplayManager &&) = delete;
    DisplayManager &operator=(DisplayManager &&) = delete;

    bool initDisplayManager(void *surface);
    bool waitExecutor(HardwareExecutorVulkan &executor);
    bool displayFrame(void *surface, HardwareImage displayImage);

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
    std::vector<ResourceManager::ImageHardwareWrap> swapChainImages;
    ResourceManager::ImageHardwareWrap displayImage{};

    // 同步对象
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> acquireFences;

    // 队列和设备
    std::atomic_uint16_t currentQueueIndex{0};
    std::vector<DeviceManager::QueueUtils> presentQueues;
    std::shared_ptr<HardwareContext::HardwareUtils> displayDevice;

    // 跨设备传输资源
    void *hostBufferPtr = nullptr;
    ResourceManager::BufferHardwareWrap srcStaging{};
    ResourceManager::BufferHardwareWrap dstStaging{};

    // 执行器
    std::shared_ptr<HardwareExecutorVulkan> waitedExecutor;
    std::shared_ptr<HardwareExecutorVulkan> mainDeviceExecutor;
    std::shared_ptr<HardwareExecutorVulkan> displayDeviceExecutor;
    //std::vector<std::shared_ptr<HardwareExecutorVulkan>> displayDeviceExecutors;

    // 内部方法
    void cleanUpDisplayManager();
    void createVkSurface(void *surface);
    void choosePresentDevice();
    void createSyncObjects();
    void createSwapChain();
    void recreateSwapChain();

    void cleanupSyncObjects();
    void cleanupSwapChainImages();
    void cleanupStagingBuffers();
    void cleanupDisplayImage();

    bool needsSwapChainRecreation(const ktm::uvec2 &newSize) const;
    void setupCrossDeviceTransfer(const ResourceManager::ImageHardwareWrap &sourceImage);
};