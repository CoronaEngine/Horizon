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
    explicit DisplayManager(PresentDevicePreference preference);
    ~DisplayManager();

    // 禁止拷贝和移动
    DisplayManager(const DisplayManager &) = delete;
    DisplayManager &operator=(const DisplayManager &) = delete;
    DisplayManager(DisplayManager &&) = delete;
    DisplayManager &operator=(DisplayManager &&) = delete;

    bool initDisplayManager(void *surface);
    bool waitExecutor(const HardwareExecutorVulkan &executor);
    bool displayFrame(void *surface, const HardwareImage& displayImage, HardwareExecutorVulkan* producer = nullptr);

  private:
    friend struct HardwareDisplayer;
    struct PixelUpload
    {
        uint32_t width{}, height{};
        HardwareImage::Format format{};
        HardwareBuffer staging;
        HardwareImage image;
        HardwareExecutor executor;
    };
    std::optional<PixelUpload> pixelUpload;

    // Vulkan 核心资源
    VkSurfaceKHR vkSurface{VK_NULL_HANDLE};
    VkSwapchainKHR swapChain{VK_NULL_HANDLE};
    VkSurfaceFormatKHR surfaceFormat{};

    // 显示参数
    ktm::uvec2 displaySize{0, 0};
    void *displaySurface{nullptr};
    PresentDevicePreference devicePreference;
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
    SubmissionPoint waitedSubmission;
    std::unique_ptr<HardwareExecutorVulkan> mainDeviceExecutor;
    std::unique_ptr<HardwareExecutorVulkan> displayDeviceExecutor;

    // 内部方法
    void cleanUpDisplayManager();
    void createVkSurface(void *surface);
    void choosePresentDevice();
    void createSyncObjects();
    void createSwapChain();
    void recreateSwapChain();

    void cleanupSyncObjects();
    void cleanupStagingBuffers();
    void cleanupDisplayImage();

    void setupCrossDeviceTransfer(const ResourceManager::ImageHardwareWrap &sourceImage);
};
