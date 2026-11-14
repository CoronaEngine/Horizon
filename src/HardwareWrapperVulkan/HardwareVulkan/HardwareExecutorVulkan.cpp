#include "HardwareExecutorVulkan.h"

#include "horizon/core/logging.h"
#include "ResourceAccess.h"
#include <algorithm>

namespace {
void clearSubmitState(HardwareExecutorVulkan& executor)
{
    executor.waitSemaphores.clear();
    executor.signalSemaphores.clear();
    executor.waitFence = VK_NULL_HANDLE;
}

VkSemaphoreSubmitInfo timelineSubmitInfo(VkSemaphore semaphore, uint64_t value)
{
    VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    info.semaphore = semaphore;
    info.value = value;
    info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    return info;
}

VkResult waitForDeferredSubmissions(VkDevice device, std::span<const DeferredRelease> submissions, uint64_t timeout)
{
    std::unordered_map<VkSemaphore, uint64_t> maximumValues;
    for (const auto& submission : submissions)
    {
        auto& value = maximumValues[submission.semaphore];
        value = std::max(value, submission.timelineValue);
    }
    if (maximumValues.empty()) return VK_SUCCESS;

    std::vector<VkSemaphore> semaphores;
    std::vector<uint64_t> values;
    semaphores.reserve(maximumValues.size());
    values.reserve(maximumValues.size());
    for (const auto& [semaphore, value] : maximumValues)
    {
        semaphores.push_back(semaphore);
        values.push_back(value);
    }
    VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    info.semaphoreCount = static_cast<uint32_t>(semaphores.size());
    info.pSemaphores = semaphores.data();
    info.pValues = values.data();
    return vkWaitSemaphores(device, &info, timeout);
}
}

std::vector<VkSemaphoreSubmitInfo> mergeSemaphoreSubmitInfos(const std::vector<VkSemaphoreSubmitInfo> &infos)
{
    std::vector<VkSemaphoreSubmitInfo> mergedInfos;
    mergedInfos.reserve(infos.size());

    std::unordered_map<VkSemaphore, size_t> semaphoreToIndex;
    semaphoreToIndex.reserve(infos.size());

    for (const auto &info : infos)
    {
        if (info.semaphore == VK_NULL_HANDLE)
        {
            continue;
        }

        auto it = semaphoreToIndex.find(info.semaphore);
        if (it == semaphoreToIndex.end())
        {
            VkSemaphoreSubmitInfo normalizedInfo = info;
            normalizedInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            normalizedInfo.pNext = nullptr;
            mergedInfos.push_back(normalizedInfo);
            semaphoreToIndex.emplace(info.semaphore, mergedInfos.size() - 1);
            continue;
        }

        auto &dst = mergedInfos[it->second];
        dst.value = std::max(dst.value, info.value);
        dst.stageMask |= info.stageMask;
    }

    return mergedInfos;
}

// ========== 析构函数：等待所有延迟释放的资源完成 ==========
HardwareExecutorVulkan::~HardwareExecutorVulkan()
{
    if (!deferredReleaseQueue.empty() && hardwareContext)
    {
        const auto result = waitForDeferredSubmissions(hardwareContext->deviceManager.logicalDevice,
                                                       deferredReleaseQueue, UINT64_MAX);
        if (result != VK_SUCCESS)
        {
            CFW_LOG_ERROR("HardwareExecutorVulkan destructor: vkWaitSemaphores failed with {}",
                          static_cast<int>(result));
        }
        else
        {
            CFW_LOG_TRACE("HardwareExecutorVulkan destructor: successfully waited for deferred submissions");
        }
    }

    deferredReleaseQueue.clear();
}

