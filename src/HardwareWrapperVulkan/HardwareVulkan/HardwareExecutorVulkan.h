#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "HardwareWrapperVulkan/HardwareContext.h"
#include "Horizon.h"

struct HardwareExecutorVulkan;
struct CommandRecordVulkan;

struct CopyCommandImpl
{
    virtual ~CopyCommandImpl() = default;
    virtual CommandRecordVulkan* getCommandRecord() = 0;
};

// Generic resource holder for keeping objects alive until GPU work is done
struct ResourceHolderCommand : public CopyCommandImpl
{
    std::vector<HardwareBuffer> buffers;
    std::vector<HardwareImage> images;
    // Add other resources if needed (e.g., PushConstants)

    CommandRecordVulkan* getCommandRecord() override { return nullptr; }
};

// ========== 延迟释放条目 ==========
struct DeferredRelease
{
    uint64_t timelineValue;                    // GPU 完成此值后可释放
    std::shared_ptr<CopyCommandImpl> resource; // 持有资源的引用
    VkSemaphore semaphore;                     // 对应的 timeline semaphore
};

struct CommandRecordVulkan
{
    enum class ExecutorType
    {
        Graphics,
        Compute,
        Transfer,
        Invalid
    };

    struct RequiredBarriers
    {
        std::vector<VkMemoryBarrier2> memoryBarriers;
        std::vector<VkBufferMemoryBarrier2> bufferBarriers;
        std::vector<VkImageMemoryBarrier2> imageBarriers;
    };

    CommandRecordVulkan() = default;
    virtual ~CommandRecordVulkan() = default;

    virtual void commitCommand(HardwareExecutorVulkan &executor)
    {
    }

    virtual RequiredBarriers getRequiredBarriers(HardwareExecutorVulkan &executor)
    {
        return RequiredBarriers{};
    }

    virtual ExecutorType getExecutorType()
    {
        return ExecutorType::Invalid;
    }

  protected:
    ExecutorType executorType{ExecutorType::Invalid};
};

struct HardwareExecutorVulkan
{
    explicit HardwareExecutorVulkan(std::shared_ptr<HardwareContext::HardwareUtils> context)
        : hardwareContext(std::move(context))
    {
        if (!hardwareContext)
        {
            throw std::invalid_argument("Hardware context cannot be null");
        }
        // 预分配以减少重分配
        pendingResources.reserve(32);
        deferredReleaseQueue.reserve(128);
    }

    // 兼容旧代码的构造函数，但建议逐步废弃，建议在调用处显式传递
    explicit HardwareExecutorVulkan()
        : hardwareContext(globalHardwareContext.getMainDevice())
    {
        // 预分配以减少重分配
        pendingResources.reserve(32);
        deferredReleaseQueue.reserve(128);
    }

    ~HardwareExecutorVulkan();

    HardwareExecutorVulkan &operator<<(CommandRecordVulkan *commandRecord)
    {
        if (commandRecord && commandRecord->getExecutorType() != CommandRecordVulkan::ExecutorType::Invalid)
        {
            commandList.push_back(commandRecord);
        }
        return *this;
    }

    HardwareExecutorVulkan &operator<<(HardwareExecutorVulkan &other)
    {
        return other;
    }

    HardwareExecutorVulkan &wait(const std::vector<VkSemaphoreSubmitInfo> &waitInfos = {},
                                 const std::vector<VkSemaphoreSubmitInfo> &signalInfos = {})
    {
        // waitFence = fence;
        waitSemaphores.insert(waitSemaphores.end(), waitInfos.begin(), waitInfos.end());
        signalSemaphores.insert(signalSemaphores.end(), signalInfos.begin(), signalInfos.end());
        return *this;
    }

