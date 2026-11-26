#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <span>

#include "HardwareWrapperVulkan/HardwareContext.h"
#include "Horizon.h"

struct HardwareExecutorVulkan;
struct ResourceAccessList;

namespace horizon::detail {
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
} // namespace horizon::detail

using horizon::detail::CommandRecordVulkan;

// The owner retains captured GPU resources through recording and submission.
struct RecordedCommand
{
    CommandRecordVulkan* command{};
    std::vector<ResourceUse> resourceUses;
    std::shared_ptr<void> owner;
    CommandRecordVulkan::ExecutorType type{CommandRecordVulkan::ExecutorType::Invalid};

    RecordedCommand() = default;
    explicit RecordedCommand(CommandRecordVulkan& record, std::shared_ptr<void> owner = {},
                             std::vector<ResourceUse> resources = {})
        : command(&record), resourceUses(std::move(resources)), owner(std::move(owner)), type(record.getExecutorType()) {}
};

struct HardwareExecutor::Command::Impl
{
    struct Draw;
    struct Dispatch;
    struct Display;
    struct BufferToBuffer;
    struct BufferToImage;
    struct ImageToImage;
    struct ImageToBuffer;

    using Entry = std::variant<RecordedCommand, std::shared_ptr<const Display>>;

    virtual ~Impl() = default;
    virtual Entry queueEntry(std::shared_ptr<Impl> owner) = 0;
};

struct HardwareExecutor::Command::Impl::Display : HardwareExecutor::Command::Impl
{
    Display(std::shared_ptr<HardwareDisplayer::Impl> target, const HardwareImage& image)
        : target(std::move(target)), image(image) {}

    Entry queueEntry(std::shared_ptr<Impl> owner) final
    {
        return std::shared_ptr<const Display>(std::move(owner), this);
    }
    virtual void submit(HardwareExecutorVulkan& executor) const;

    std::shared_ptr<HardwareDisplayer::Impl> target;
    HardwareImage image;
};

// A borrowed completion point. Queue storage and its device must outlive use;
// public executors use DeviceManager queues, which remain stable after initialization.
struct SubmissionPoint
{
    const DeviceManager::QueueUtils* queue{nullptr};
    uint64_t value{0};

    explicit operator bool() const noexcept { return queue != nullptr && value != 0; }
};

// One submitted batch retains its commands and resources until GPU completion.
struct DeferredRelease
{
    SubmissionPoint point;
    std::vector<RecordedCommand> commands;
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
    // Supply an owner when the queue should retain the command.
    HardwareExecutorVulkan &record(CommandRecordVulkan *commandRecord,
                                  const std::vector<ResourceUse>& resources = {},
                                  std::shared_ptr<void> owner = {})
    {
        if (commandRecord) {
            RecordedCommand entry(*commandRecord, std::move(owner), resources);
            if (entry.type != CommandRecordVulkan::ExecutorType::Invalid)
                commandList.push_back(std::move(entry));
        }
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

    HardwareExecutorVulkan &commit(bool submitPendingWaits = false);

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

    DeviceManager::QueueUtils *currentRecordQueue{nullptr};
    // Number of vkCmdBeginRenderPass calls recorded by the most recent nonempty commit.
    uint32_t recordedRasterPassCount{};
    std::shared_ptr<HardwareContext::HardwareUtils> hardwareContext;
    std::vector<RecordedCommand> commandList; // GPU batch currently being submitted.
    std::vector<VkSemaphoreSubmitInfo> waitSemaphores;
    std::vector<VkSemaphoreSubmitInfo> signalSemaphores;

    // ========== 延迟释放成员 ==========
    std::vector<DeferredRelease> deferredReleaseQueue;

  private:
    void cleanupCompletedResources();
    std::vector<SubmissionPoint> pendingWaits; // Freeze the exact submissions at wait() time.
    SubmissionPoint lastSubmission; // Published only after a successful queue submission.
    SubmissionPoint lastCompleted;  // Observed from the device or a successful host wait.
};

struct HardwareExecutor::Impl
{
    HardwareExecutorVulkan backend;

    std::deque<Command::Impl::Entry> pending;
};

struct HardwareBackendAccess
{
    static HardwareExecutor::Impl& state(HardwareExecutor& executor)
    {
        if (!executor.impl_)
            throw std::runtime_error("Empty HardwareExecutor handle");
        return *executor.impl_;
    }
};