// ========== 清理已完成的资源（非阻塞） ==========
void HardwareExecutorVulkan::cleanupCompletedResources()
{
    if (deferredReleaseQueue.empty())
    {
        return;
    }

    // ========== Step 1: 收集所有不同的 semaphore ==========
    std::unordered_map<VkSemaphore, uint64_t> semaphoreCompletedValues;

    for (const auto &entry : deferredReleaseQueue)
    {
        // 只记录 semaphore，稍后批量查询
        semaphoreCompletedValues[entry.semaphore] = 0;
    }

    // ========== Step 2: 批量查询每个 semaphore 的当前值 ==========
    // 用于记录损坏的 semaphore
    std::unordered_set<VkSemaphore> corruptedSemaphores;

    for (auto &[semaphore, completedValue] : semaphoreCompletedValues)
    {
        VkResult result = vkGetSemaphoreCounterValue(
            hardwareContext->deviceManager.logicalDevice,
            semaphore,
            &completedValue);

        if (result != VK_SUCCESS)
        {
            CFW_LOG_ERROR("Failed to query timeline semaphore value, VkResult: {}",
                          static_cast<int>(result));
            // 查询失败时设为 0，不释放该 semaphore 对应的资源
            completedValue = 0;
        }
        else if (completedValue == UINT64_MAX)
        {
            // ===== 修复4: 检测损坏的 semaphore =====
            CFW_LOG_ERROR("[cleanupCompletedResources] Semaphore {} has invalid value UINT64_MAX, marking as corrupted",
                          reinterpret_cast<uintptr_t>(semaphore));
            corruptedSemaphores.insert(semaphore);
            // 设为一个非常大的值以触发资源释放，防止内存泄漏
            completedValue = UINT64_MAX - 1;
        }
    }

    // ========== Step 3: 分区并移除已完成的资源 ==========
    // 使用 stable_partition 保持未完成资源的相对顺序（FIFO 释放）
    auto partitionPoint = std::stable_partition(
        deferredReleaseQueue.begin(),
        deferredReleaseQueue.end(),
        [&semaphoreCompletedValues, &corruptedSemaphores](const DeferredRelease &entry) {
            // 如果 semaphore 已损坏，强制释放其资源
            if (corruptedSemaphores.count(entry.semaphore) > 0)
            {
                return false; // 移除（释放）
            }

            // 返回 true 表示保留（未完成），返回 false 表示移除（已完成）
            auto it = semaphoreCompletedValues.find(entry.semaphore);
            if (it != semaphoreCompletedValues.end())
            {
                // timelineValue <= completedValue 表示 GPU 已完成
                return entry.timelineValue > it->second;
            }
            // 未找到 semaphore 信息，保守处理：保留
            return true;
        });

    // ========== Step 4: 记录释放信息并清理 ==========
    size_t releasedCount = std::distance(partitionPoint, deferredReleaseQueue.end());

    if (releasedCount > 0)
    {
        CFW_LOG_TRACE("Released {} deferred resources, remaining: {}",
                      releasedCount,
                      std::distance(deferredReleaseQueue.begin(), partitionPoint));
    }

    // 实际释放资源（shared_ptr 析构）
    deferredReleaseQueue.erase(partitionPoint, deferredReleaseQueue.end());
}

// ========== 同步等待所有延迟释放的资源完成 ==========
void HardwareExecutorVulkan::waitForAllDeferredResources()
{
    if (deferredReleaseQueue.empty())
    {
        return;
    }

    const auto result = waitForDeferredSubmissions(hardwareContext->deviceManager.logicalDevice,
                                                   deferredReleaseQueue, UINT64_MAX);

    if (result != VK_SUCCESS)
    {
        coronaHardwareCheck(result);
    }

    // 等待完成后，清理所有资源
    size_t count = deferredReleaseQueue.size();
    deferredReleaseQueue.clear();

    CFW_LOG_TRACE("waitForAllDeferredResources: released {} resources", count);
}