    HardwareExecutorVulkan &wait(HardwareExecutorVulkan &other)
    {
        if (other)
        {
            // 跨设备时自动解析为本设备可用的 imported semaphore；同设备零开销直接返回
            VkSemaphore resolvedSemaphore = hardwareContext->deviceManager.getOrImportTimelineSemaphore(
                *other.currentRecordQueue);

            if (resolvedSemaphore == VK_NULL_HANDLE)
            {
                // CPU-bridge fallback：跨设备 semaphore 导入不可用（不同架构 GPU），
                // 回退到 host 侧等待外部设备完成，再让本地 GPU 继续
                uint64_t waitValue = other.lastSignalValue;
                VkSemaphore foreignSem = other.currentRecordQueue->timelineSemaphore;
                VkDevice foreignDevice = other.currentRecordQueue->deviceManager->getLogicalDevice();

                VkSemaphoreWaitInfo cpuWaitInfo{};
                cpuWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
                cpuWaitInfo.pNext = nullptr;
                cpuWaitInfo.flags = 0;
                cpuWaitInfo.semaphoreCount = 1;
                cpuWaitInfo.pSemaphores = &foreignSem;
                cpuWaitInfo.pValues = &waitValue;

                VkResult r = vkWaitSemaphores(foreignDevice, &cpuWaitInfo, 5'000'000'000ULL);
                if (r != VK_SUCCESS)
                {
                    CFW_LOG_ERROR("[wait] CPU-bridge fallback: vkWaitSemaphores on foreign device failed, VkResult={}",
                                  static_cast<int>(r));
                }
                // 外部 GPU 工作已在 CPU 时间线上确认完成，无需添加 GPU 侧 wait semaphore
                return *this;
            }

            VkSemaphoreSubmitInfo timelineWaitSemaphoreSubmitInfo{};
            timelineWaitSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            timelineWaitSemaphoreSubmitInfo.semaphore = resolvedSemaphore;
            timelineWaitSemaphoreSubmitInfo.value = other.lastSignalValue;
            timelineWaitSemaphoreSubmitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            waitSemaphores.push_back(timelineWaitSemaphoreSubmitInfo);
        }
        return *this;
    }

    explicit operator bool() const
    {
        return currentRecordQueue != nullptr;
    }

    HardwareExecutorVulkan &commit();
    //HardwareExecutorVulkan &commitTest();

    // ========== 延迟释放相关接口 ==========
    void cleanupCompletedResources();
    void waitForAllDeferredResources();

    // 调试：获取统计信息
    struct DeferredReleaseStats
    {
        size_t currentPending = 0;   // 当前等待中的资源数
        size_t totalSemaphores = 0;  // 涉及的 semaphore 数量
        uint64_t oldestTimeline = 0; // 最老的等待 timeline
        uint64_t newestTimeline = 0; // 最新的等待 timeline
    };
    DeferredReleaseStats getDeferredReleaseStats() const;

    // void waitUntilCommitIsComplete();
    // void waitUntilAllCommitAreComplete();
    // void disposeWhenCommitCompletes(std::shared_ptr<Buffer> buffer);
    // void disposeWhenCommitCompletes(std::function<void()> &&deallocator);

    DeviceManager::QueueUtils *pickQueueAndCommit(std::atomic_uint16_t &queueIndex,
                                                  std::vector<DeviceManager::QueueUtils> &queues,
                                                  std::function<bool(DeviceManager::QueueUtils *currentRecordQueue)> commitCommand);

    DeviceManager::QueueUtils *currentRecordQueue{nullptr};
    uint64_t lastSignalValue{0}; // 记录最近一次提交的 signal timeline 值，避免跨原子操作竞态
    std::shared_ptr<HardwareContext::HardwareUtils> hardwareContext;
    std::vector<CommandRecordVulkan *> commandList;
    std::vector<VkSemaphoreSubmitInfo> waitSemaphores;
    std::vector<VkSemaphoreSubmitInfo> signalSemaphores;
    // std::vector<VkFence> prentFences;
    VkFence waitFence{VK_NULL_HANDLE};
    // std::unordered_map<VkFence, DeviceManager::QueueUtils*> fenceToPresent;
    // std::vector<std::vector<std::shared_ptr<Buffer>>> buffer_to_dispose_;

    // ========== 延迟释放成员 ==========
    std::vector<std::shared_ptr<CopyCommandImpl>> pendingResources;
    std::vector<DeferredRelease> deferredReleaseQueue;
};
