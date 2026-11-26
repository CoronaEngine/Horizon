#include "HardwareExecutorVulkan.h"

#include "horizon/core/logging.h"
#include "ResourceAccess.h"
#include <algorithm>

namespace {
VkSemaphoreSubmitInfo timelineSubmitInfo(VkSemaphore semaphore, uint64_t value)
{
    VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    info.semaphore = semaphore;
    info.value = value;
    info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    return info;
}

void addSemaphore(std::vector<VkSemaphoreSubmitInfo>& infos, VkSemaphoreSubmitInfo info)
{
    if (!info.semaphore) return;
    for (auto& existing : infos)
        if (existing.semaphore == info.semaphore)
        {
            existing.value = std::max(existing.value, info.value);
            existing.stageMask |= info.stageMask;
            return;
        }
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.pNext = nullptr;
    infos.push_back(info);
}

VkResult waitTimeline(VkDevice device, VkSemaphore semaphore, uint64_t value)
{
    VkSemaphoreWaitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    info.semaphoreCount = 1;
    info.pSemaphores = &semaphore;
    info.pValues = &value;
    return vkWaitSemaphores(device, &info, UINT64_MAX);
}
}

HardwareExecutorVulkan::~HardwareExecutorVulkan()
{
    if (!deferredReleaseQueue.empty())
    {
        const auto result = waitTimeline(hardwareContext->deviceManager.getLogicalDevice(),
                                         lastSubmission.queue->timelineSemaphore, lastSubmission.value);
        if (result != VK_SUCCESS)
            CFW_LOG_ERROR("Executor teardown wait failed with {}", static_cast<int>(result));
    }
}

void HardwareExecutorVulkan::cleanupCompletedResources()
{
    const auto pending = std::find_if(deferredReleaseQueue.begin(), deferredReleaseQueue.end(), [&](const auto& entry) {
        if (lastCompleted.queue != entry.point.queue || lastCompleted.value < entry.point.value)
        {
            uint64_t completed{};
            const auto result = vkGetSemaphoreCounterValue(hardwareContext->deviceManager.getLogicalDevice(),
                                                           entry.point.queue->timelineSemaphore, &completed);
            if (result != VK_SUCCESS)
            {
                CFW_LOG_ERROR("Failed to query executor timeline: {}", static_cast<int>(result));
                return true;
            }
            lastCompleted = {entry.point.queue, completed};
        }
        return entry.point.value > lastCompleted.value;
    });
    deferredReleaseQueue.erase(deferredReleaseQueue.begin(), pending);
}

void HardwareExecutorVulkan::waitForAllDeferredResources()
{
    if (!lastSubmission) return;
    // Queue order and explicit cross-queue waits link this executor's submissions.
    const auto device = hardwareContext->deviceManager.getLogicalDevice();
    const auto timeline = lastSubmission.queue->timelineSemaphore;
    coronaHardwareCheck(waitTimeline(device, timeline, lastSubmission.value));
    lastCompleted = lastSubmission;
    deferredReleaseQueue.clear();
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
        uniqueSemaphores.insert(entry.point.queue->timelineSemaphore);
        minTimeline = std::min(minTimeline, entry.point.value);
        maxTimeline = std::max(maxTimeline, entry.point.value);
    }

    stats.totalSemaphores = uniqueSemaphores.size();
    stats.oldestTimeline = minTimeline;
    stats.newestTimeline = maxTimeline;

    return stats;
}