// ========== 获取延迟释放统计信息 ==========
HardwareExecutorVulkan::DeferredReleaseStats HardwareExecutorVulkan::getDeferredReleaseStats() const
{
    DeferredReleaseStats stats;
    stats.currentPending = deferredReleaseQueue.size();

    if (deferredReleaseQueue.empty())
    {
        return stats;
    }

    // 统计不同的 semaphore 数量
    std::unordered_set<VkSemaphore> uniqueSemaphores;
    uint64_t minTimeline = UINT64_MAX;
    uint64_t maxTimeline = 0;

    for (const auto &entry : deferredReleaseQueue)
    {
        uniqueSemaphores.insert(entry.semaphore);
        minTimeline = std::min(minTimeline, entry.timelineValue);
        maxTimeline = std::max(maxTimeline, entry.timelineValue);
    }

    stats.totalSemaphores = uniqueSemaphores.size();
    stats.oldestTimeline = minTimeline;
    stats.newestTimeline = maxTimeline;

    return stats;
}

DeviceManager::QueueUtils *HardwareExecutorVulkan::pickQueueAndCommit(std::atomic_uint16_t &currentQueueIndex,
                                                                      std::vector<DeviceManager::QueueUtils> &currentQueues,
                                                                      std::function<bool(DeviceManager::QueueUtils *currentRecordQueue)> commitCommand)
{
    if (currentQueues.empty()) throw std::runtime_error("No queue is available for submission");
    DeviceManager::QueueUtils* queue;
    std::unique_lock<std::mutex> queueLock;
    for (;;)
    {
        queue = &currentQueues[currentQueueIndex.fetch_add(1) % currentQueues.size()];
        queueLock = std::unique_lock<std::mutex>(*queue->queueMutex, std::try_to_lock);
        if (queueLock.owns_lock())
        {
            uint64_t completed = 0;
            coronaHardwareCheck(vkGetSemaphoreCounterValue(queue->deviceManager->logicalDevice,
                                                            queue->timelineSemaphore, &completed));
            if (completed == UINT64_MAX)
                throw std::runtime_error("Queue timeline is exhausted or the device is lost");
            if (completed >= queue->timelineValue->load(std::memory_order_acquire)) break;
            queueLock.unlock();
        }
        std::this_thread::yield();
    }
    // ===== 首先清理已完成的资源 =====
    cleanupCompletedResources();

    // Keep the existing exception/retry behavior; ordinary submit exits share cleanup.
    const bool submitted = [&] {
        if (!commitCommand(queue)) return false;

        // Direct callers (including present) also use the selected queue's timeline.
        this->currentRecordQueue = queue;
        VkCommandBufferSubmitInfo commandBufferSubmitInfo{};
        commandBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferSubmitInfo.commandBuffer = currentRecordQueue->commandBuffer;

        // 用单次 fetch_add 同时获取 wait 和 signal 值，消除两次原子操作间的竞态窗口
        uint64_t waitValue = currentRecordQueue->timelineValue->fetch_add(1);
        uint64_t signalValue = waitValue + 1;
        struct RollbackTimeline {
            std::atomic_uint64_t& value;
            bool submitted{false};
            ~RollbackTimeline() { if (!submitted) value.fetch_sub(1); }
        } rollbackTimeline{*currentRecordQueue->timelineValue};

        auto mergedWaitSemaphores = mergeSemaphoreSubmitInfos(waitSemaphores);
        auto mergedSignalSemaphores = mergeSemaphoreSubmitInfos(signalSemaphores);
        auto appendQueuePoint = [&](auto& infos, uint64_t value) {
            auto existing = std::find_if(infos.begin(), infos.end(), [&](const auto& info) {
                return info.semaphore == queue->timelineSemaphore;
            });
            if (existing == infos.end())
                infos.push_back(timelineSubmitInfo(queue->timelineSemaphore, value));
            else
            {
                if (existing->value > value) throw std::invalid_argument("Conflicting queue timeline value");
                existing->value = value;
                existing->stageMask |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            }
        };
        appendQueuePoint(mergedWaitSemaphores, waitValue);
        appendQueuePoint(mergedSignalSemaphores, signalValue);

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(mergedWaitSemaphores.size());
        submitInfo.pWaitSemaphoreInfos = mergedWaitSemaphores.data();
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(mergedSignalSemaphores.size());
        submitInfo.pSignalSemaphoreInfos = mergedSignalSemaphores.data();
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferSubmitInfo;

        coronaHardwareCheck(vkQueueSubmit2(queue->vkQueue, 1, &submitInfo, waitFence));

        rollbackTimeline.submitted = true;
        // Publish this successful submission independently of later recording.
        lastSubmission = {currentRecordQueue, signalValue};
        return true;
    }();

    // 提交完成后清理已消费的 semaphore 状态，避免泄漏到后续的 pickQueueAndCommit 调用
    clearSubmitState(*this);
    return submitted ? queue : nullptr;
}

