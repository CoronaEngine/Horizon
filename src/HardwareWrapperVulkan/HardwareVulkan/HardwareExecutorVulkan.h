#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <span>

#include "HardwareWrapperVulkan/HardwareContext.h"
#include "Horizon_old.h"

struct HardwareExecutorVulkan;
struct CommandRecordVulkan;
struct ResourceAccessList;

struct CopyCommandImpl
{
    virtual ~CopyCommandImpl() = default;
    virtual CommandRecordVulkan* getCommandRecord() = 0;
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

    virtual ~CommandRecordVulkan() = default;
    virtual void commitCommand(HardwareExecutorVulkan &executor) = 0;
    virtual void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) = 0;
    virtual ExecutorType getExecutorType() = 0;
};

// One enqueue's dependency metadata. Owned pipeline commands freeze execution
// state; legacy borrowed commands retain their existing lifetime contract.
struct RecordedCommand
{
    CommandRecordVulkan* command;
    std::vector<ResourceUse> resourceUses;
    std::shared_ptr<void> owner;
    CommandRecordVulkan::ExecutorType type;
};

// One submitted batch retains its commands and resources until GPU completion.
struct DeferredRelease
{
    uint64_t timelineValue;
    std::vector<RecordedCommand> commands;
    VkSemaphore semaphore;
};

// A borrowed completion point. Queue storage and its device must outlive use;
// public executors use DeviceManager queues, which remain stable after initialization.
struct SubmissionPoint
{
    const DeviceManager::QueueUtils* queue{nullptr};
    uint64_t value{0};

    explicit operator bool() const noexcept { return queue != nullptr && value != 0; }
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
        deferredReleaseQueue.reserve(128);
    }

    // 兼容旧代码的构造函数，但建议逐步废弃，建议在调用处显式传递
    explicit HardwareExecutorVulkan()
        : hardwareContext(getHardwareContext().getMainDevice())
    {
        // 预分配以减少重分配
        deferredReleaseQueue.reserve(128);
    }

    ~HardwareExecutorVulkan();

    HardwareExecutorVulkan(const HardwareExecutorVulkan&) = delete;
    HardwareExecutorVulkan& operator=(const HardwareExecutorVulkan&) = delete;

    [[nodiscard]] SubmissionPoint getSubmissionPoint() const noexcept
    {
        return lastSubmission;
    }

    // Borrowed commands must outlive recording; their GPU resources must outlive execution.
    // Supply an owner (or use recordOwned) when the queue should retain the command.
    HardwareExecutorVulkan &record(CommandRecordVulkan *commandRecord,
                                  const std::vector<ResourceUse>& resources = {},
                                  std::shared_ptr<void> owner = {})
    {
        const auto type = commandRecord ? commandRecord->getExecutorType() : CommandRecordVulkan::ExecutorType::Invalid;
        if (type != CommandRecordVulkan::ExecutorType::Invalid)
            commandList.push_back({commandRecord, resources, std::move(owner), type});
        return *this;
    }

    HardwareExecutorVulkan &operator<<(CommandRecordVulkan *commandRecord)
    {
        return record(commandRecord);
    }

    HardwareExecutorVulkan& recordOwned(std::shared_ptr<CommandRecordVulkan> command,
                                       const std::vector<ResourceUse>& resources = {})
    {
        auto* record = command.get();
        return this->record(record, resources, std::move(command));
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

    HardwareExecutorVulkan &wait(const HardwareExecutorVulkan &other)
    {
        return wait(other.getSubmissionPoint());
    }

    HardwareExecutorVulkan &wait(SubmissionPoint point)
    {
        if (point)
        {
            for (auto& pending : pendingWaits)
                if (pending.queue == point.queue)
                {
                    if (point.value > pending.value) pending.value = point.value;
                    return *this;
                }
            pendingWaits.push_back(point);
        }
        return *this;
    }

    explicit operator bool() const
    {
        return static_cast<bool>(lastSubmission);
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
    std::shared_ptr<HardwareContext::HardwareUtils> hardwareContext;
    std::vector<RecordedCommand> commandList;
    std::vector<VkSemaphoreSubmitInfo> waitSemaphores;
    std::vector<VkSemaphoreSubmitInfo> signalSemaphores;
    // std::vector<VkFence> prentFences;
    VkFence waitFence{VK_NULL_HANDLE};
    // std::unordered_map<VkFence, DeviceManager::QueueUtils*> fenceToPresent;
    // std::vector<std::vector<std::shared_ptr<Buffer>>> buffer_to_dispose_;

    // ========== 延迟释放成员 ==========
    std::vector<DeferredRelease> deferredReleaseQueue;

  private:
    std::vector<SubmissionPoint> pendingWaits; // Freeze the exact submissions at wait() time.
    SubmissionPoint lastSubmission; // Published only after a successful queue submission.
};