HardwareExecutorVulkan &HardwareExecutorVulkan::commit(bool submitPendingWaits)
{
    if (commandList.empty() && !(submitPendingWaits && !pendingWaits.empty())) return *this;
    auto& device = hardwareContext->deviceManager;
    cleanupCompletedResources();
    // Reserve before submission so every submitted command retains its resources on success.
    deferredReleaseQueue.reserve(deferredReleaseQueue.size() + 1);
    std::vector<VkSemaphoreSubmitInfo> waits, signals;
    waits.reserve(waitSemaphores.size() + pendingWaits.size() + 2);
    signals.reserve(signalSemaphores.size() + 1);
    for (auto info : waitSemaphores) addSemaphore(waits, info);
    for (auto info : signalSemaphores) addSemaphore(signals, info);
    for (const auto point : pendingWaits)
    {
        if (const auto imported = device.getOrImportTimelineSemaphore(*point.queue))
            addSemaphore(waits, timelineSubmitInfo(imported, point.value));
        else
        {
            VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
            wait.semaphoreCount = 1;
            wait.pSemaphores = &point.queue->timelineSemaphore;
            wait.pValues = &point.value;
            coronaHardwareCheck(vkWaitSemaphores(point.queue->deviceManager->getLogicalDevice(), &wait, 5'000'000'000ULL));
        }
    }
    // Graphics submissions already form a single ordered chain. Keep it on one physical
    // queue rather than making each render and blit hand off to another queue.
    using Type = CommandRecordVulkan::ExecutorType;
    Type type = Type::Transfer;
    for (const auto& command : commandList)
    {
        if (command.type == Type::Graphics) { type = Type::Graphics; break; }
        if (command.type == Type::Compute) type = Type::Compute;
    }
    auto* queues = &device.graphicsQueues;
    size_t index = 0;
    if (type == Type::Compute && !device.computeQueues.empty())
    {
        queues = &device.computeQueues;
        index = device.currentComputeQueueIndex.fetch_add(1) % queues->size();
    }
    else if (type == Type::Transfer && !device.transferQueues.empty())
    {
        queues = &device.transferQueues;
        index = device.currentTransferQueueIndex.fetch_add(1) % queues->size();
    }
    auto& queue = queues->at(index);
    if (lastSubmission && lastSubmission.queue != &queue)
        addSemaphore(waits, timelineSubmitInfo(lastSubmission.queue->timelineSemaphore, lastSubmission.value));
    std::lock_guard lock(*queue.queueMutex);
    const uint64_t previous = queue.timelineValue->load(std::memory_order_acquire);
    if (previous == UINT64_MAX)
        throw std::runtime_error("Queue timeline is exhausted");
    const uint64_t signalValue = previous + 1;
    // Re-record only after the previous use of THIS command buffer has completed.
    if (previous >= queue.commandBuffers.size())
    {
        const uint64_t recycleValue = signalValue - queue.commandBuffers.size();
        if (lastCompleted.queue != &queue || lastCompleted.value < recycleValue)
        {
            coronaHardwareCheck(waitTimeline(device.logicalDevice, queue.timelineSemaphore, recycleValue));
            lastCompleted = {&queue, recycleValue};
        }
    }

    currentRecordQueue = &queue;
    const auto cb = queue.commandBuffer = queue.commandBuffers[previous % queue.commandBuffers.size()];
    coronaHardwareCheck(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    coronaHardwareCheck(vkBeginCommandBuffer(cb, &begin));

    ResourceLayoutJournal layouts;
    ResourceBarrierPlanner planner;
    ResourceAccessList accesses;
    ResourceBarriers barriers;
    recordedRasterPassCount = 0;
    for (auto& recorded : commandList)
    {
        accesses.clear();
        recorded.command->resolveResourceUses(recorded.resourceUses, accesses);
        layouts.capture(accesses);
        planner.plan(accesses, barriers, &recorded == &commandList.back());
        if (!barriers.memoryBarriers.empty() || !barriers.bufferBarriers.empty() || !barriers.imageBarriers.empty())
        {
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.memoryBarrierCount = static_cast<uint32_t>(barriers.memoryBarriers.size());
            dependency.pMemoryBarriers = barriers.memoryBarriers.data();
            dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.bufferBarriers.size());
            dependency.pBufferMemoryBarriers = barriers.bufferBarriers.data();
            dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.imageBarriers.size());
            dependency.pImageMemoryBarriers = barriers.imageBarriers.data();
            vkCmdPipelineBarrier2(cb, &dependency);
        }
        recorded.command->commitCommand(*this);
    }
    planner.recordHostReadBarrier(cb);
    coronaHardwareCheck(vkEndCommandBuffer(cb));

    // A future wait or a larger external signal on our private timeline would
    // break the correspondence between command-buffer slots and completion values.
    if (std::ranges::any_of(waits, [&](const auto& i) { return i.semaphore == queue.timelineSemaphore && i.value > previous; }) ||
        std::ranges::any_of(signals, [&](const auto& i) { return i.semaphore == queue.timelineSemaphore && i.value > signalValue; }))
        throw std::invalid_argument("Conflicting queue timeline value");
    addSemaphore(signals, timelineSubmitInfo(queue.timelineSemaphore, signalValue));
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = cb;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    submit.pWaitSemaphoreInfos = waits.data();
    submit.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    submit.pSignalSemaphoreInfos = signals.data();
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;

    // Resource retirement samples this counter concurrently with queue submission.
    queue.timelineValue->store(signalValue, std::memory_order_release);
    const auto result = vkQueueSubmit2(queue.vkQueue, 1, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS)
    {
        queue.timelineValue->store(previous, std::memory_order_release);
        coronaHardwareCheck(result);
    }
    layouts.committed = true;
    lastSubmission = {&queue, signalValue};
    deferredReleaseQueue.push_back({lastSubmission, std::move(commandList)});
    pendingWaits.clear();
    waitSemaphores.clear();
    signalSemaphores.clear();
    return *this;
}