HardwareExecutorVulkan &HardwareExecutorVulkan::commit()
{
    if (commandList.empty()) return *this;
    // Remove only the waits derived for this attempt if recording or submission throws.
    struct AttemptWaits {
        std::vector<VkSemaphoreSubmitInfo>& waits;
        size_t originalSize;
        ~AttemptWaits() { if (waits.size() > originalSize) waits.resize(originalSize); }
    };
    // Resolve recorded dependencies before locking a queue. A failed CPU bridge
    // must leave the commands and submission points available for retry.
    {
        AttemptWaits attempt{waitSemaphores, waitSemaphores.size()};
        for (const auto point : pendingWaits)
        {
            const auto semaphore = hardwareContext->deviceManager.getOrImportTimelineSemaphore(*point.queue);
            if (semaphore)
            {
                waitSemaphores.push_back(timelineSubmitInfo(semaphore, point.value));
            }
            else
            {
                // An incompatible device pair uses a host wait before local submit.
                VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
                wait.semaphoreCount = 1;
                wait.pSemaphores = &point.queue->timelineSemaphore;
                wait.pValues = &point.value;
                coronaHardwareCheck(vkWaitSemaphores(point.queue->deviceManager->getLogicalDevice(), &wait, 5'000'000'000ULL));
            }
        }

        // Allocate before submission. On failure the queued commands stay in place;
        // on success their vector moves into the completion queue without allocation.
        deferredReleaseQueue.reserve(deferredReleaseQueue.size() + 1);
        ResourceLayoutJournal layouts;
        auto& commands = commandList;

        auto commitToQueue = [&](DeviceManager::QueueUtils *currentRecordQueue) -> bool {
            this->currentRecordQueue = currentRecordQueue;

            coronaHardwareCheck(vkResetCommandBuffer(currentRecordQueue->commandBuffer, 0));

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            coronaHardwareCheck(vkBeginCommandBuffer(currentRecordQueue->commandBuffer, &beginInfo));

            ResourceBarrierPlanner planner;
            ResourceAccessList accesses;
            ResourceBarriers requiredBarriers;
            for (size_t i = 0; i < commands.size(); i++)
            {
                auto& recorded = commands[i];
                accesses.clear();
                recorded.command->resolveResourceUses(recorded.resourceUses, accesses);
                layouts.capture(accesses);
                planner.plan(accesses, requiredBarriers);

                if (!requiredBarriers.memoryBarriers.empty() || !requiredBarriers.bufferBarriers.empty() || !requiredBarriers.imageBarriers.empty())
                {
                    VkDependencyInfo dependencyInfo{};
                    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dependencyInfo.memoryBarrierCount = static_cast<uint32_t>(requiredBarriers.memoryBarriers.size());
                    dependencyInfo.pMemoryBarriers = requiredBarriers.memoryBarriers.data();
                    dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(requiredBarriers.bufferBarriers.size());
                    dependencyInfo.pBufferMemoryBarriers = requiredBarriers.bufferBarriers.data();
                    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(requiredBarriers.imageBarriers.size());
                    dependencyInfo.pImageMemoryBarriers = requiredBarriers.imageBarriers.data();
                    dependencyInfo.pNext = nullptr;

                    vkCmdPipelineBarrier2(currentRecordQueue->commandBuffer, &dependencyInfo);
                }

                recorded.command->commitCommand(*this);
            }

            coronaHardwareCheck(vkEndCommandBuffer(currentRecordQueue->commandBuffer));

            return true;
        };

        CommandRecordVulkan::ExecutorType queueType = CommandRecordVulkan::ExecutorType::Transfer;
        for (size_t i = 0; i < commands.size(); i++)
        {
            if (commands[i].type == CommandRecordVulkan::ExecutorType::Graphics)
            {
                queueType = CommandRecordVulkan::ExecutorType::Graphics;
                break;
            }
            else if (commands[i].type == CommandRecordVulkan::ExecutorType::Compute)
            {
                queueType = CommandRecordVulkan::ExecutorType::Compute;
            }
        }

        std::unique_lock<std::mutex> graphicsSubmitChainLock;
        auto& device = hardwareContext->deviceManager;
        if (queueType == CommandRecordVulkan::ExecutorType::Graphics)
        {
            graphicsSubmitChainLock = std::unique_lock<std::mutex>(device.graphicsSubmitMutex);
            if (device.lastGraphicsSemaphore)
                waitSemaphores.push_back(timelineSubmitInfo(device.lastGraphicsSemaphore, device.lastGraphicsValue));
        }
        // Keep room for the completion point before submitting any GPU work.
        waitSemaphores.reserve(std::max<size_t>(1, waitSemaphores.size()));

        DeviceManager::QueueUtils *submittedQueue = nullptr;
        switch (queueType)
        {
        case CommandRecordVulkan::ExecutorType::Graphics:
            submittedQueue = pickQueueAndCommit(hardwareContext->deviceManager.currentGraphicsQueueIndex,
                                                hardwareContext->deviceManager.graphicsQueues,
                                                commitToQueue);
            break;
        case CommandRecordVulkan::ExecutorType::Compute:
            if (!hardwareContext->deviceManager.computeQueues.empty())
            {
                submittedQueue = pickQueueAndCommit(hardwareContext->deviceManager.currentComputeQueueIndex,
                                                    hardwareContext->deviceManager.computeQueues,
                                                    commitToQueue);
            }
            else
            {
                submittedQueue = pickQueueAndCommit(hardwareContext->deviceManager.currentGraphicsQueueIndex,
                                                    hardwareContext->deviceManager.graphicsQueues,
                                                    commitToQueue);
            }
            break;
        case CommandRecordVulkan::ExecutorType::Transfer:
            if (!hardwareContext->deviceManager.transferQueues.empty())
            {
                submittedQueue = pickQueueAndCommit(hardwareContext->deviceManager.currentTransferQueueIndex,
                                                    hardwareContext->deviceManager.transferQueues,
                                                    commitToQueue);
            }
            else
            {
                submittedQueue = pickQueueAndCommit(hardwareContext->deviceManager.currentGraphicsQueueIndex,
                                                    hardwareContext->deviceManager.graphicsQueues,
                                                    commitToQueue);
            }
            break;
        case CommandRecordVulkan::ExecutorType::Invalid:
            CFW_LOG_ERROR("No valid command to commit in HardwareExecutorVulkan!");
            break;
        default:
            CFW_LOG_ERROR("Unknown executor type in HardwareExecutorVulkan!");
            break;
        }

        if (!submittedQueue) throw std::runtime_error("The recorded commands were not submitted");
        deferredReleaseQueue.push_back({lastSubmission.value, std::move(commandList), lastSubmission.queue->timelineSemaphore});
        layouts.committed = true;
        pendingWaits.clear();
        if (queueType == CommandRecordVulkan::ExecutorType::Graphics)
        {
            device.lastGraphicsSemaphore = lastSubmission.queue->timelineSemaphore;
            device.lastGraphicsValue = lastSubmission.value;
        }

    }

    clearSubmitState(*this);
    // Use the published value, not a queue counter that another submit may have advanced.
    if (lastSubmission)
        waitSemaphores.push_back(timelineSubmitInfo(lastSubmission.queue->timelineSemaphore, lastSubmission.value));

    return *this;
}
